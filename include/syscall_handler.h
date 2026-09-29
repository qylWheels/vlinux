#pragma once

#include <array>
#include <cstdint>
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
        std::shared_ptr<Task> task;
        std::shared_ptr<mm::PhysicalPageAllocator> ppa;
        std::shared_ptr<mm::PageTable> page_table;
    };

public:
    std::uint64_t dispatch(Context context, std::uint64_t syscall_id,
                           std::array<std::uint64_t, 6> args);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace syscall
}  // namespace vlinux
