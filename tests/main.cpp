#include <catch2/catch_test_macros.hpp>

#include "mm.h"

TEST_CASE("Test PhysicalPageAllocator", "[physical_page_allocator]") {
    vlinux::PhysicalPageAllocator ppa;

    SECTION("Test alloc() and free()") {
        REQUIRE_NOTHROW(ppa.free(ppa.alloc()));
    }
}
