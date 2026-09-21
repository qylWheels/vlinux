#include "mm.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <system_error>

namespace vlinux {
namespace mm {
PhysicalPageAllocator::PhysicalPageAllocator() = default;

PhysicalPageAllocator::~PhysicalPageAllocator() {
    for (auto pa : this->alloced_pages_) {
        this->free(pa);
    }
}

outcome::result<std::uint64_t> PhysicalPageAllocator::alloc() {
    auto addr = std::aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    if (addr == nullptr) {
        return std::errc::not_enough_memory;
    } else {
        this->alloced_pages_.insert(reinterpret_cast<std::uint64_t>(addr));
        return outcome::success(reinterpret_cast<std::uint64_t>(addr));
    }
}

void PhysicalPageAllocator::free(std::uint64_t pa) {
    this->alloced_pages_.erase(pa);
    std::free(reinterpret_cast<void*>(pa));
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
