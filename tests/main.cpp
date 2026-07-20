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
        REQUIRE(pt.va_to_pa(0x1145) == 0x2145);
        REQUIRE(pt.va_to_pa(0x1000 + 4096 - 1) == 0x2000 + 4096 - 1);
        pt.unmap(0x1000);
        REQUIRE(pt.va_to_pa(0x1000) == std::nullopt);
    }

    SECTION("Test map() with unaligned va and pa") {
        REQUIRE_THROWS_AS(pt.map(0x1001, 0x2001), std::runtime_error);
        REQUIRE_NOTHROW(pt.map(0x1000, 0x2000));
    }

    SECTION("Test unmap() with unaligned va") {
        pt.map(0x1000, 0x2000);
        REQUIRE_THROWS_AS(pt.unmap(0x1001), std::runtime_error);
        REQUIRE_NOTHROW(pt.unmap(0x1000));
    }
}
