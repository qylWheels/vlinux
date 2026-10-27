#include "syscall_handler.h"

#include <bits/types/struct_iovec.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unicorn/unicorn.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <outcome/result.hpp>
#include <outcome/try.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "mm.h"
#include "vfs.h"

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

namespace helpers {
bool check_fd(Task& task, int fd) {
    return fd >= 0 && fd < task.files.size() && task.files[fd].has_value();
}

std::variant<std::shared_ptr<IINode>, int> retrieve_parent_of_path(
    Task& task, MountInfo& mount_info, int dirfd, std::string_view pathname) {
    // Get inode:
    // 1. referred by dirfd if pathname is relative.
    // 2. referred by "/" if pathname is absolute.
    // 3. referred by AT_FDCWD if pathname is relative.
    std::optional<std::shared_ptr<File>> cwd_file_opt = std::nullopt;
    std::optional<std::shared_ptr<IINode>> cwd_inode_opt = std::nullopt;
    if (pathname[0] != '/') {  // Relative pathname.
        if (dirfd == AT_FDCWD) {
            cwd_file_opt = task.cwd;
            cwd_inode_opt = task.cwd->inode();
        } else {
            cwd_file_opt = task.files[dirfd];
            if (!cwd_file_opt.has_value()) {
                return -EBADF;
            }
            cwd_inode_opt = cwd_file_opt.value()->inode();
        }
    } else {
        auto root_inode = mount_info.curr_mount_of("/");
        assert(root_inode.has_value());
        cwd_inode_opt = root_inode.value();
    }
    assert(cwd_inode_opt.has_value());
    auto cwd_inode = cwd_inode_opt.value();
    if (cwd_inode->type() != IINode::Type::Directory) {
        return -ENOTDIR;
    }

    // Lookup parent inode of target file.
    std::filesystem::path path(pathname);
    std::filesystem::path curr_path =
        pathname[0] == '/' ? "" : cwd_file_opt.value()->path();
    auto curr_inode = cwd_inode;
    for (auto component : path.parent_path()) {
        curr_path /= component;
        auto mount_inode = mount_info.curr_mount_of(curr_path);
        if (mount_inode.has_value()) {
            curr_inode = mount_inode.value();
        } else {
            auto inode_result = curr_inode->lookup(component);
            if (!inode_result.has_value()) {
                return -ENOENT;
            }
            curr_inode = inode_result.value();
        }
        if (curr_inode->type() != IINode::Type::Directory) {
            return -ENOTDIR;
        }
    }

    return curr_inode;
}
}  // namespace helpers

class SysMmapHandler {
public:
    SysMmapHandler() = default;
    ~SysMmapHandler() = default;
    SysMmapHandler(const SysMmapHandler&) = delete;
    SysMmapHandler& operator=(const SysMmapHandler&) = delete;
    SysMmapHandler(SysMmapHandler&&) = delete;
    SysMmapHandler& operator=(SysMmapHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context ctx,
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
    static std::uint64_t handle(SyscallHandler::Context ctx,
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
    static std::uint64_t handle(SyscallHandler::Context ctx,
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
    static void* expand(SyscallHandler::Context context,
                        void* addr) {  // Check if the address is valid.
        auto task = context.task;

        std::uint64_t page_cnt = calc_unmap_page_cnt(
            task->address_space->brk, reinterpret_cast<uint64_t>(addr),
            mm::PAGE_SIZE);

        // Allocate pages eagerly, we wouldn't implement lazy allocation now.
        std::vector<std::shared_ptr<mm::PhysicalPageDescriptor>> pdescs;
        std::vector<std::shared_ptr<mm::VirtualPageDescriptor>> vdescs;
        // Cleanup function.
        auto cleanup = [&task, &pdescs, &vdescs, &context]() {
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

    static void* shrink(SyscallHandler::Context context, void* addr) {
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
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        void* addr = reinterpret_cast<void*>(args[0]);

        auto task = context.task;
        if (reinterpret_cast<uint64_t>(addr) < task->address_space->start_brk) {
            return task->address_space->brk;
        }

        auto brk = task->address_space->brk;
        if (reinterpret_cast<uint64_t>(addr) > brk) {
            return reinterpret_cast<std::uint64_t>(
                SysBrkHandler::expand(context, addr));
        } else if (reinterpret_cast<uint64_t>(addr) < brk) {
            return reinterpret_cast<std::uint64_t>(
                SysBrkHandler::shrink(context, addr));
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
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        auto flags = args[0];
        auto stack_addr = args[1];
        auto parent_tid = args[2];
        auto child_tid = args[3];
        auto tls = args[4];

        if (flags != 0) return -EINVAL;  // Only support flags = 0 for now.
        if (stack_addr != 0)
            return -EINVAL;  // Only support stack_addr = 0 for now.
        if (parent_tid != 0)
            return -EINVAL;  // Only support parent_tid = 0 for now.
        if (child_tid != 0)
            return -EINVAL;            // Only support child_tid = 0 for now.
        if (tls != 0) return -EINVAL;  // Only support tls = 0 for now.

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

        // Setup task context to ensure it is same as parent process.
        // Except:
        // - RAX, it must be set to 0 in child process.
        // - RIP, it must be set to current RIP + 2. (2 is the size of syscall
        // instruction)
        err = ::uc_context_save(context.uc, ctx);
        if (err != UC_ERR_OK) {
            return -ENOMEM;
        }
        std::uint64_t zero = 0;
        err = ::uc_context_reg_write(ctx, UC_X86_REG_RAX, &zero);
        if (err != UC_ERR_OK) {
            return -ENOMEM;
        }
        std::uint64_t rip = 0;
        err = ::uc_context_reg_read(ctx, UC_X86_REG_RIP, &rip);
        if (err != UC_ERR_OK) {
            return -ENOMEM;
        }
        rip += 2;
        err = ::uc_context_reg_write(ctx, UC_X86_REG_RIP, &rip);
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
        // No need to set seperate stack_bottom and stack_top for now,
        // just keep it same as parent process.
        task->stack_bottom = parent->stack_bottom;
        task->stack_top = parent->stack_top;
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

            // Copy the content of the parent's page.
            auto parent_ppage = parent->page_table->vdesc_to_pdesc(vdesc);
            if (parent_ppage.has_value()) {
                std::vector<std::uint8_t> page_data(mm::PAGE_SIZE);
                err =
                    ::uc_mem_read(context.uc, parent_ppage.value()->start_addr,
                                  page_data.data(), mm::PAGE_SIZE);
                if (err != UC_ERR_OK) {
                    cleanup();
                    return -ENOMEM;
                }
                err =
                    ::uc_mem_write(context.uc, ppage_result.value()->start_addr,
                                   page_data.data(), mm::PAGE_SIZE);
                if (err != UC_ERR_OK) {
                    cleanup();
                    return -ENOMEM;
                }
            }
        }
        task->cwd = parent->cwd;
        // TODO: Copy files from parent except files set O_CLOEXEC.
        task->syscalls = {};

        // Add task to scheduler. clone() runs inside the scheduling loop, so
        // the running scheduler must accept the new task.
        auto add_result = context.scheduler->add_task_running(task);
        if (!add_result) {
            cleanup();
            return -ENOMEM;
        }

        return task->pid;
    }
};

class SysForkHandler {
public:
    SysForkHandler() = default;
    ~SysForkHandler() = default;
    SysForkHandler(const SysForkHandler&) = delete;
    SysForkHandler& operator=(const SysForkHandler&) = delete;
    SysForkHandler(SysForkHandler&&) = delete;
    SysForkHandler& operator=(SysForkHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        return SysCloneHandler::handle(context, {0, 0, 0, 0, 0, 0});
    }
};

class SysExitHandler {
public:
    SysExitHandler() = default;
    ~SysExitHandler() = default;
    SysExitHandler(const SysExitHandler&) = delete;
    SysExitHandler& operator=(const SysExitHandler&) = delete;
    SysExitHandler(SysExitHandler&&) = delete;
    SysExitHandler& operator=(SysExitHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        auto task = context.scheduler->current_task();
        // Exit() is not allowed for idle and init process.
        if (task->pid == 0 || task->pid == 1) {
            std::abort();
        }

        // Stop unicorn engine.
        ::uc_emu_stop(context.uc);

        // Remove it from scheduler.
        auto remove_result = context.scheduler->remove_task_running(task);
        if (!remove_result) {
            std::abort();
        }

        // Free physical pages.
        for (auto& vdesc : task->address_space->vpages) {
            auto ppage = task->page_table->vdesc_to_pdesc(vdesc);
            if (ppage.has_value()) {
                (void)context.ppa->free(ppage.value());
            }
        }

        // Set exit status.
        task->exit_status = args[0];

        // Set task state to Zombie.
        task->state = Task::State::Zombie;

        // Entrust child processes to init.
        auto init = std::find_if(
            context.tasks->begin(), context.tasks->end(),
            [](const std::shared_ptr<Task>& t) { return t->pid == 1; });
        if (init == context.tasks->end()) {
            std::abort();  // Unreachable.
        }
        for (auto& child : task->children) {
            child->parent = *init;
            (*init)->children.push_back(child);
        }

        // TODO: Wake up parent process who called wait().

        // Actually, the task never returns.
        return 0;
    }
};

class SysGetpidHandler {
public:
    SysGetpidHandler() = default;
    ~SysGetpidHandler() = default;
    SysGetpidHandler(const SysGetpidHandler&) = delete;
    SysGetpidHandler& operator=(const SysGetpidHandler&) = delete;
    SysGetpidHandler(SysGetpidHandler&&) = delete;
    SysGetpidHandler& operator=(SysGetpidHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        return context.scheduler->current_task()->pid;
    }
};

class SysGetppidHandler {
public:
    SysGetppidHandler() = default;
    ~SysGetppidHandler() = default;
    SysGetppidHandler(const SysGetppidHandler&) = delete;
    SysGetppidHandler& operator=(const SysGetppidHandler&) = delete;
    SysGetppidHandler(SysGetppidHandler&&) = delete;
    SysGetppidHandler& operator=(SysGetppidHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        return context.scheduler->current_task()->parent.lock()->pid;
    }
};

class SysOpenAtHandler {
public:
    SysOpenAtHandler() = default;
    ~SysOpenAtHandler() = default;
    SysOpenAtHandler(const SysOpenAtHandler&) = delete;
    SysOpenAtHandler& operator=(const SysOpenAtHandler&) = delete;
    SysOpenAtHandler(SysOpenAtHandler&&) = delete;
    SysOpenAtHandler& operator=(SysOpenAtHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int dirfd = args[0];
        auto pathname_addr = args[1];
        int flags = args[2];
        auto mode = args[3];

        auto task = context.scheduler->current_task();

        // Read pathname.
        std::string pathname;
        for (int i = 0; i < PATH_MAX; ++i) {
            char c;
            if (::uc_vmem_read(context.uc, pathname_addr + i, UC_PROT_READ, &c,
                               1) != UC_ERR_OK) {
                return -EFAULT;
            }
            if (c == '\0') {
                break;
            }
            pathname += c;
        }

        // Only check dirfd when pathname is relative.
        if (pathname[0] != '/') {
            // Check dirfd.
            if ((dirfd != AT_FDCWD) &&
                (dirfd < 0 || dirfd >= task->files.size()))
                return -EBADF;
        }

        // We DON'T need to check flags for now.

        // TODO: Check mode (only for O_CREAT flag).

        auto parent_inode_of_path_result = helpers::retrieve_parent_of_path(
            *task, *context.mount_info, dirfd, pathname);
        if (std::holds_alternative<int>(parent_inode_of_path_result)) {
            return std::get<int>(parent_inode_of_path_result);
        }
        auto parent_inode_of_path =
            std::get<std::shared_ptr<IINode>>(parent_inode_of_path_result);
        auto path = std::filesystem::path{pathname};

        // Plain open.
        auto plain_open = [&]() -> std::variant<int, std::shared_ptr<File>> {
            outcome::result<std::shared_ptr<IINode>> inode_result =
                std::errc::io_error;

            // path.filename() is empty when pathname is "/", which is
            // already opened and stored in curr_inode.
            if (path.filename().empty()) {
                inode_result = parent_inode_of_path;
            } else {
                inode_result = parent_inode_of_path->lookup(path.filename());
            }

            // Check inode_result.
            if (!inode_result.has_value()) {
                return -ENOENT;
            }

            // Writing to a directory is not allowed.
            if (inode_result.value()->type() == IINode::Type::Directory &&
                ((flags & O_WRONLY) || (flags & O_RDWR))) {
                return -EISDIR;
            }

            auto inode = inode_result.value();
            auto file = std::make_shared<File>(inode, context.uc);
            auto result = file->get();
            if (!result.has_value()) {
                return -EIO;
            }
            result = file->set_flags(flags);
            if (!result.has_value()) {
                return -EIO;
            }
            result = file->set_offset(0);
            if (!result.has_value()) {
                return -EIO;
            }
            result = inode->inc_mem_refcount();
            if (!result.has_value()) {
                return -EIO;
            }
            return file;
        };

        // Create and open file.
        auto create_and_open =
            [&]() -> std::variant<int, std::shared_ptr<File>> {
            // It is an error if `pathname` is exist and O_CREAT is set.
            auto peek_inode_opt = parent_inode_of_path->lookup(path.filename());
            if (peek_inode_opt.has_value()) {
                return -EEXIST;
            }

            auto sb = parent_inode_of_path->super_block();
            auto inode_result =
                sb.lock()->alloc_inode(sb.lock(), IINode::Type::File);
            if (!inode_result.has_value()) return -ENOMEM;
            auto inode = inode_result.value();

            auto curr_inode_data = parent_inode_of_path->data().value();

            // curr_inode must be a directory. link the new file into it
            // so that the lookup() in plain_open() can find it below.
            if (!std::holds_alternative<IINode::DirectoryFileContent>(
                    *curr_inode_data)) {
                return -ENOTDIR;
            }
            auto& dir_entries =
                std::get<IINode::DirectoryFileContent>(*curr_inode_data);
            dir_entries.push_back({path.filename(), inode});

            return plain_open();
        };

        auto file_result = (flags & O_CREAT) ? create_and_open() : plain_open();
        if (std::holds_alternative<int>(file_result)) {
            return std::get<int>(file_result);
        }
        auto file = std::get<std::shared_ptr<File>>(file_result);

        // Find smallest unused file descriptor.
        int fd = 0;
        if (std::all_of(
                task->files.begin(), task->files.end(),
                [](const auto& file_opt) { return file_opt.has_value(); })) {
            task->files.push_back(file);
            fd = task->files.size() - 1;
        } else {
            for (const auto& file_opt : task->files) {
                if (file_opt.has_value()) {
                    ++fd;
                }
            }
            task->files[fd] = file;
        }

        return fd;
    }
};

class SysOpenHandler {
public:
    SysOpenHandler() = default;
    ~SysOpenHandler() = default;
    SysOpenHandler(const SysOpenHandler&) = delete;
    SysOpenHandler& operator=(const SysOpenHandler&) = delete;
    SysOpenHandler(SysOpenHandler&&) = delete;
    SysOpenHandler& operator=(SysOpenHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        std::array<std::uint64_t, 6> new_args{
            static_cast<std::uint64_t>(AT_FDCWD), args[0], args[1], args[2]};
        return SysOpenAtHandler::handle(context, new_args);
    }
};

class SysCloseHandler {
private:
    SysCloseHandler() = default;
    ~SysCloseHandler() = default;
    SysCloseHandler(const SysCloseHandler&) = delete;
    SysCloseHandler& operator=(const SysCloseHandler&) = delete;
    SysCloseHandler(SysCloseHandler&&) = delete;
    SysCloseHandler& operator=(SysCloseHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }

        // Decrement refcount.
        if (!task->files[fd].value()->inode()->dec_mem_refcount()) {
            return -EIO;
        }
        if (!task->files[fd].value()->put()) {
            return -EIO;
        }

        // Close file.
        task->files[fd] = std::nullopt;

        return 0;
    }
};

struct PreadvPwritevCommon {
    enum class Type { Preadv, Pwritev };

    static std::variant<std::vector<IoVector>, int> pre_work(
        SyscallHandler::Context context, int fd, std::uint64_t iov_addr,
        int iovcnt, std::uint32_t pos_low, std::uint32_t pos_high, Type type) {
        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }

        // Check permission.
        if (type == Type::Preadv &&
            ((task->files[fd].value()->flags() & O_ACCMODE) == O_WRONLY)) {
            return -EBADF;
        }
        if (type == Type::Pwritev &&
            ((task->files[fd].value()->flags() & O_ACCMODE) == O_RDONLY)) {
            return -EBADF;
        }

        // Check iovcnt.
        if (iovcnt < 0) return -EINVAL;

        // Read and check iovs.
        std::vector<iovec> iovs(iovcnt);
        for (int i = 0; i < iovcnt; ++i) {
            auto err = ::uc_vmem_read(context.uc, iov_addr + i * sizeof(iovec),
                                      UC_PROT_READ, &iovs[i], sizeof(iovec));
            if (err != 0) {
                return -EFAULT;
            }
        }

        // Transform linux iov to our iov.
        auto transform_view =
            iovs | std::views::transform([](const auto& iov) {
                return IoVector{reinterpret_cast<std::uint64_t>(iov.iov_base),
                                iov.iov_len};
            });
        std::vector<IoVector> iovs2(transform_view.begin(),
                                    transform_view.end());

        return iovs2;
    }
};

class SysPreadvHandler {
private:
    SysPreadvHandler() = default;
    ~SysPreadvHandler() = default;
    SysPreadvHandler(const SysPreadvHandler&) = delete;
    SysPreadvHandler& operator=(const SysPreadvHandler&) = delete;
    SysPreadvHandler(SysPreadvHandler&&) = delete;
    SysPreadvHandler& operator=(SysPreadvHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);
        std::uint64_t iov_addr = args[1];
        int iovcnt = static_cast<int>(args[2]);
        std::uint32_t pos_low = static_cast<std::uint32_t>(args[3]);
        std::uint32_t pos_high = static_cast<std::uint32_t>(args[4]);

        auto task = context.scheduler->current_task();

        // Pre-work.
        auto pre_work_result = PreadvPwritevCommon::pre_work(
            context, fd, iov_addr, iovcnt, pos_low, pos_high,
            PreadvPwritevCommon::Type::Preadv);
        if (std::holds_alternative<int>(pre_work_result)) {
            return std::get<int>(pre_work_result);
        }
        auto iovs2 = std::get<std::vector<IoVector>>(pre_work_result);

        // Read from file.
        auto file = task->files[fd].value();
        std::uint64_t offset = static_cast<std::uint64_t>(pos_low) |
                               (static_cast<std::uint64_t>(pos_high) << 32);
        auto ret = file->preadv(iovs2, offset);
        return ret ? ret.value() : -ret.error().value();
    }
};

class SysPwritevHandler {
private:
    SysPwritevHandler() = default;
    ~SysPwritevHandler() = default;
    SysPwritevHandler(const SysPwritevHandler&) = delete;
    SysPwritevHandler& operator=(const SysPwritevHandler&) = delete;
    SysPwritevHandler(SysPwritevHandler&&) = delete;
    SysPwritevHandler& operator=(SysPwritevHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        // Get args.
        int fd = static_cast<int>(args[0]);
        std::uint64_t iov_addr = args[1];
        int iovcnt = static_cast<int>(args[2]);
        std::uint32_t pos_low = static_cast<std::uint32_t>(args[3]);
        std::uint32_t pos_high = static_cast<std::uint32_t>(args[4]);

        auto task = context.scheduler->current_task();

        // Pre-work.
        auto pre_work_result = PreadvPwritevCommon::pre_work(
            context, fd, iov_addr, iovcnt, pos_low, pos_high,
            PreadvPwritevCommon::Type::Pwritev);
        if (std::holds_alternative<int>(pre_work_result)) {
            return std::get<int>(pre_work_result);
        }
        auto iovs2 = std::get<std::vector<IoVector>>(pre_work_result);

        // Write to file.
        auto file = task->files[fd].value();
        std::uint64_t offset = static_cast<std::uint64_t>(pos_low) |
                               (static_cast<std::uint64_t>(pos_high) << 32);
        auto ret = file->pwritev(iovs2, offset);
        return ret ? ret.value() : -ret.error().value();
    }
};

class SysPread64Handler {
private:
    SysPread64Handler() = default;
    ~SysPread64Handler() = default;
    SysPread64Handler(const SysPread64Handler&) = delete;
    SysPread64Handler& operator=(const SysPread64Handler&) = delete;
    SysPread64Handler(SysPread64Handler&&) = delete;
    SysPread64Handler& operator=(SysPread64Handler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);
        std::uint64_t buf = args[1];
        std::uint64_t count = args[2];
        std::int64_t offset = static_cast<std::int64_t>(args[3]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }

        // Check permission.
        if ((task->files[fd].value()->flags() & O_ACCMODE) == O_WRONLY) {
            return -EBADF;
        }

        // Check offset.
        if (offset < 0) {
            return -EINVAL;
        }

        // Read from file.
        auto file = task->files[fd].value();
        std::vector<IoVector> iov{{buf, count}};
        auto ret = file->preadv(iov, offset);
        return ret ? ret.value() : -ret.error().value();
    }
};

class SysPwrite64Handler {
private:
    SysPwrite64Handler() = default;
    ~SysPwrite64Handler() = default;
    SysPwrite64Handler(const SysPwrite64Handler&) = delete;
    SysPwrite64Handler& operator=(const SysPwrite64Handler&) = delete;
    SysPwrite64Handler(SysPwrite64Handler&&) = delete;
    SysPwrite64Handler& operator=(SysPwrite64Handler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);
        std::uint64_t buf = args[1];
        std::uint64_t count = args[2];
        std::int64_t offset = static_cast<std::int64_t>(args[3]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }

        // Check permission.
        if ((task->files[fd].value()->flags() & O_ACCMODE) == O_RDONLY) {
            return -EBADF;
        }

        // Check offset.
        if (offset < 0) {
            return -EINVAL;
        }

        // Write to file.
        auto file = task->files[fd].value();
        std::vector<IoVector> iov{{buf, count}};
        auto ret = file->pwritev(iov, offset);
        return ret ? ret.value() : -ret.error().value();
    }
};

class SysReadvHandler {
private:
    SysReadvHandler() = default;
    ~SysReadvHandler() = default;
    SysReadvHandler(const SysReadvHandler&) = delete;
    SysReadvHandler& operator=(const SysReadvHandler&) = delete;
    SysReadvHandler(SysReadvHandler&&) = delete;
    SysReadvHandler& operator=(SysReadvHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }
        auto file = task->files[fd].value();

        // Read from the current file offset.
        std::uint64_t offset = file->offset();
        auto new_args = args;
        new_args[3] = static_cast<std::uint32_t>(offset);
        new_args[4] = static_cast<std::uint32_t>(offset >> 32);
        auto ret = SysPreadvHandler::handle(context, new_args);
        if (static_cast<std::int64_t>(ret) < 0) {
            return ret;
        }

        // Advance the file offset.
        auto result = file->set_offset(offset + ret);
        if (!result) {
            return -result.error().value();
        }
        return ret;
    }
};

class SysWritevHandler {
private:
    SysWritevHandler() = default;
    ~SysWritevHandler() = default;
    SysWritevHandler(const SysWritevHandler&) = delete;
    SysWritevHandler& operator=(const SysWritevHandler&) = delete;
    SysWritevHandler(SysWritevHandler&&) = delete;
    SysWritevHandler& operator=(SysWritevHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }
        auto file = task->files[fd].value();

        // Write to the current file offset.
        std::uint64_t offset = file->offset();
        auto new_args = args;
        new_args[3] = static_cast<std::uint32_t>(offset);
        new_args[4] = static_cast<std::uint32_t>(offset >> 32);
        auto ret = SysPwritevHandler::handle(context, new_args);
        if (static_cast<std::int64_t>(ret) < 0) {
            return ret;
        }

        // Advance the file offset.
        auto result = file->set_offset(offset + ret);
        if (!result) {
            return -result.error().value();
        }
        return ret;
    }
};

class SysReadHandler {
private:
    SysReadHandler() = default;
    ~SysReadHandler() = default;
    SysReadHandler(const SysReadHandler&) = delete;
    SysReadHandler& operator=(const SysReadHandler&) = delete;
    SysReadHandler(SysReadHandler&&) = delete;
    SysReadHandler& operator=(SysReadHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }
        auto file = task->files[fd].value();

        // Read from the current file offset.
        std::uint64_t offset = file->offset();
        std::array<std::uint64_t, 6> new_args{args[0], args[1], args[2],
                                              offset};
        auto ret = SysPread64Handler::handle(context, new_args);
        if (static_cast<std::int64_t>(ret) < 0) {
            return ret;
        }

        // Advance the file offset.
        auto result = file->set_offset(offset + ret);
        if (!result) {
            return -result.error().value();
        }
        return ret;
    }
};

class SysWriteHandler {
private:
    SysWriteHandler() = default;
    ~SysWriteHandler() = default;
    SysWriteHandler(const SysWriteHandler&) = delete;
    SysWriteHandler& operator=(const SysWriteHandler&) = delete;
    SysWriteHandler(SysWriteHandler&&) = delete;
    SysWriteHandler& operator=(SysWriteHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }
        auto file = task->files[fd].value();

        // Write to the current file offset.
        std::uint64_t offset = file->offset();
        std::array<std::uint64_t, 6> new_args{args[0], args[1], args[2],
                                              offset};
        auto ret = SysPwrite64Handler::handle(context, new_args);
        if (static_cast<std::int64_t>(ret) < 0) {
            return ret;
        }

        // Advance the file offset.
        auto result = file->set_offset(offset + ret);
        if (!result) {
            return -result.error().value();
        }
        return ret;
    }
};

class SysLseekHandler {
private:
    SysLseekHandler() = default;
    ~SysLseekHandler() = default;
    SysLseekHandler(const SysLseekHandler&) = delete;
    SysLseekHandler& operator=(const SysLseekHandler&) = delete;
    SysLseekHandler(SysLseekHandler&&) = delete;
    SysLseekHandler& operator=(SysLseekHandler&&) = delete;

public:
    static std::uint64_t handle(SyscallHandler::Context context,
                                std::array<std::uint64_t, 6> args) {
        int fd = static_cast<int>(args[0]);
        std::int64_t offset = static_cast<std::int64_t>(args[1]);
        int whence = static_cast<int>(args[2]);

        auto task = context.scheduler->current_task();

        // Check fd.
        if (fd < 0 || fd >= task->files.size() ||
            !task->files[fd].has_value()) {
            return -EBADF;
        }

        // Check whence.
        if (whence < 0 || whence > 2) {
            return -EINVAL;
        }

        // Calculate and check offset.
        auto file = task->files[fd].value();
        std::int64_t result_offset;
        switch (whence) {
            case SEEK_SET: {
                if (offset < 0) {
                    return -EINVAL;
                }
                result_offset = offset;
                break;
            }
            case SEEK_CUR: {
                auto curr_offset = static_cast<std::int64_t>(file->offset());
                result_offset = curr_offset + offset;
                if (result_offset < 0) {
                    return -EINVAL;
                }
                break;
            }
            case SEEK_END: {
                auto curr_offset =
                    static_cast<std::int64_t>(file->inode()->size());
                result_offset = curr_offset + offset;
                if (result_offset < 0) {
                    return -EINVAL;
                }
                break;
            }
            default:
                std::abort();  // Unreachable.
        }

        // Set offset.
        auto ret = file->set_offset(result_offset);
        if (!ret) {
            return -ret.error().value();
        }

        return result_offset;
    }
};

class SyscallHandler::Impl {
public:
    Impl() = default;
    ~Impl() = default;

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
            ret = SysMmapHandler::handle(context, args);
            break;
        case SYS_munmap:
            ret = SysMunmapHandler::handle(context, args);
            break;
        case SYS_mprotect:
            ret = SysMprotectHandler::handle(context, args);
            break;
        case SYS_brk:
            ret = SysBrkHandler::handle(context, args);
            break;
        case SYS_clone:
            ret = SysCloneHandler::handle(context, args);
            break;
        case SYS_fork:
            ret = SysForkHandler::handle(context, args);
            break;
        case SYS_exit:
            ret = SysExitHandler::handle(context, args);
            break;
        case SYS_getpid:
            ret = SysGetpidHandler::handle(context, args);
            break;
        case SYS_getppid:
            ret = SysGetppidHandler::handle(context, args);
            break;
        case SYS_open:
            ret = SysOpenHandler::handle(context, args);
            break;
        case SYS_openat:
            ret = SysOpenAtHandler::handle(context, args);
            break;
        case SYS_close:
            ret = SysCloseHandler::handle(context, args);
            break;
        case SYS_lseek:
            ret = SysLseekHandler::handle(context, args);
            break;
        case SYS_preadv:
            ret = SysPreadvHandler::handle(context, args);
            break;
        case SYS_pwritev:
            ret = SysPwritevHandler::handle(context, args);
            break;
        case SYS_read:
            ret = SysReadHandler::handle(context, args);
            break;
        case SYS_write:
            ret = SysWriteHandler::handle(context, args);
            break;
        case SYS_readv:
            ret = SysReadvHandler::handle(context, args);
            break;
        case SYS_writev:
            ret = SysWritevHandler::handle(context, args);
            break;
        case SYS_pread64:
            ret = SysPread64Handler::handle(context, args);
            break;
        case SYS_pwrite64:
            ret = SysPwrite64Handler::handle(context, args);
            break;
        default:
            ret = 0;  // Pretend that we have handled it.
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