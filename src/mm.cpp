#include "mm.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <stdexcept>

namespace vlinux {
PhysicalPageAllocator::PhysicalPageAllocator() = default;

PhysicalPageAllocator::~PhysicalPageAllocator() = default;

std::uint64_t PhysicalPageAllocator::alloc() {
    return reinterpret_cast<std::uint64_t>(std::malloc(PAGE_SIZE));
}

void PhysicalPageAllocator::free(std::uint64_t pa) {
    std::free(reinterpret_cast<void*>(pa));
}

PageTable::PageTable() = default;

PageTable::~PageTable() = default;

void PageTable::map(std::uint64_t va, std::uint64_t pa) {
    if (va % PAGE_SIZE) {
        throw std::runtime_error("va must be aligned to PAGE_SIZE");
    }
    auto result = this->map_.insert({va, pa});
    if (!result.second) {
        // va already mapped.
        throw std::runtime_error(std::format(
            "virtual address {} is already mapped to physical address {}", va,
            result.first->second));
    }
}

void PageTable::unmap(std::uint64_t va) {
    if (va % PAGE_SIZE != 0) {
        throw std::runtime_error("va must be aligned to PAGE_SIZE");
    }
    this->map_.erase(va);
}

std::optional<std::uint64_t> PageTable::va_to_pa(std::uint64_t va) {
    std::uint64_t aligned_va = va & ~(PAGE_SIZE - 1);
    auto it = this->map_.find(aligned_va);
    if (it == this->map_.end()) {
        return std::nullopt;
    }
    return it->second + (va - aligned_va);
}
}  // namespace vlinux
