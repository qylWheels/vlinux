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

class PageTable {
public:
    PageTable();
    ~PageTable();

public:
    // va and pa must be aligned to PAGE_SIZE.
    void map(std::uint64_t va, std::uint64_t pa);
    void unmap(std::uint64_t va);

private:
    std::map<std::uint64_t, std::uint64_t> map_;
};
}  // namespace vlinux
