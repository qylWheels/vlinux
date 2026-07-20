#include <catch2/catch_test_macros.hpp>

#include "mm.h"

TEST_CASE("Test PhysicalPageAllocator", "[physical_page_allocator]") {
    vlinux::PhysicalPageAllocator ppa;

    SECTION("Test alloc() and free()") {
        REQUIRE_NOTHROW(ppa.free(ppa.alloc()));
    }
}

TEST_CASE("Test PageTable", "[page_table]") {
    vlinux::PageTable pt;

    SECTION("Test map(), unmap() and va_to_pa()") {
        pt.map(0x1000, 0x2000);
        REQUIRE(pt.va_to_pa(0x1000) == 0x2000);
        pt.unmap(0x1000);
        REQUIRE(pt.va_to_pa(0x1000) == std::nullopt);
    }
}
