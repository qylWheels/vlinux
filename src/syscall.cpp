#include "syscall.h"

#include <sys/syscall.h>

#include <cstdlib>

namespace vlinux {
namespace syscall {
class SysBrkHandler {
public:
    SysBrkHandler() = default;
    ~SysBrkHandler() = default;

public:
    void* brk_expand(SyscallDispatcher::Context context,
                     void* addr) {  // Check if the address is valid.
        auto task = context.task;
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

    void* brk_shrink(SyscallDispatcher::Context context, void* addr) {
        auto task = context.task;
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
            (void)context.ppa->free(pdesc);

            page_cnt--;
        }

        // Update brk.
        task->address_space->brk = reinterpret_cast<uint64_t>(addr);

        return reinterpret_cast<void*>(task->address_space->brk);
    }

    void* handle_brk(SyscallDispatcher::Context context, void* addr) {
        auto task = context.task;
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

class SyscallDispatcher::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    SysBrkHandler sys_brk_handler_;
};

std::uint64_t SyscallDispatcher::dispatch(Context context,
                                          std::uint64_t syscall_id,
                                          std::array<std::uint64_t, 6> args) {
    switch (syscall_id) {
        case SYS_brk:
            return reinterpret_cast<std::uint64_t>(
                this->impl_->sys_brk_handler_.handle_brk(
                    context, reinterpret_cast<void*>(args[0])));
        default:
            return -ENOSYS;
    }

    // Unreachable.
    std::abort();
}
}  // namespace syscall
}  // namespace vlinux
