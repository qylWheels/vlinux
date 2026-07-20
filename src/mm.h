#pragma once

#include <cstdint>
#include <map>

namespace vlinux {
class PhysicalPageAllocator {
public:
    PhysicalPageAllocator();
    ~PhysicalPageAllocator();
    PhysicalPageAllocator& operator=(const PhysicalPageAllocator&) = delete;
    PhysicalPageAllocator(const PhysicalPageAllocator&) = delete;
    PhysicalPageAllocator(PhysicalPageAllocator&&) = delete;
    PhysicalPageAllocator& operator=(PhysicalPageAllocator&&) = delete;

public:
    // Allocate a physical page.
    std::uint64_t alloc();

    // pa must be physical address allocated by alloc().
    void free(std::uint64_t pa);
};
}  // namespace vlinux
