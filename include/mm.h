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
#include <outcome/result.hpp>
#include <set>

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
    // [0x1000, 0x2000): idle task. Desc not in this allocator.
    // [0x2000, 0x3000): init task. Desc not in this allocator.
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
    // Return descriptor of physical page.
    outcome::result<std::shared_ptr<PhysicalPageDescriptor>> alloc();

    // Increment refcount.
    outcome::result<std::shared_ptr<PhysicalPageDescriptor>> get_page(
        std::shared_ptr<PhysicalPageDescriptor> desc);

    // Desc must be page descriptor allocated by alloc().
    outcome::result<void> free(std::shared_ptr<PhysicalPageDescriptor> desc);

private:
    uc_engine* uc_;
    std::list<std::shared_ptr<PhysicalPageDescriptor>> free_pages_;
    std::set<std::shared_ptr<PhysicalPageDescriptor>> alloced_pages_;
};

struct VirtualPageDescriptor {
    std::uint64_t start_addr;
    std::size_t len;
    std::uint64_t perm;  // Use UC_PROT_*.
};

class PageTable {
public:
    PageTable();
    ~PageTable();

public:
    // Va refers to virtual address of guest process.
    // Pa refers to physical address of vm, i.e. virtual address of the host.
    outcome::result<void> map(std::shared_ptr<VirtualPageDescriptor> va_desc,
                              std::shared_ptr<PhysicalPageDescriptor> pa_desc);
    outcome::result<void> unmap(std::shared_ptr<VirtualPageDescriptor> va_desc);

    // Va needn't be aligned to PAGE_SIZE.
    // Return std::nullopt if va is not mapped.
    std::optional<std::uint64_t> va_to_pa(std::uint64_t va);

    // Va needn't be aligned to PAGE_SIZE.
    // Return std::nullopt if va is not mapped.
    std::optional<std::shared_ptr<VirtualPageDescriptor>> va_to_desc(
        std::uint64_t va);

private:
    std::map<std::shared_ptr<VirtualPageDescriptor>,
             std::shared_ptr<PhysicalPageDescriptor>>
        map_;
};

struct VirtualMemoryAddressSpace;

// i.e vm_area_struct.
struct VirtualMemoryArea {
    std::uint64_t start, end;
    std::uint64_t perm;  // Use UC_PROT_*.
    std::set<std::shared_ptr<VirtualPageDescriptor>> vdescs;
    VirtualMemoryAddressSpace* address_space;

    bool operator<(const VirtualMemoryArea& other) const {
        return this->start < other.start;
    }
};

// i.e mm_struct.
struct VirtualMemoryAddressSpace {
    std::set<VirtualMemoryArea> vm_areas;
    std::uint64_t start_brk;
    std::uint64_t brk;
};
}  // namespace mm
}  // namespace vlinux
