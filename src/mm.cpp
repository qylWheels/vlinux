#include "mm.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <stdexcept>

namespace vlinux {
const std::size_t PAGE_SIZE = 4096;

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
    auto result = this->map_.insert({va, pa});
    if (!result.second) {
        // va already mapped.
        throw std::runtime_error(std::format(
            "virtual address {} is already mapped to physical address {}", va,
            result.first->second));
    }
}

void PageTable::unmap(std::uint64_t va) { this->map_.erase(va); }
}  // namespace vlinux
