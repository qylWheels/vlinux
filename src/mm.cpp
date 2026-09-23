#include "mm.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <system_error>

namespace vlinux {
namespace mm {
PhysicalPageAllocator::PhysicalPageAllocator() {
    // [0, 0x1000): Reserved.
    // [0x1000, 0x2000): idle task.
    // [0x2000, 0x3000): init task.
    // [0x3000, 0x2000,0000): Free pages.
    // [0x20000000, max): Reserved.
    for (std::uint64_t addr = 0x3000; addr < 0x3000 + kMaxPageCount * PAGE_SIZE;
         addr += PAGE_SIZE) {
        auto desc =
            std::make_shared<PhysicalPageDescriptor>(addr, PAGE_SIZE, 0, 0);
        this->free_pages_.push_back(desc);
    }
}

PhysicalPageAllocator::~PhysicalPageAllocator() = default;

outcome::result<std::shared_ptr<PhysicalPageDescriptor>>
PhysicalPageAllocator::alloc() {
    if (this->free_pages_.empty()) {
        return std::errc::not_enough_memory;
    }

    // Get a page.
    auto page = this->free_pages_.front();
    this->free_pages_.pop_front();

    // Increment refcount.
    page->refcount++;

    // Add to alloced_pages.
    this->alloced_pages_.insert(page);

    return outcome::success(page);
}

void PhysicalPageAllocator::free(std::shared_ptr<PhysicalPageDescriptor> desc) {
    // Remove from alloced_pages.
    this->alloced_pages_.erase(desc);

    // Decrement refcount.
    desc->refcount--;

    // Add to free_pages.
    this->free_pages_.push_front(desc);
}

PageTable::PageTable() = default;

PageTable::~PageTable() = default;

outcome::result<void> PageTable::map(std::uint64_t va, std::uint64_t pa) {
    if ((va % PAGE_SIZE) || (pa % PAGE_SIZE)) {
        return std::errc::invalid_argument;
    }

    auto result = this->map_.insert({va, pa});
    if (!result.second) {
        // va already mapped.
        return std::errc::address_in_use;
    }

    return outcome::success();
}

outcome::result<void> PageTable::unmap(std::uint64_t va) {
    if (va % PAGE_SIZE != 0) {
        return std::errc::invalid_argument;
    }

    this->map_.erase(va);

    return outcome::success();
}

std::optional<std::uint64_t> PageTable::va_to_pa(std::uint64_t va) {
    std::uint64_t aligned_va = va & ~(PAGE_SIZE - 1);
    auto it = this->map_.find(aligned_va);
    if (it == this->map_.end()) {
        return std::nullopt;
    }
    return it->second + (va - aligned_va);
}
}  // namespace mm
}  // namespace vlinux
