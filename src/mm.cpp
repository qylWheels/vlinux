#include "mm.h"

#include <unicorn/unicorn.h>

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <system_error>

namespace vlinux {
namespace mm {
PhysicalPageAllocator::PhysicalPageAllocator(uc_engine* uc) : uc_(uc) {
    for (std::uint64_t addr = 0x3000; addr < 0x3000 + kMaxPageCount * PAGE_SIZE;
         addr += PAGE_SIZE) {
        auto desc =
            std::make_shared<PhysicalPageDescriptor>(addr, PAGE_SIZE, 0, 0);
        this->free_pages_.push_back(desc);
    }
}

PhysicalPageAllocator::~PhysicalPageAllocator() = default;

outcome::result<std::shared_ptr<PhysicalPageDescriptor>>
PhysicalPageAllocator::alloc(std::uint32_t prot) {
    if (this->free_pages_.empty()) {
        return std::errc::not_enough_memory;
    }

    uc_err err;

    // Get a page descriptor.
    auto page = this->free_pages_.front();
    this->free_pages_.pop_front();

    // Alloc physical page in unicorn.
    err = ::uc_mem_map(this->uc_, page->start_addr, PAGE_SIZE, prot);
    if (err != UC_ERR_OK) {
        return std::errc::invalid_argument;
    }

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

    // Free physical page in unicorn.
    (void)::uc_mem_unmap(this->uc_, desc->start_addr, PAGE_SIZE);

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
