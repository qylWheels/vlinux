#include "mm.h"

#include <cstdint>
#include <cstdlib>

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
}  // namespace vlinux
