#include "mm.h"

#include <error.h>
#include <unicorn/unicorn.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <system_error>

namespace vlinux {
namespace mm {
class UnicornGuestPhysicalMemory::Impl {
public:
    uc_engine* uc;
};

UnicornGuestPhysicalMemory::UnicornGuestPhysicalMemory(uc_engine* uc)
    : impl_(std::make_unique<Impl>()) {
    impl_->uc = uc;
}

UnicornGuestPhysicalMemory::~UnicornGuestPhysicalMemory() = default;

outcome::result<std::vector<std::uint8_t>> UnicornGuestPhysicalMemory::read(
    std::size_t addr, std::size_t bytes) {
    std::vector<std::uint8_t> data(bytes);
    auto ret = ::uc_mem_read(impl_->uc, addr, data.data(), bytes);
    if (ret != UC_ERR_OK) {
        return make_error_code(ret);
    }
    return data;
}

outcome::result<std::string> UnicornGuestPhysicalMemory::read_string(
    std::size_t addr) {
    std::string result;
    char c;
    while (true) {
        auto ret = ::uc_mem_read(impl_->uc, addr, &c, 1);
        if (ret != UC_ERR_OK) {
            return make_error_code(ret);
        }
        if (c == '\0') {
            break;
        }
        result += c;
        addr++;
    }
    return result;
}

outcome::result<void> UnicornGuestPhysicalMemory::write(
    std::size_t addr, std::vector<std::uint8_t> data) {
    auto ret = ::uc_mem_write(impl_->uc, addr, data.data(), data.size());
    if (ret != UC_ERR_OK) {
        return make_error_code(ret);
    }
    return outcome::success();
}

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

    // Decrement refcount.
    desc->refcount--;

    // The page stays mapped in unicorn: unmapping it here would break the
    // engine's physical-address keyed translation caches (see mm.h). It is
    // simply returned to the pool and reused by a later alloc().
    // if (desc->refcount == 0) {
    //     (void)::uc_mem_unmap(this->uc_, desc->start_addr, PAGE_SIZE);
    // }

    if (desc->refcount == 0) {
        this->alloced_pages_.erase(desc);
        this->free_pages_.push_front(desc);
    }

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

class UnicornGuestVirtualMemory::Impl {
public:
    uc_engine* uc;
    std::shared_ptr<IGuestPageTable<PageTable::MapType>> page_table;
};

UnicornGuestVirtualMemory::UnicornGuestVirtualMemory(
    uc_engine* uc,
    std::shared_ptr<IGuestPageTable<PageTable::MapType>> page_table)
    : impl_(std::make_unique<Impl>()) {
    impl_->uc = uc;
    impl_->page_table = page_table;
}

UnicornGuestVirtualMemory::~UnicornGuestVirtualMemory() = default;

outcome::result<std::vector<std::uint8_t>> UnicornGuestVirtualMemory::read(
    std::size_t addr, std::size_t bytes) {
    auto pa_opt = impl_->page_table->va_to_pa(addr);
    if (!pa_opt.has_value()) {
        return std::errc::bad_address;
    }
    auto pa = pa_opt.value();
    std::vector<std::uint8_t> data(bytes);
    auto ret = ::uc_mem_read(impl_->uc, pa, data.data(), bytes);
    if (ret != UC_ERR_OK) {
        return make_error_code(ret);
    }
    return data;
}

outcome::result<std::string> UnicornGuestVirtualMemory::read_string(
    std::size_t addr) {
    std::string result;
    char c;
    while (true) {
        auto pa_opt = impl_->page_table->va_to_pa(addr);
        if (!pa_opt.has_value()) {
            return std::errc::bad_address;
        }
        auto pa = pa_opt.value();
        auto ret = ::uc_mem_read(impl_->uc, pa, &c, 1);
        if (ret != UC_ERR_OK) {
            return make_error_code(ret);
        }
        if (c == '\0') {
            break;
        }
        result += c;
        addr++;
    }
    return result;
}

outcome::result<void> UnicornGuestVirtualMemory::write(
    std::size_t addr, std::vector<std::uint8_t> data) {
    auto addr_round_up = (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    auto pagecnt = addr_round_up / PAGE_SIZE;
    std::size_t offset = 0;
    for (auto i = 0; i < pagecnt; i++) {
        auto pa_opt = impl_->page_table->va_to_pa(addr);
        if (!pa_opt.has_value()) {
            return std::errc::bad_address;
        }
        auto pa = pa_opt.value();
        auto ret = ::uc_mem_write(impl_->uc, pa, data.data() + offset,
                                  std::min(PAGE_SIZE, data.size() - offset));
        if (ret != UC_ERR_OK) {
            return make_error_code(ret);
        }
        addr += PAGE_SIZE;
        offset += PAGE_SIZE;
    }
    return outcome::success();
}

outcome::result<void> UnicornGuestVirtualMemory::flush_tlb() {
    uc_err err;
    err = uc_ctl_flush_tlb(impl_->uc);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }
    err = uc_ctl_flush_tb(impl_->uc);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }
    return outcome::success();
}

}  // namespace mm
}  // namespace vlinux
