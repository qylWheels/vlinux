#include "syscall_handler.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unicorn/unicorn.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>

#include "mm.h"

namespace vlinux {
namespace syscall {
std::function<std::int64_t(std::uint64_t, std::uint64_t, std::uint64_t)>
    calc_unmap_page_cnt = [](std::uint64_t old_addr, std::uint64_t new_addr,
                             std::uint64_t pgsize) -> std::int64_t {
    auto old_round_up =
        (old_addr + vlinux::mm::PAGE_SIZE - 1) & (~(vlinux::mm::PAGE_SIZE - 1));
    auto new_round_up =
        (new_addr + vlinux::mm::PAGE_SIZE - 1) & (~(vlinux::mm::PAGE_SIZE - 1));
    return (static_cast<std::int64_t>(new_round_up) -
            static_cast<std::int64_t>(old_round_up)) /
           static_cast<std::int64_t>(vlinux::mm::PAGE_SIZE);
};

class SysMmapHandler {
public:
    SysMmapHandler() = default;
    ~SysMmapHandler() = default;
    SysMmapHandler(const SysMmapHandler&) = delete;
    SysMmapHandler& operator=(const SysMmapHandler&) = delete;
    SysMmapHandler(SysMmapHandler&&) = delete;
    SysMmapHandler& operator=(SysMmapHandler&&) = delete;

public:
    std::uint64_t handle(SyscallHandler::Context ctx,
                         std::array<std::uint64_t, 6> args) {
        auto task = ctx.task;
        auto ppa = ctx.ppa;

        void* addr = reinterpret_cast<void*>(args[0]);
        std::size_t len = args[1];
        int prot = args[2];
        int flags = args[3];
        int fd = args[4];
        std::int64_t offset = args[5];

        if (addr != nullptr) return -EINVAL;

        if (len == 0) return -EINVAL;

        if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) return -EINVAL;

        if (flags & ~(MAP_PRIVATE | MAP_ANONYMOUS)) return -EINVAL;
        if (!(flags & MAP_PRIVATE)) return -EINVAL;

        if (fd != -1) return -EINVAL;

        // Calculate the page count.
        auto len_round_up = (len + mm::PAGE_SIZE - 1) & (~(mm::PAGE_SIZE - 1));
        auto pagecnt = len_round_up / mm::PAGE_SIZE;

        // Allocate physical pages.
        std::vector<std::shared_ptr<mm::PhysicalPageDescriptor>> ppages;
        for (auto i = 0; i < pagecnt; i++) {
            auto result = ppa->alloc();
            if (result.has_error()) {
                // Rollback.
                for (auto ppage : ppages) {
                    (void)ppa->free(ppage);
                }
                return -ENOMEM;
            }
            ppages.push_back(result.value());
        }

        // Map virtual pages.
        auto mmap = task->address_space->mmap;
        std::vector<std::shared_ptr<mm::VirtualPageDescriptor>> vdescs;
        for (auto i = 0; i < pagecnt; i++) {
            auto vdesc = std::make_shared<mm::VirtualPageDescriptor>();
            vdesc->start_addr = mmap + i * mm::PAGE_SIZE;
            vdesc->len = mm::PAGE_SIZE;
            vdesc->perm |= ((prot & PROT_READ) ? UC_PROT_READ : 0);
            vdesc->perm |= ((prot & PROT_WRITE) ? UC_PROT_WRITE : 0);
            vdesc->perm |= ((prot & PROT_EXEC) ? UC_PROT_EXEC : 0);
            auto result1 = task->page_table->map(vdesc, ppages[i]);
            auto result2 = task->address_space->vpages.insert(vdesc);
            if (result1.has_error() || !result2.second) {
                // Rollback.
                task->address_space->mmap = mmap;
                for (auto vdesc : vdescs) {
                    task->address_space->vpages.erase(vdesc);
                    (void)task->page_table->unmap(vdesc);
                }
                for (auto ppage : ppages) {
                    (void)ppa->free(ppage);
                }
                return -ENOMEM;
            }
            task->address_space->mmap += mm::PAGE_SIZE;
            vdescs.push_back(vdesc);
        }

        return mmap;
    }
};

class SysMunmapHandler {
public:
    SysMunmapHandler() = default;
    ~SysMunmapHandler() = default;
    SysMunmapHandler(const SysMunmapHandler&) = delete;
    SysMunmapHandler& operator=(const SysMunmapHandler&) = delete;
    SysMunmapHandler(SysMunmapHandler&&) = delete;
    SysMunmapHandler& operator=(SysMunmapHandler&&) = delete;

public:
    std::uint64_t handle(SyscallHandler::Context ctx,
                         std::array<std::uint64_t, 6> args) {
        std::uint64_t start = args[0], len = args[1], end = start + len;
        if (start % mm::PAGE_SIZE != 0) return -EINVAL;

        auto end_round_up = (end + mm::PAGE_SIZE - 1) & (~(mm::PAGE_SIZE - 1));
        auto pagecnt = (end_round_up - start) / mm::PAGE_SIZE;

        for (auto i = start; i < end_round_up; i += mm::PAGE_SIZE) {
            // Unmap virtual pages.
            auto vdesc_it = std::find_if(
                ctx.task->address_space->vpages.begin(),
                ctx.task->address_space->vpages.end(),
                [i](const auto& vdesc) { return vdesc->start_addr == i; });
            if (vdesc_it == ctx.task->address_space->vpages.end()) continue;
            (void)ctx.task->page_table->unmap(*vdesc_it);

            // Free physical pages.
            auto pdesc_result = ctx.task->page_table->vdesc_to_pdesc(*vdesc_it);
            if (pdesc_result.has_value()) {
                (void)ctx.ppa->free(pdesc_result.value());
            }
            ctx.task->address_space->vpages.erase(vdesc_it);
        }

        return 0;
    }
};

class SysMprotectHandler {
public:
    SysMprotectHandler() = default;
    ~SysMprotectHandler() = default;
    SysMprotectHandler(const SysMprotectHandler&) = delete;
    SysMprotectHandler& operator=(const SysMprotectHandler&) = delete;
    SysMprotectHandler(SysMprotectHandler&&) = delete;
    SysMprotectHandler& operator=(SysMprotectHandler&&) = delete;

public:
    std::uint64_t handle(SyscallHandler::Context ctx,
                         std::array<std::uint64_t, 6> args) {
        auto task = ctx.task;

        void* addr = reinterpret_cast<void*>(args[0]);
        std::size_t len = args[1];
        int prot = args[2];

        std::uint64_t start = reinterpret_cast<uint64_t>(addr);

        // addr not a multiple of the system page size.
        if (start % mm::PAGE_SIZE != 0) {
            return -EINVAL;
        }

        // Invalid prot.
        if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) {
            return -EINVAL;
        }

        std::uint64_t end = start + len;
        std::uint64_t start_round_down = start - (start % mm::PAGE_SIZE);
        std::uint64_t end_round_up =
            (end + (mm::PAGE_SIZE - 1)) & (~(mm::PAGE_SIZE - 1));

        // [addr, addr + len] is not all in the address space.
        // O(n^2).
        for (auto i = start_round_down; i < end_round_up; i += mm::PAGE_SIZE) {
            if (std::find_if(task->address_space->vpages.begin(),
                             task->address_space->vpages.end(),
                             [&](auto vdesc) {
                                 return vdesc->start_addr == i;
                             }) == task->address_space->vpages.end()) {
                return -ENOMEM;
            }
        }

        std::uint64_t page_cnt =
            calc_unmap_page_cnt(start, end, mm::PAGE_SIZE) + 1;

        for (std::uint64_t i = start_round_down; i < end_round_up;
             i += mm::PAGE_SIZE) {
            // vdesc_it must be valid.
            auto vdesc_it = std::find_if(
                task->address_space->vpages.begin(),
                task->address_space->vpages.end(),
                [i](auto vdesc) { return vdesc->start_addr == i; });
            std::uint64_t uc_prot = 0;
            uc_prot |= ((prot & PROT_READ) ? UC_PROT_READ : 0);
            uc_prot |= ((prot & PROT_WRITE) ? UC_PROT_WRITE : 0);
            uc_prot |= ((prot & PROT_EXEC) ? UC_PROT_EXEC : 0);
            (*vdesc_it)->perm = uc_prot;
        }

        return 0;
    }
};

class SysBrkHandler {
public:
    SysBrkHandler() = default;
    ~SysBrkHandler() = default;

private:
    void* brk_expand(SyscallHandler::Context context,
                     void* addr) {  // Check if the address is valid.
        auto task = context.task;

        std::uint64_t page_cnt = calc_unmap_page_cnt(
            task->address_space->brk, reinterpret_cast<uint64_t>(addr),
            mm::PAGE_SIZE);

        // Allocate pages eagerly, we wouldn't implement lazy allocation now.
        std::vector<std::shared_ptr<mm::PhysicalPageDescriptor>> pdescs;
        std::vector<std::shared_ptr<mm::VirtualPageDescriptor>> vdescs;
        // Cleanup function.
        auto cleanup = [this, &task, &pdescs, &vdescs, &context]() {
            // Clean up page table.
            for (auto vdesc : vdescs) {
                (void)task->page_table->unmap(vdesc);
            }
            // Free the physical pages.
            for (auto pdesc : pdescs) {
                (void)context.ppa->free(pdesc);
            }
        };
        for (std::uint64_t i = 0; i < page_cnt; i++) {
            // Allocate physical page.
            auto pdesc_result = context.ppa->alloc();
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
                // Cleanup.
                cleanup();
                return reinterpret_cast<void*>(task->address_space->brk);
            }
        }

        // Update brk.
        task->address_space->brk = reinterpret_cast<uint64_t>(addr);

        return reinterpret_cast<void*>(task->address_space->brk);
    }

    void* brk_shrink(SyscallHandler::Context context, void* addr) {
        auto task = context.task;

        // Calculate the number of pages to shrink.
        std::uint64_t page_cnt = std::abs(calc_unmap_page_cnt(
            task->address_space->brk, reinterpret_cast<uint64_t>(addr),
            mm::PAGE_SIZE));

        // Delete page table maps and physical pages.
        // Record victim vdescs.
        std::vector<std::shared_ptr<mm::VirtualPageDescriptor>> vdesc_victims;
        for (auto it = task->address_space->vpages.rbegin();
             it != task->address_space->vpages.rend(); it++) {
            if (page_cnt == 0) {
                break;
            }

            // Not in [start_brk, brk), skip.
            if ((*it)->start_addr >= task->address_space->brk) {
                continue;
            }

            // Get pdesc related to vdesc.
            auto pdesc = task->page_table->vdesc_to_pdesc(*it).value();

            // Unmap.
            (void)task->page_table->unmap(*it);

            // Record victim vdescs.
            vdesc_victims.push_back(*it);

            // Flush TLB and TB.
            ::uc_ctl_flush_tlb(context.uc);
            ::uc_ctl_flush_tb(context.uc);

            // Free physical page.
            (void)context.ppa->free(pdesc);

            page_cnt--;
        }

        // Remove victim vdescs from address space.
        for (auto vdesc : vdesc_victims) {
            (void)task->address_space->vpages.erase(vdesc);
        }

        // Update brk.
        task->address_space->brk = reinterpret_cast<uint64_t>(addr);

        return reinterpret_cast<void*>(task->address_space->brk);
    }

public:
    std::uint64_t handle_brk(SyscallHandler::Context context,
                             std::array<std::uint64_t, 6> args) {
        void* addr = reinterpret_cast<void*>(args[0]);

        auto task = context.task;
        if (reinterpret_cast<uint64_t>(addr) < task->address_space->start_brk) {
            return task->address_space->brk;
        }

        auto brk = task->address_space->brk;
        if (reinterpret_cast<uint64_t>(addr) > brk) {
            return reinterpret_cast<std::uint64_t>(
                this->brk_expand(context, addr));
        } else if (reinterpret_cast<uint64_t>(addr) < brk) {
            return reinterpret_cast<std::uint64_t>(
                this->brk_shrink(context, addr));
        } else {
            return brk;
        }
    }
};

class SysCloneHandler {
public:
    SysCloneHandler() = default;
    ~SysCloneHandler() = default;
    SysCloneHandler(const SysCloneHandler&) = delete;
    SysCloneHandler& operator=(const SysCloneHandler&) = delete;
    SysCloneHandler(SysCloneHandler&&) = delete;
    SysCloneHandler& operator=(SysCloneHandler&&) = delete;

public:
    std::uint64_t handle(SyscallHandler::Context context,
                         std::array<std::uint64_t, 6> args) {
        auto fn_addr = args[0];
        auto stack_addr = args[1];
        auto flags = args[2];
        auto arg_addr = args[3];

        if (fn_addr == 0) return -EINVAL;
        if (stack_addr == 0) return -EINVAL;
        if (flags != 0) return -EINVAL;  // Only support flags = 0 for now.

        // Create task context.
        uc_err err;
        uc_context* ctx = nullptr;
        err = ::uc_context_alloc(context.uc, &ctx);
        if (err != UC_ERR_OK) {
            return -ENOMEM;
        }

        // Hand the context over to the VM.
        if (!context.reg_ctx_in_vm(ctx)) {
            ::uc_context_free(ctx);
            return -ENOMEM;
        }

        // Setup task context.
        err = ::uc_context_save(context.uc, ctx);
        if (err != UC_ERR_OK) {
            return -ENOMEM;
        }
        err = ::uc_context_reg_write(ctx, UC_X86_REG_RIP, &fn_addr);
        if (err != UC_ERR_OK) {
            return -ENOMEM;
        }

        // Create task.
        auto task = std::make_shared<vlinux::Task>(ctx);

        // Setup task.
        auto parent = context.scheduler->current_task();
        task->name = parent->name;
        task->root_task = false;
        auto pid_result = context.pid_manager->alloc_pid();
        if (!pid_result) {
            return -EAGAIN;
        }
        task->pid = pid_result.value();
        task->tgid = parent->tgid;
        task->parent = parent;
        task->children = {};
        parent->children.push_back(task);
        task->state = Task::State::Ready;
        task->stack_bottom = stack_addr;
        // We are not able to set stack_top because we do not
        // know the stack size.
        // task->stack_top = stack_addr;
        task->address_space = std::make_shared<mm::VirtualMemoryAddressSpace>(
            *parent->address_space);
        task->address_space->vpages = {};
        task->page_table = std::make_shared<mm::PageTable>();
        std::vector<std::shared_ptr<mm::PhysicalPageDescriptor>> alloced_ppages;
        auto cleanup = [&]() {
            for (auto alloced_ppage : alloced_ppages) {
                (void)context.ppa->free(alloced_ppage);
            }
        };
        for (auto& vdesc : parent->address_space->vpages) {
            auto child_vdesc =
                std::make_shared<mm::VirtualPageDescriptor>(*vdesc);
            task->address_space->vpages.insert(child_vdesc);
            // Allocate physical page eagerly.
            auto ppage_result = context.ppa->alloc();
            if (!ppage_result) {
                cleanup();
                return -ENOMEM;
            }
            alloced_ppages.push_back(ppage_result.value());
            auto map_result =
                task->page_table->map(child_vdesc, ppage_result.value());
            if (!map_result) {
                cleanup();
                return -ENOMEM;
            }
        }

        // Add task to scheduler.
        auto add_result = context.scheduler->add_task(task);
        if (!add_result) {
            cleanup();
            return -ENOMEM;
        }

        return task->pid;
    }
};

class SyscallHandler::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    SysMmapHandler sys_mmap_handler_;
    SysMunmapHandler sys_munmap_handler_;
    SysMprotectHandler sys_mprotect_handler_;
    SysBrkHandler sys_brk_handler_;
    SysCloneHandler sys_clone_handler_;

public:  // Hooks.
    std::vector<std::function<void(std::uint64_t syscall_id,
                                   std::array<std::uint64_t, 6> args,
                                   std::uint64_t ret)>>
        syscall_hooks_;
};

SyscallHandler::SyscallHandler() : impl_(std::make_unique<Impl>()) {}

SyscallHandler::~SyscallHandler() = default;

std::uint64_t SyscallHandler::dispatch(Context context,
                                       std::uint64_t syscall_id,
                                       std::array<std::uint64_t, 6> args) {
    std::uint64_t ret;
    switch (syscall_id) {
        case SYS_mmap:
            ret = this->impl_->sys_mmap_handler_.handle(context, args);
            break;
        case SYS_munmap:
            ret = this->impl_->sys_munmap_handler_.handle(context, args);
            break;
        case SYS_mprotect:
            ret = this->impl_->sys_mprotect_handler_.handle(context, args);
            break;
        case SYS_brk:
            ret = this->impl_->sys_brk_handler_.handle_brk(context, args);
            break;
        case SYS_clone:
            ret = this->impl_->sys_clone_handler_.handle(context, args);
            break;
        default:
            ret = -ENOSYS;
            break;
    }

    // Trigger syscall hooks.
    for (auto& hook : this->impl_->syscall_hooks_) {
        hook(syscall_id, args, ret);
    }

    return ret;
}

outcome::result<void> SyscallHandler::add_syscall_hook(
    std::function<void(std::uint64_t syscall_id,
                       std::array<std::uint64_t, 6> args, std::uint64_t ret)>
        hook) {
    this->impl_->syscall_hooks_.push_back(hook);
    return outcome::success();
}
}  // namespace syscall
}  // namespace vlinux
