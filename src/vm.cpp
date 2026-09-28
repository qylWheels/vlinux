#include "vm.h"

#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <algorithm>
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
        idle_task_vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
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
        init_task_vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
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

public:  // Syscalls.
    void exit(int status) {
        auto task = this->scheduler_->current_task();
        task.value()->exit_status = status;
        task.value()->state = Task::State::Stopped;
        if (task.value()->root_task) {
            uc_emu_stop(this->uc_);
        }
    }

    // brk() syscall.
    void* brk(void* addr) {
        auto task_result = this->scheduler_->current_task();
        if (!task_result) {
            std::abort();  // Unreachable.
        }

        auto task = task_result.value();
        if (!task) {
            std::abort();  // Unreachable.
        }

        auto brk = task->address_space->brk;
        if (reinterpret_cast<uint64_t>(addr) > brk) {
            return this->brk_expand(task, addr);
        } else if (reinterpret_cast<uint64_t>(addr) < brk) {
            return this->brk_shrink(task, addr);
        } else {
            return reinterpret_cast<void*>(brk);
        }
    }

    void* brk_expand(std::shared_ptr<Task> task,
                     void* addr) {  // Check if the address is valid.
        if (reinterpret_cast<uint64_t>(addr) < task->address_space->start_brk) {
            return reinterpret_cast<void*>(task->address_space->brk);
        }

        // Round up the address to the nearest page boundary.
        std::uint64_t p = reinterpret_cast<uint64_t>(addr) + mm::PAGE_SIZE - 1;
        p &= ~(mm::PAGE_SIZE - 1);

        std::uint64_t page_cnt = (p - task->address_space->brk) / mm::PAGE_SIZE;

        // Allocate pages eagerly, we wouldn't implement lazy allocation now.
        std::vector<std::shared_ptr<mm::PhysicalPageDescriptor>> pdescs;
        std::vector<std::shared_ptr<mm::VirtualPageDescriptor>> vdescs;
        for (std::uint64_t i = 0; i < page_cnt; i++) {
            // Cleanup function.
            auto cleanup = [this, &task, &pdescs, &vdescs]() {
                // Clean up page table.
                for (auto vdesc : vdescs) {
                    (void)task->page_table->unmap(vdesc);
                }
                // Free the physical pages.
                for (auto pdesc : pdescs) {
                    (void)this->ppa_->free(pdesc);
                }
            };

            // Allocate physical page.
            auto pdesc_result = this->ppa_->alloc();
            if (!pdesc_result) {
                // Cleanup.
                cleanup();
                return reinterpret_cast<void*>(task->address_space->brk);
            }
            pdescs.push_back(pdesc_result.value());

            // Set virtual page descriptor.
            auto vdesc = std::make_shared<mm::VirtualPageDescriptor>();
            vdesc->start_addr = task->address_space->brk + i * mm::PAGE_SIZE;
            vdesc->len = mm::PAGE_SIZE;
            vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
            vdescs.push_back(vdesc);

            // Map virtual page.
            auto result = task->page_table->map(vdesc, pdesc_result.value());
            if (!result) {
                // Cleanup.
                cleanup();
                return reinterpret_cast<void*>(task->address_space->brk);
            }
        }

        // Update address space.
        for (auto vdesc : vdescs) {
            if (!task->address_space->vpages.insert(vdesc).second) {
                return reinterpret_cast<void*>(task->address_space->brk);
            }
        }

        // Update brk.
        task->address_space->brk = reinterpret_cast<uint64_t>(addr);

        return reinterpret_cast<void*>(task->address_space->brk);
    }

    void* brk_shrink(std::shared_ptr<Task> task, void* addr) {
        if (reinterpret_cast<uint64_t>(addr) < task->address_space->start_brk) {
            return reinterpret_cast<void*>(task->address_space->brk);
        }

        // Round up the address to the nearest page boundary.
        std::uint64_t p = reinterpret_cast<uint64_t>(addr) + mm::PAGE_SIZE - 1;
        p &= ~(mm::PAGE_SIZE - 1);

        // Calculate the number of pages to shrink.
        std::uint64_t page_cnt = (task->address_space->brk - p) / mm::PAGE_SIZE;

        // Delete page table maps, vdescs and physical pages.
        for (auto it = task->address_space->vpages.rbegin();
             it != task->address_space->vpages.rend(); it++) {
            if (page_cnt == 0) {
                break;
            }

            // Not in [start_brk, brk), skip.
            if ((*it)->start_addr >= task->address_space->brk) {
                continue;
            }

            // Unmap.
            (void)task->page_table->unmap(*it);

            // Remove vdesc.
            auto pdesc = task->page_table->vdesc_to_pdesc(*it).value();
            (void)task->address_space->vpages.erase(*it);

            // Free physical page.
            (void)this->ppa_->free(pdesc);

            page_cnt--;
        }

        // Update brk.
        task->address_space->brk = reinterpret_cast<uint64_t>(addr);

        return reinterpret_cast<void*>(task->address_space->brk);
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
        err = uc_reg_read_batch(engine, argregs,
                                reinterpret_cast<void**>(argptrs), 6);
        if (err != UC_ERR_OK) {
            throw std::runtime_error(
                std::format("uc_reg_read_batch failed: {}", uc_strerror(err)));
        }
        std::cout << std::format("args: {}, {}, {}, {}, {}, {}", *argptrs[0],
                                 *argptrs[1], *argptrs[2], *argptrs[3],
                                 *argptrs[4], *argptrs[5])
                  << std::endl;

        // Dispatch.
        switch (syscall_number) {
            case 60: {
                self->exit(args[0]);
                break;
            }
            case SYS_brk: {
                self->brk(reinterpret_cast<void*>(args[0]));
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

    uc_engine* uc_;
    uc_hook syscall_hook_;
    std::shared_ptr<Task> curr_task_;
};

VM::VM() { this->impl_ = std::make_unique<Impl>(); }

VM::~VM() = default;

void VM::reset() {}

outcome::result<void> VM::load(const std::filesystem::path& path) noexcept {
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
        std::uint64_t non_zero_len = file_size;
        std::uint64_t zero_len = virtual_size - file_size;
        for (std::uint64_t i = 0; i < virtual_size; i += mm::PAGE_SIZE) {
            // Allocate a physical page.
            OUTCOME_TRY(auto pdesc, this->impl_->ppa_->alloc());

            // Map the virtual page to the physical page.
            auto vdesc = std::make_shared<mm::VirtualPageDescriptor>();
            vdesc->start_addr = virtual_address + i;
            vdesc->len = mm::PAGE_SIZE;
            vdesc->perm = perms;
            OUTCOME_TRY(page_table->map(vdesc, pdesc));

            // Add the virtual page descriptor to address space.
            if (!address_space->vpages.insert(vdesc).second) {
                return std::errc::address_in_use;
            }

            // Prepare the page content.
            std::vector<char> mem_data(mm::PAGE_SIZE, 0);
            if (i < non_zero_len) {  // The page has file-backed bytes.
                std::uint64_t left_non_zero = non_zero_len - i;
                std::uint64_t len = left_non_zero > mm::PAGE_SIZE
                                        ? mm::PAGE_SIZE
                                        : left_non_zero;
                std::copy_n(file_data.begin() + i, len, mem_data.begin());
            }

            // Write data to physical page.
            err = ::uc_mem_write(this->impl_->uc_, pdesc->start_addr,
                                 mem_data.data(), mm::PAGE_SIZE);
            if (err != UC_ERR_OK) {
                return make_error_code(err);
            }
        }
    }

    // Set start_brk and brk.
    auto highest_page_it = address_space->vpages.rbegin();
    if (highest_page_it == address_space->vpages.rend()) {
        return std::errc::address_not_available;
    }
    OUTCOME_TRY(auto heap_page_pdesc, this->impl_->ppa_->alloc());
    auto heap_page_vdesc = std::make_shared<mm::VirtualPageDescriptor>();
    heap_page_vdesc->start_addr =
        (*highest_page_it)->start_addr + (*highest_page_it)->len;
    heap_page_vdesc->len = mm::PAGE_SIZE;
    heap_page_vdesc->perm = UC_PROT_READ | UC_PROT_WRITE;
    OUTCOME_TRY(page_table->map(heap_page_vdesc, heap_page_pdesc));
    if (!address_space->vpages.insert(heap_page_vdesc).second) {
        return std::errc::address_in_use;
    }
    address_space->start_brk = heap_page_vdesc->start_addr;
    address_space->brk = heap_page_vdesc->start_addr;

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

    return outcome::success();
}

outcome::result<void> VM::run(
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds sched_interval) noexcept {
    uc_err err;

    // Add syscall hook.
    err = uc_hook_add(
        this->impl_->uc_, &this->impl_->syscall_hook_, UC_HOOK_INSN,
        reinterpret_cast<void*>(Impl::syscall_hook_callback), this, 0,
        std::numeric_limits<std::uint64_t>::max(), UC_X86_INS_SYSCALL);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Set state to ready.
    this->impl_->curr_task_->state = Task::State::Ready;

    // Start the scheduler.
    OUTCOME_TRY(
        this->impl_->scheduler_->start_schedule(timeout, sched_interval));

    return outcome::success();
}
}  // namespace vlinux
