#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include "mm.h"
#include "task.h"

namespace vlinux {
namespace syscall {
class SyscallHandler {
public:
    SyscallHandler();
    ~SyscallHandler();

public:
    struct Context {
        uc_engine *uc;
        std::shared_ptr<Scheduler> scheduler;
        std::shared_ptr<Task> task;
        std::shared_ptr<mm::PhysicalPageAllocator> ppa;
    };

public:
    std::uint64_t dispatch(Context context, std::uint64_t syscall_id,
                           std::array<std::uint64_t, 6> args);
    outcome::result<void> add_syscall_hook(
        std::function<void(std::uint64_t syscall_id,
                           std::array<std::uint64_t, 6> args,
                           std::uint64_t ret)>
            hook);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace syscall
}  // namespace vlinux
