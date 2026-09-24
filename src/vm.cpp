#include "vm.h"

#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <cstdint>
#include <elfio/elfio.hpp>
#include <format>
#include <fstream>
#include <limits>
#include <outcome.hpp>
#include <system_error>

#include "error.h"
#include "mm.h"

namespace vlinux {
VM::VM() {
    uc_err err;

    // Create Unicorn engine.
    err = uc_open(UC_ARCH_X86, UC_MODE_64, &this->uc_);
    if (err != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_open failed: {}", uc_strerror(err)));
    }

    // Set TLB to virtual mode. i.e., we translate virtual addresses to physical
    // addresses by ourselves.
    err = uc_ctl_tlb_mode(this->uc_, UC_TLB_VIRTUAL);
    if (err != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_ctl_tlb_mode failed: {}", uc_strerror(err)));
    }

    this->scheduler_ = std::make_shared<Scheduler>(this->uc_);
    this->ppa_ = std::make_shared<mm::PhysicalPageAllocator>(this->uc_);

    // Setup task initializer.
    this->task_initializer_ = TaskInitializer();
    this->task_initializer_.create_task_ctx =
        [this]() -> outcome::result<uc_context*> {
        uc_err err;
        uc_context* ctx;
        err = ::uc_context_alloc(this->uc_, &ctx);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
        return outcome::success();
    };
    this->task_initializer_.setup_task_ctx =
        [this](uc_context* ctx, std::uint64_t rip) -> outcome::result<void> {
        uc_err err;

        // TODO: Don't save the context?
        err = ::uc_context_save(this->uc_, ctx);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
        err = ::uc_context_reg_write(ctx, UC_X86_REG_RIP, &rip);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }

        return outcome::success();
    };
    this->task_initializer_.add_task_ctx_to_context_manager =
        [this](uc_context* ctx) -> outcome::result<void> {
        auto [it, inserted] = this->contexts_.insert(ctx);
        if (!inserted) {
            return std::errc::file_exists;
        }
        return outcome::success();
    };
    this->task_initializer_.create_task =
        [this](uc_context* ctx) -> outcome::result<std::shared_ptr<Task>> {
        return std::make_shared<vlinux::Task>(ctx);
    };
    this->task_initializer_.setup_task_properties =
        [this](std::shared_ptr<Task> task, std::string name, bool root_task,
               std::int64_t pid, std::int64_t tgid,
               std::shared_ptr<Task> parent,
               std::vector<std::shared_ptr<Task>> children, Task::State state,
               std::uint64_t stack_top, std::uint64_t stack_bottom,
               std::shared_ptr<mm::VirtualMemoryAddressSpace> address_space,
               std::shared_ptr<mm::PageTable> page_table)
        -> outcome::result<void> {
        task->name = name;
        task->root_task = root_task;
        task->pid = pid;
        task->tgid = tgid;
        task->parent = parent;
        task->children = children;
        task->state = state;
        task->stack_top = stack_top;
        task->stack_bottom = stack_bottom;
        task->address_space = address_space;
        task->page_table = page_table;
        return outcome::success();
    };
    this->task_initializer_.add_task_to_task_manager =
        [this](std::shared_ptr<Task> task) -> outcome::result<void> {
        auto [it, inserted] = this->tasks_.insert(task);
        if (!inserted) {
            return std::errc::file_exists;
        }
        return outcome::success();
    };
    this->task_initializer_.add_task_to_scheduler =
        [this](std::shared_ptr<Task> task) -> outcome::result<void> {
        return this->scheduler_->add_task(task);
    };
}

VM::~VM() {
    // Free contexts.
    for (auto ctx : this->contexts_) {
        ::uc_context_free(ctx);
    }

    ::uc_close(this->uc_);
}

void VM::reset() {}

outcome::result<void> VM::load(const std::filesystem::path& path) noexcept {
    uc_err err;
    ELFIO::elfio reader;

    // Create task.
    this->curr_task_ = std::make_shared<Task>(nullptr);
    this->curr_task_->state = Task::State::New;
    this->curr_task_->root_task = true;
    OUTCOME_TRY(this->curr_task_->pid, this->pid_manager_.alloc_pid());

    if (!reader.load(path)) {
        return std::make_error_code(std::errc::io_error);
    }

    for (const auto& segment : reader.segments) {
        if (segment->get_type() != ELFIO::PT_LOAD) {
            // We only care about LOAD segments.
            continue;
        }

        auto file_offset = segment->get_offset();
        auto file_size = segment->get_file_size();
        auto virtual_address = segment->get_virtual_address();
        auto virtual_size = segment->get_memory_size();

        // Read the segment data from the file.
        std::vector<char> data;
        std::ifstream file(path.string(), std::ios::binary);
        file.seekg(file_offset);
        data.resize(file_size);
        file.read(data.data(), file_size);
        file.close();

        // Create memory mapping.
        std::uint32_t perms = 0;
        auto segment_flags = segment->get_flags();
        if (segment_flags & ELFIO::PF_R) {
            perms |= UC_PROT_READ;
        }
        if (segment_flags & ELFIO::PF_W) {
            perms |= UC_PROT_WRITE;
        }
        if (segment_flags & ELFIO::PF_X) {
            perms |= UC_PROT_EXEC;
        }
        err = uc_mem_map(
            this->uc_, virtual_address,
            (virtual_size + mm::PAGE_SIZE - 1) & ~(mm::PAGE_SIZE - 1), perms);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }

        // Write the segment data to the memory.
        err = uc_mem_write(this->uc_, virtual_address, data.data(), file_size);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
        std::vector<char> zeros(virtual_size - file_size);
        err = uc_mem_write(this->uc_, virtual_address + file_size, zeros.data(),
                           virtual_size - file_size);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
    }

    // Set the entrypoint.
    this->entrypoint_ = reader.get_entry();

    // Set the state to Ready.
    this->curr_task_->state = Task::State::Ready;

    return outcome::success();
}

outcome::result<void> VM::run() noexcept {
    uc_err err;

    // Add syscall hook.
    err = uc_hook_add(this->uc_, &this->syscall_hook_, UC_HOOK_INSN,
                      reinterpret_cast<void*>(VM::syscall_hook_callback), this,
                      0, std::numeric_limits<std::uint64_t>::max(),
                      UC_X86_INS_SYSCALL);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Setup stack.
    OUTCOME_TRY(auto stack_bottom_pa_desc, this->ppa_->alloc());
    std::uint64_t stack_bottom_pa = stack_bottom_pa_desc->start_addr;
    std::uint64_t stack_bottom_va = 0xf000'0000;
    std::uint64_t stack_top_va = stack_bottom_va + mm::PAGE_SIZE;
    err = uc_mem_map_ptr(this->uc_, stack_bottom_va, mm::PAGE_SIZE,
                         UC_PROT_READ | UC_PROT_WRITE,
                         reinterpret_cast<void*>(stack_bottom_pa));
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }
    err = uc_reg_write(this->uc_, UC_X86_REG_RSP, &stack_top_va);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Set state to Running.
    this->curr_task_->state = Task::State::Running;

    // Run!
    err = uc_emu_start(this->uc_, this->entrypoint_, 0, 0, 0);
    if (err != UC_ERR_OK) {
        std::uint64_t rip;
        uc_reg_read(this->uc_, UC_X86_REG_RIP, &rip);
        std::cout << std::format("rip: {:#x}", rip) << std::endl;
        return make_error_code(err);
    }

    return outcome::success();
}

outcome::result<void> VM::setup_idle_and_init_task() {
    uc_err err;

    // Setup task whose PID is 0(idle) and 1(init).
    // Infinite loop code for idle and init.
    std::uint8_t code[] = {0xEB, 0xFE};

    // Map the code to unicorn.
    err = ::uc_mem_map_ptr(this->uc_, mm::kIdleTaskCodeRegionStart,
                           sizeof(code), UC_PROT_READ | UC_PROT_WRITE,
                           reinterpret_cast<void*>(code));
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }
    err = ::uc_mem_map_ptr(this->uc_, mm::kInitTaskCodeRegionStart,
                           sizeof(code), UC_PROT_READ | UC_PROT_WRITE,
                           reinterpret_cast<void*>(code));
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Setup idle task.
    auto idle_task_pdesc = std::make_shared<mm::PhysicalPageDescriptor>();
    idle_task_pdesc->start_addr = mm::kIdleTaskCodeRegionStart;
    idle_task_pdesc->len = mm::PAGE_SIZE;
    idle_task_pdesc->refcount = 1;
    idle_task_pdesc->flags = 0;
    auto idle_task_vdesc = std::make_shared<mm::VirtualPageDescriptor>();
    idle_task_vdesc->start_addr = mm::kIdleTaskCodeRegionStart;
    idle_task_vdesc->len = mm::PAGE_SIZE;
    idle_task_vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
    std::shared_ptr<mm::VirtualMemoryAddressSpace> address_space =
        std::make_shared<mm::VirtualMemoryAddressSpace>();
    *address_space = {
        {{.start = mm::kIdleTaskCodeRegionStart,
          .end = mm::kIdleTaskCodeRegionStart + mm::kIdleTaskCodeRegionLen,
          .perm = UC_PROT_READ | UC_PROT_WRITE,
          .vdescs = {idle_task_vdesc},
          .address_space = &*address_space}}};
    auto idle_task_pagetable = std::make_shared<mm::PageTable>();
    OUTCOME_TRY(idle_task_pagetable->map(idle_task_vdesc, idle_task_pdesc));
    OUTCOME_TRY(this->task_initializer_.init_task(
        mm::kIdleTaskCodeRegionStart, "idle", true, 0, 0, nullptr, {},
        Task::State::Ready, 0, 0, address_space, idle_task_pagetable));

    // Setup init task.
    auto init_task_pdesc = std::make_shared<mm::PhysicalPageDescriptor>();
    init_task_pdesc->start_addr = mm::kInitTaskCodeRegionStart;
    init_task_pdesc->len = mm::PAGE_SIZE;
    init_task_pdesc->refcount = 1;
    init_task_pdesc->flags = 0;
    auto init_task_vdesc = std::make_shared<mm::VirtualPageDescriptor>();
    init_task_vdesc->start_addr = mm::kInitTaskCodeRegionStart;
    init_task_vdesc->len = mm::PAGE_SIZE;
    init_task_vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
    std::shared_ptr<mm::VirtualMemoryAddressSpace> init_address_space =
        std::make_shared<mm::VirtualMemoryAddressSpace>();
    *init_address_space = {
        {{.start = mm::kInitTaskCodeRegionStart,
          .end = mm::kInitTaskCodeRegionStart + mm::kInitTaskCodeRegionLen,
          .perm = UC_PROT_READ | UC_PROT_WRITE,
          .vdescs = {init_task_vdesc},
          .address_space = &*init_address_space}}};
    auto init_task_pagetable = std::make_shared<mm::PageTable>();
    OUTCOME_TRY(init_task_pagetable->map(init_task_vdesc, init_task_pdesc));
    // TODO: Set parent to idle task.
    OUTCOME_TRY(this->task_initializer_.init_task(
        mm::kInitTaskCodeRegionStart, "init", true, 1, 1, nullptr, {},
        Task::State::Ready, 0, 0, init_address_space, init_task_pagetable));

    return outcome::success();
}

void VM::exit(int status) {
    this->curr_task_->exit_status = status;
    this->curr_task_->state = Task::State::Stopped;
    if (this->curr_task_->root_task) {
        uc_emu_stop(this->uc_);
    }
}

void VM::syscall_hook_callback(uc_engine* engine, void* user_data) {
    VM* self = reinterpret_cast<VM*>(user_data);
    std::uint64_t syscall_number;
    std::uint64_t args[6];
    std::uint64_t* argptrs[6] = {&args[0], &args[1], &args[2],
                                 &args[3], &args[4], &args[5]};
    int argregs[] = {UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX,
                     UC_X86_REG_R10, UC_X86_REG_R8,  UC_X86_REG_R9};
    std::uint64_t ret;
    uc_err err;

    // Read syscall number.
    err = uc_reg_read(engine, UC_X86_REG_RAX, &syscall_number);
    if (err != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_reg_read failed: {}", uc_strerror(err)));
    }
    std::cout << std::format("syscall: {}", syscall_number) << std::endl;

    // Read syscall arguments.
    err = uc_reg_read_batch(engine, argregs, reinterpret_cast<void**>(argptrs),
                            6);
    if (err != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_reg_read_batch failed: {}", uc_strerror(err)));
    }
    std::cout << std::format("args: {}, {}, {}, {}, {}, {}", *argptrs[0],
                             *argptrs[1], *argptrs[2], *argptrs[3], *argptrs[4],
                             *argptrs[5])
              << std::endl;

    // Dispatch.
    switch (syscall_number) {
        case 60: {
            self->exit(args[0]);
            break;
        }
        default: {
            std::cout << std::format("syscall {} not implemented",
                                     syscall_number)
                      << std::endl;
            break;
        }
    }
}
}  // namespace vlinux
