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

PhysicalPageAllocator::~PhysicalPageAllocator(){
    // Do not unmap anything here. The allocator may outlive uc_close(), and
    // the engine frees its own memory on close. Unmapping a page here would
    // also make the engine drop its physical-address keyed translation
    // caches (see mm.h).
};

outcome::result<std::shared_ptr<PhysicalPageDescriptor>>
PhysicalPageAllocator::alloc() {
    if (this->free_pages_.empty()) {
        return std::errc::not_enough_memory;
    }

    uc_err err;

    // Get a page descriptor.
    auto page = this->free_pages_.front();
    this->free_pages_.pop_front();

    // Alloc physical page in unicorn on first use. Set permission to
    // UC_PROT_ALL because this is a physical page of guest.
    // A page that was allocated before is already mapped, and it stays mapped
    // until the allocator is destroyed.
    if (this->mapped_pages_.find(page) == this->mapped_pages_.end()) {
        err = ::uc_mem_map(this->uc_, page->start_addr, PAGE_SIZE, UC_PROT_ALL);
        if (err != UC_ERR_OK) {
            this->free_pages_.push_front(page);
            return std::errc::invalid_argument;
        }
        this->mapped_pages_.insert(page);
    }

    // Set page status.
    page->refcount = 1;

    // Add to alloced_pages.
    this->alloced_pages_.insert(page);

    return outcome::success(page);
}

outcome::result<std::shared_ptr<PhysicalPageDescriptor>>
PhysicalPageAllocator::get_page(std::shared_ptr<PhysicalPageDescriptor> desc) {
    if (desc == nullptr) {
        return std::errc::invalid_argument;
    }

    auto it = this->alloced_pages_.find(desc);
    if (it == this->alloced_pages_.end()) {
        return std::errc::invalid_argument;
    }

    // Increment refcount.
    desc->refcount++;

    return outcome::success(desc);
}

outcome::result<void> PhysicalPageAllocator::free(
    std::shared_ptr<PhysicalPageDescriptor> desc) {
    if (desc == nullptr) {
        return std::errc::invalid_argument;
    }

    if (desc->refcount == 0) {
        return std::errc::invalid_argument;
    }

    // Remove from alloced_pages.
    this->alloced_pages_.erase(desc);

    // Decrement refcount.
    desc->refcount--;

    // The page stays mapped in unicorn: unmapping it here would break the
    // engine's physical-address keyed translation caches (see mm.h). It is
    // simply returned to the pool and reused by a later alloc().

    // Add to free_pages.
    this->free_pages_.push_front(desc);

    return outcome::success();
}

PageTable::PageTable() = default;

PageTable::~PageTable() = default;

outcome::result<void> PageTable::map(
    std::shared_ptr<VirtualPageDescriptor> va_desc,
    std::shared_ptr<PhysicalPageDescriptor> pa_desc) {
    if (va_desc == nullptr || pa_desc == nullptr) {
        return std::errc::invalid_argument;
    }

    auto result = this->map_.insert({va_desc, pa_desc});
    if (!result.second) {
        // va already mapped.
        return std::errc::address_in_use;
    }

    return outcome::success();
}

outcome::result<void> PageTable::unmap(
    std::shared_ptr<VirtualPageDescriptor> va_desc) {
    if (va_desc == nullptr) {
        return std::errc::invalid_argument;
    }

    this->map_.erase(va_desc);

    return outcome::success();
}

std::optional<std::uint64_t> PageTable::va_to_pa(std::uint64_t va) {
    for (auto& [va_desc, pa_desc] : this->map_) {
        if (va_desc->start_addr <= va &&
            va < va_desc->start_addr + va_desc->len) {
            return pa_desc->start_addr + (va - va_desc->start_addr);
        }
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<VirtualPageDescriptor>> PageTable::va_to_desc(
    std::uint64_t va) {
    for (const auto& [va_desc, pa_desc] : this->map_) {
        if (va_desc->start_addr <= va &&
            va < va_desc->start_addr + va_desc->len) {
            return va_desc;
        }
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<PhysicalPageDescriptor>>
PageTable::vdesc_to_pdesc(std::shared_ptr<VirtualPageDescriptor> vdesc) {
    auto it = this->map_.find(vdesc);
    if (it == this->map_.end()) {
        return std::nullopt;
    }
    return it->second;
}
}  // namespace mm
}  // namespace vlinux
