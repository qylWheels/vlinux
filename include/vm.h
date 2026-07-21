#pragma once

#include <unicorn/unicorn.h>

#include <cstdint>
#include <filesystem>
#include <outcome.hpp>

#include "mm.h"

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
    void run();

private:
    static void syscall_hook_callback(uc_engine* engine, void* user_data);

private:
    std::uint64_t entrypoint_;
    PhysicalPageAllocator ppa_;
    uc_engine* engine_;
    uc_hook syscall_hook_;
};
}  // namespace vlinux
