#include "syscall_handler.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unicorn/unicorn.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

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

class SysMprotectHandler {
public:
    SysMprotectHandler() = default;
    ~SysMprotectHandler() = default;
    SysMprotectHandler(const SysMprotectHandler&) = delete;
    SysMprotectHandler& operator=(const SysMprotectHandler&) = delete;
    SysMprotectHandler(SysMprotectHandler&&) = delete;
    SysMprotectHandler& operator=(SysMprotectHandler&&) = delete;

public:
    int handle(std::shared_ptr<Task> task, void* addr, std::size_t len,
               int prot) {
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
                return -EINVAL;
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

public:
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

    void* handle_brk(SyscallHandler::Context context, void* addr) {
        auto task = context.task;
        if (reinterpret_cast<uint64_t>(addr) < task->address_space->start_brk) {
            return reinterpret_cast<void*>(task->address_space->brk);
        }

        auto brk = task->address_space->brk;
        if (reinterpret_cast<uint64_t>(addr) > brk) {
            return this->brk_expand(context, addr);
        } else if (reinterpret_cast<uint64_t>(addr) < brk) {
            return this->brk_shrink(context, addr);
        } else {
            return reinterpret_cast<void*>(brk);
        }
    }
};

class SyscallHandler::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    SysMprotectHandler sys_mprotect_handler_;
    SysBrkHandler sys_brk_handler_;

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
        case SYS_mprotect:
            ret = static_cast<std::uint64_t>(
                this->impl_->sys_mprotect_handler_.handle(
                    context.task, reinterpret_cast<void*>(args[0]), args[1],
                    static_cast<int>(args[2])));
            break;
        case SYS_brk:
            ret = reinterpret_cast<std::uint64_t>(
                this->impl_->sys_brk_handler_.handle_brk(
                    context, reinterpret_cast<void*>(args[0])));
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
