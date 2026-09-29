#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace vlinux {
namespace syscall {
class SyscallDispatcher {
public:
    SyscallDispatcher() = default;
    ~SyscallDispatcher() = default;

public:
    std::uint64_t dispatch(std::uint64_t syscall_id,
                           std::array<std::uint64_t, 6> args);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace syscall
}  // namespace vlinux
