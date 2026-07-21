#pragma once

#include <unicorn/unicorn.h>

#include <cstdint>
#include <filesystem>

#include "mm.h"

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
    void load(const std::filesystem::path& path);
    void run();

private:
    std::uint64_t entrypoint_;
    PhysicalPageAllocator ppa_;
    uc_engine* engine_;
};
}  // namespace vlinux
