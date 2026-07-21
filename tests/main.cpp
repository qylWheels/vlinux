#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "mm.h"
#include "vm.h"

TEST_CASE("Test PhysicalPageAllocator", "[physical_page_allocator]") {
    vlinux::PhysicalPageAllocator ppa;

    SECTION("Test alloc() and free()") {
        REQUIRE_NOTHROW(ppa.free(ppa.alloc()));
    }
}

TEST_CASE("Test PageTable", "[page_table]") {
    vlinux::PageTable pt;
    vlinux::PhysicalPageAllocator ppa;

    SECTION("Test map(), unmap() and va_to_pa()") {
        std::uint64_t pa = ppa.alloc();
        pt.map(0x1000, pa);
        REQUIRE(pt.va_to_pa(0x1000) == pa);
        REQUIRE(pt.va_to_pa(0x1145) == pa + 0x1145 - 0x1000);
        REQUIRE(pt.va_to_pa(0x1000 + 4096 - 1) == pa + 4096 - 1);
        pt.unmap(0x1000);
        REQUIRE(pt.va_to_pa(0x1000) == std::nullopt);
        ppa.free(pa);
    }

    SECTION("Test map() with unaligned va") {
        REQUIRE_THROWS_AS(pt.map(0x1001, 0x2000), std::runtime_error);
        REQUIRE_NOTHROW(pt.map(0x1000, 0x2001));
    }

    SECTION("Test unmap() with unaligned va") {
        pt.map(0x1000, 0x2000);
        REQUIRE_THROWS_AS(pt.unmap(0x1001), std::runtime_error);
        REQUIRE_NOTHROW(pt.unmap(0x1000));
    }
}

TEST_CASE("Test VM", "[vm]") {
    vlinux::VM vm;
    vm.reset();

    SECTION("Test load()") {
        REQUIRE_NOTHROW(vm.load("/home/comma/projs/vlinux/tmp/test"));
    }
}
