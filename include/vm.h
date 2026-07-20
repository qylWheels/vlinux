#pragma once

#include <filesystem>

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
    void init();
    void load(const std::filesystem::path& path);
    void run();
};
}  // namespace vlinux
