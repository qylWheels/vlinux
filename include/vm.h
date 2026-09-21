#pragma once

#include <unicorn/unicorn.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <outcome.hpp>

#include "mm.h"
#include "task.h"

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vlinux {
class VM {
public:
    VM();
    ~VM();
    VM(const VM& other) = delete;
    VM& operator=(const VM& other) = delete;
    VM(VM&& other) = delete;
    VM& operator=(VM&& other) = delete;

public:
    void reset();
    outcome::result<void> load(const std::filesystem::path& path) noexcept;
    outcome::result<void> run() noexcept;

    // Syscalls.
public:
    void exit(int status);

private:
    static void syscall_hook_callback(uc_engine* engine, void* user_data);

private:
    std::uint64_t entrypoint_;
    mm::PhysicalPageAllocator ppa_;
    uc_engine* engine_;
    uc_hook syscall_hook_;
    std::shared_ptr<Task> curr_task_;
};
}  // namespace vlinux
