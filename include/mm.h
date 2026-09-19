#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <outcome/config.hpp>
#include <outcome/outcome.hpp>
#include <vector>

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vlinux {
const std::size_t PAGE_SIZE = 4096;

// i.e struct page.
struct PhysicalPageDescriptor {
    std::uint64_t start_addr;
    std::int32_t refcount;
    std::uint64_t flags;
};

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
    // Va refers to virtual address of guest process.
    // Pa refers to physical address of vm, i.e. virtual address of the host.

    // Both va and pa must be aligned to PAGE_SIZE.
    outcome::result<void> map(std::uint64_t va, std::uint64_t pa);
    outcome::result<void> unmap(std::uint64_t va);

    // Va needn't be aligned to PAGE_SIZE.
    std::optional<std::uint64_t> va_to_pa(std::uint64_t va);

private:
    std::map<std::uint64_t, std::uint64_t> map_;
};

struct VirtualMemoryAddressSpace;

// i.e vm_area_struct.
struct VirtualMemoryArea {
    enum class Perm {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
        Execute = 1 << 2,
    };

    std::uint64_t start, end;
    std::uint64_t perm;
    VirtualMemoryAddressSpace* address_space;
};

// i.e mm_struct.
struct VirtualMemoryAddressSpace {
    std::vector<std::shared_ptr<VirtualMemoryArea>> vm_areas;
};
}  // namespace vlinux
