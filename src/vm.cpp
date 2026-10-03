#include "vm.h"

#include <sys/syscall.h>
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <elfio/elfio.hpp>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <outcome.hpp>
#include <system_error>
#include <vector>

#include "error.h"
#include "mm.h"
#include "syscall_handler.h"

namespace vlinux {
class VM::Impl {
public:
    Impl() {
        uc_err err;

        // Create Unicorn engine.
        err = uc_open(UC_ARCH_X86, UC_MODE_64, &this->uc_);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(
                std::format("uc_open failed: {}", uc_strerror(err)));
        }

        // Set TLB to virtual mode. i.e., we translate virtual addresses to
        // physical addresses by ourselves.
        err = uc_ctl_tlb_mode(this->uc_, UC_TLB_VIRTUAL);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(
                std::format("uc_ctl_tlb_mode failed: {}", uc_strerror(err)));
        }

        this->scheduler_ = std::make_shared<Scheduler>(this->uc_);
        this->ppa_ = std::make_shared<mm::PhysicalPageAllocator>(this->uc_);
        this->infinite_loop_code_ = static_cast<std::uint8_t*>(
            std::aligned_alloc(mm::PAGE_SIZE, mm::PAGE_SIZE));
        this->infinite_loop_code_[0] = 0xEB;
        this->infinite_loop_code_[1] = 0xFE;

        // Setup task initializer.
        this->task_initializer_ = TaskInitializer();
        this->task_initializer_.create_task_ctx =
            [this]() -> outcome::result<uc_context*> {
            uc_err err;
            uc_context* ctx = nullptr;
            err = ::uc_context_alloc(this->uc_, &ctx);
            if (err != UC_ERR_OK) {
                return make_error_code(err);
            }
            return outcome::success(ctx);
        };
        this->task_initializer_.setup_task_ctx =
            [this](uc_context* ctx,
                   std::uint64_t rip) -> outcome::result<void> {
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
                   std::vector<std::shared_ptr<Task>> children,
                   Task::State state, std::uint64_t stack_top,
                   std::uint64_t stack_bottom,
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

    ~Impl() {
        // Free contexts.
        for (auto ctx : this->contexts_) {
            ::uc_context_free(ctx);
        }

        // Release the physical page allocator *before* closing the engine:
        // Because ~PhysicalPageAllocator() unmaps every allocated physical page
        // through uc_mem_unmap().
        this->ppa_.reset();

        // Free infinite loop code.
        std::free(this->infinite_loop_code_);

        ::uc_close(this->uc_);
    }

public:
    outcome::result<void> setup_idle_and_init_task() {
        uc_err err;

        // Map the code to unicorn.
        err = ::uc_mem_map_ptr(
            this->uc_, mm::kIdleTaskCodeRegionStart, mm::PAGE_SIZE,
            UC_PROT_READ | UC_PROT_EXEC,
            reinterpret_cast<void*>(this->infinite_loop_code_));
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
        err = ::uc_mem_map_ptr(
            this->uc_, mm::kInitTaskCodeRegionStart, mm::PAGE_SIZE,
            UC_PROT_READ | UC_PROT_EXEC,
            reinterpret_cast<void*>(this->infinite_loop_code_));
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }

        // Setup idle task.
        OUTCOME_TRY(auto idle_task_pid, this->pid_manager_.alloc_pid());
        auto idle_task_tgid = idle_task_pid;
        auto idle_task_pdesc = std::make_shared<mm::PhysicalPageDescriptor>();
        idle_task_pdesc->start_addr = mm::kIdleTaskCodeRegionStart;
        idle_task_pdesc->len = mm::PAGE_SIZE;
        idle_task_pdesc->refcount = 1;
        idle_task_pdesc->flags = 0;
        auto idle_task_vdesc = std::make_shared<mm::VirtualPageDescriptor>();
        idle_task_vdesc->start_addr = mm::kIdleTaskCodeRegionStart;
        idle_task_vdesc->len = mm::PAGE_SIZE;
        idle_task_vdesc->perm = UC_PROT_READ | UC_PROT_EXEC;
        std::shared_ptr<mm::VirtualMemoryAddressSpace> idle_address_space =
            std::make_shared<mm::VirtualMemoryAddressSpace>();
        *idle_address_space = {
            .vpages = {idle_task_vdesc},
        };
        auto idle_task_pagetable = std::make_shared<mm::PageTable>();
        OUTCOME_TRY(idle_task_pagetable->map(idle_task_vdesc, idle_task_pdesc));
        OUTCOME_TRY(
            auto idle_task,
            this->task_initializer_.init_task(
                mm::kIdleTaskCodeRegionStart, "idle", true, idle_task_pid,
                idle_task_tgid, nullptr, {}, Task::State::Ready, 0, 0,
                idle_address_space, idle_task_pagetable));

        // Setup init task.
        OUTCOME_TRY(auto init_task_pid, this->pid_manager_.alloc_pid());
        auto init_task_tgid = init_task_pid;
        auto init_task_pdesc = std::make_shared<mm::PhysicalPageDescriptor>();
        init_task_pdesc->start_addr = mm::kInitTaskCodeRegionStart;
        init_task_pdesc->len = mm::PAGE_SIZE;
        init_task_pdesc->refcount = 1;
        init_task_pdesc->flags = 0;
        auto init_task_vdesc = std::make_shared<mm::VirtualPageDescriptor>();
        init_task_vdesc->start_addr = mm::kInitTaskCodeRegionStart;
        init_task_vdesc->len = mm::PAGE_SIZE;
        init_task_vdesc->perm = UC_PROT_READ | UC_PROT_EXEC;
        std::shared_ptr<mm::VirtualMemoryAddressSpace> init_address_space =
            std::make_shared<mm::VirtualMemoryAddressSpace>();
        *init_address_space = {.vpages = {init_task_vdesc}};
        auto init_task_pagetable = std::make_shared<mm::PageTable>();
        OUTCOME_TRY(init_task_pagetable->map(init_task_vdesc, init_task_pdesc));
        OUTCOME_TRY(
            auto init_task,
            this->task_initializer_.init_task(
                mm::kInitTaskCodeRegionStart, "init", true, init_task_pid,
                init_task_tgid, idle_task, {}, Task::State::Ready, 0, 0,
                init_address_space, init_task_pagetable));

        // Set idle children to init task.
        idle_task->children.push_back(init_task);

        return outcome::success();
    }

public:
    static void syscall_hook_callback(uc_engine* engine, void* user_data) {
        Impl* self = reinterpret_cast<Impl*>(user_data);
        std::uint64_t syscall_number;
        std::uint64_t args[6];
        std::uint64_t* argptrs[6] = {&args[0], &args[1], &args[2],
                                     &args[3], &args[4], &args[5]};
        int argregs[] = {UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX,
                         UC_X86_REG_R10, UC_X86_REG_R8,  UC_X86_REG_R9};
        uc_err err;

        // Read syscall number.
        err = uc_reg_read(engine, UC_X86_REG_RAX, &syscall_number);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(
                std::format("uc_reg_read failed: {}", uc_strerror(err)));
        }

        // Read syscall arguments.
        err = uc_reg_read_batch(engine, argregs,
                                reinterpret_cast<void**>(argptrs), 6);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(
                std::format("uc_reg_read_batch failed: {}", uc_strerror(err)));
        }

        // Dispatch.
        auto task = self->scheduler_->current_task();
        std::uint64_t ret = self->syscall_handler_.dispatch(
            {self->uc_, task, self->ppa_}, syscall_number, std::to_array(args));

        // Write return value back to RAX.
        err = uc_reg_write(engine, UC_X86_REG_RAX, &ret);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(
                std::format("uc_reg_write failed: {}", uc_strerror(err)));
        }

        // Record syscall.
        task->syscalls.push_back(
            {static_cast<uint32_t>(syscall_number), std::to_array(args), ret});
    }

public:
    std::uint64_t entrypoint_;

    // Task-related fields.
    std::uint8_t* infinite_loop_code_;
    TaskInitializer task_initializer_;
    PidManager pid_manager_;
    std::set<uc_context*> contexts_;
    std::set<std::shared_ptr<Task>> tasks_;
    std::shared_ptr<Scheduler> scheduler_;

    // Memory-related fields.
    std::shared_ptr<mm::PhysicalPageAllocator> ppa_;

    // Syscall-related fields.
    syscall::SyscallHandler syscall_handler_;

    uc_engine* uc_;
    uc_hook syscall_hook_;
    std::shared_ptr<Task> curr_task_;
};

VM::VM() { this->impl_ = std::make_unique<Impl>(); }

VM::~VM() = default;

void VM::reset() {}

outcome::result<std::shared_ptr<Task>> VM::load(
    const std::filesystem::path& path) {
    uc_err err;
    ELFIO::elfio reader;

    // Initialize idle and init task.
    OUTCOME_TRY(this->impl_->setup_idle_and_init_task());

    if (!reader.load(path)) {
        return std::make_error_code(std::errc::io_error);
    }

    auto address_space = std::make_shared<mm::VirtualMemoryAddressSpace>();
    auto page_table = std::make_shared<mm::PageTable>();
    // Load segments.
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
        std::vector<char> file_data;
        std::ifstream file(path.string(), std::ios::binary);
        file.seekg(file_offset);
        file_data.resize(file_size);
        file.read(file_data.data(), file_size);
        file.close();

        // Setup memory address area.
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
        // If segment is not page-aligned, it starts at page_off of its first
        // page, so map [page_start, page_start + span).
        std::uint64_t page_start = virtual_address & ~(mm::PAGE_SIZE - 1);
        std::uint64_t page_off = virtual_address - page_start;
        std::uint64_t span = page_off + virtual_size;
        for (std::uint64_t i = 0; i < span; i += mm::PAGE_SIZE) {
            // Allocate a physical page.
            OUTCOME_TRY(auto pdesc, this->impl_->ppa_->alloc());

            // Map the virtual page to the physical page.
            auto vdesc = std::make_shared<mm::VirtualPageDescriptor>();
            vdesc->start_addr = page_start + i;
            vdesc->len = mm::PAGE_SIZE;
            vdesc->perm = perms;
            OUTCOME_TRY(page_table->map(vdesc, pdesc));

            // Add the virtual page descriptor to address space.
            if (!address_space->vpages.insert(vdesc).second) {
                return std::errc::address_in_use;
            }

            // Prepare the page content.
            std::vector<char> mem_data(mm::PAGE_SIZE, 0);
            // Segment byte #src lands at dst of this page.
            std::uint64_t src = i > page_off ? i - page_off : 0;
            std::uint64_t dst = i > page_off ? 0 : page_off - i;
            if (src < file_size) {  // The page has file-backed bytes.
                std::uint64_t left = file_size - src;
                std::uint64_t room = mm::PAGE_SIZE - dst;
                std::uint64_t len = left > room ? room : left;
                std::copy_n(file_data.begin() + src, len,
                            mem_data.begin() + dst);
            }

            // Write data to physical page.
            err = ::uc_mem_write(this->impl_->uc_, pdesc->start_addr,
                                 mem_data.data(), mm::PAGE_SIZE);
            if (err != UC_ERR_OK) {
                return make_error_code(err);
            }
        }
    }

    // Set start_brk and brk. It is page-aligned as brk_expand() requires.
    auto highest_page_it = address_space->vpages.rbegin();
    if (highest_page_it == address_space->vpages.rend()) {
        return std::errc::address_not_available;
    }
    address_space->start_brk =
        (*highest_page_it)->start_addr + (*highest_page_it)->len;
    address_space->brk = address_space->start_brk;

    // Allocate stack memory.
    OUTCOME_TRY(auto stack_page_pdesc, this->impl_->ppa_->alloc());
    auto stack_page_vdesc = std::make_shared<mm::VirtualPageDescriptor>();
    stack_page_vdesc->start_addr = 0xf000'0000;
    stack_page_vdesc->len = mm::PAGE_SIZE;
    stack_page_vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
    OUTCOME_TRY(page_table->map(stack_page_vdesc, stack_page_pdesc));
    if (!address_space->vpages.insert(stack_page_vdesc).second) {
        return std::errc::address_in_use;
    }

    // Create task.
    OUTCOME_TRY(auto pid, this->impl_->pid_manager_.alloc_pid());
    auto tgid = pid;
    std::shared_ptr<Task> parent = nullptr;
    auto parent_it = std::find_if(
        this->impl_->tasks_.begin(), this->impl_->tasks_.end(),
        [](const std::shared_ptr<Task>& task) { return task->pid == 1; });
    if (parent_it != this->impl_->tasks_.end()) {
        parent = *parent_it;
    }
    OUTCOME_TRY(auto task, this->impl_->task_initializer_.init_task(
                               reader.get_entry(), path.filename().string(),
                               false, pid, tgid, parent, {}, Task::State::New,
                               stack_page_vdesc->start_addr,
                               stack_page_vdesc->start_addr + mm::PAGE_SIZE,
                               address_space, page_table));

    // Set stack top.
    std::uint64_t rsp = stack_page_vdesc->start_addr + mm::PAGE_SIZE;
    err = ::uc_context_reg_write(task->ctx, UC_X86_REG_RSP, &rsp);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Set the entrypoint.
    this->impl_->entrypoint_ = reader.get_entry();

    // Set curr_task.
    this->impl_->curr_task_ = task;

    return outcome::success(task);
}

outcome::result<VM::Result> VM::run(std::chrono::milliseconds timeout,
                                    std::chrono::milliseconds sched_interval) {
    uc_err err;

    // Add syscall hook.
    err = uc_hook_add(
        this->impl_->uc_, &this->impl_->syscall_hook_, UC_HOOK_INSN,
        reinterpret_cast<void*>(Impl::syscall_hook_callback), this->impl_.get(),
        0, std::numeric_limits<std::uint64_t>::max(), UC_X86_INS_SYSCALL);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Set state to ready.
    this->impl_->curr_task_->state = Task::State::Ready;

    // Start the scheduler.
    OUTCOME_TRY(
        this->impl_->scheduler_->start_schedule(timeout, sched_interval));

    // Return the result.
    Result result;
    for (auto task : this->impl_->tasks_) {
        result.behav_of_tasks_[task] = task->syscalls;
    }
    return outcome::success(result);
}

outcome::result<void> VM::add_syscall_hook(
    std::function<void(std::uint64_t syscall_id,
                       std::array<std::uint64_t, 6> args, std::uint64_t ret)>
        hook) {
    OUTCOME_TRY(this->impl_->syscall_handler_.add_syscall_hook(hook));
    return outcome::success();
}

// Observability.
Scheduler& VM::get_scheduler() { return *this->impl_->scheduler_; }
}  // namespace vlinux
