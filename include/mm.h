#pragma once

#include <unicorn/unicorn.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <outcome/config.hpp>
#include <outcome/outcome.hpp>
#include <set>
#include <vector>

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vlinux {
namespace mm {
const std::size_t PAGE_SIZE = 4096;
const std::uint64_t kIdleTaskCodeRegionStart = 0x1000;
const std::uint64_t kIdleTaskCodeRegionLen = PAGE_SIZE;
const std::uint64_t kInitTaskCodeRegionStart = 0x2000;
const std::uint64_t kInitTaskCodeRegionLen = PAGE_SIZE;

// i.e struct page.
struct PhysicalPageDescriptor {
    std::uint64_t start_addr;
    std::size_t len;
    std::int32_t refcount;
    std::uint64_t flags;

    bool operator<(const PhysicalPageDescriptor& other) const {
        return start_addr < other.start_addr;
    }

    bool operator==(const PhysicalPageDescriptor& other) const {
        return start_addr == other.start_addr;
    }
};

class PhysicalPageAllocator {
    // [0, 0x1000): Reserved.
    // [0x1000, 0x2000): idle task.
    // [0x2000, 0x3000): init task.
    // [0x3000, 0x2000,0000): Free pages.
    // [0x20000000, max): Reserved.

public:
    const std::size_t kMaxPageCount = 128 * 1024;  // 512 MB.

public:
    PhysicalPageAllocator(uc_engine* uc);
    ~PhysicalPageAllocator();
    PhysicalPageAllocator& operator=(const PhysicalPageAllocator&) = delete;
    PhysicalPageAllocator(const PhysicalPageAllocator&) = delete;
    PhysicalPageAllocator(PhysicalPageAllocator&&) = delete;
    PhysicalPageAllocator& operator=(PhysicalPageAllocator&&) = delete;

public:
    // Allocate a physical page of unicorn (i.e. virtual page of host),
    // aligned to PAGE_SIZE.
    // Return pa of unicorn.
    outcome::result<std::shared_ptr<PhysicalPageDescriptor>> get_page(
        std::uint32_t prot);

    // Desc must be page descriptor allocated by get_page().
    void put_page(std::shared_ptr<PhysicalPageDescriptor> desc);

private:
    uc_engine* uc_;
    std::list<std::shared_ptr<PhysicalPageDescriptor>> free_pages_;
    std::set<std::shared_ptr<PhysicalPageDescriptor>> alloced_pages_;
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
    std::vector<VirtualMemoryArea> vm_areas;
};
}  // namespace mm
}  // namespace vlinux
