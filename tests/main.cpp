#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "mm.h"
#include "process.h"
#include "vm.h"

TEST_CASE("Test PhysicalPageAllocator", "[physical_page_allocator]") {
    vlinux::PhysicalPageAllocator ppa;

    SECTION("Test alloc() and free()") {
        auto pa = ppa.alloc();
        REQUIRE(pa.has_value());
        ppa.free(pa.value());
    }
}

TEST_CASE("Test PageTable", "[page_table]") {
    vlinux::PageTable pt;
    vlinux::PhysicalPageAllocator ppa;

    SECTION("Test map(), unmap() and va_to_pa()") {
        auto result = ppa.alloc();
        REQUIRE(result.has_value());

        auto pa = result.value();
        REQUIRE(pt.map(0x1000, pa).has_value());
        REQUIRE(pt.va_to_pa(0x1000) == pa);
        REQUIRE(pt.va_to_pa(0x1145) == pa + 0x1145 - 0x1000);
        REQUIRE(pt.va_to_pa(0x1000 + 4096 - 1) == pa + 4096 - 1);
        REQUIRE(pt.unmap(0x1000).has_value());
        REQUIRE(pt.va_to_pa(0x1000) == std::nullopt);

        ppa.free(pa);
    }

    SECTION("Test map() with unaligned va and pa") {
        REQUIRE(pt.map(0x1001, 0x2000).has_error());
        REQUIRE(pt.map(0x1000, 0x2001).has_error());
        REQUIRE(pt.map(0x1001, 0x2001).has_error());
    }

    SECTION("Test unmap() with unaligned va") {
        pt.map(0x1000, 0x2000);
        REQUIRE_THROWS_AS(pt.unmap(0x1001), std::runtime_error);
        REQUIRE_NOTHROW(pt.unmap(0x1000));
    }
}

TEST_CASE("Test Task", "[task]") {
    vlinux::Task task;
    REQUIRE(task.state == vlinux::Task::State::New);
}

TEST_CASE("Test VM", "[vm]") {
    vlinux::VM vm;
    vm.reset();

    SECTION("Test load()") {
        REQUIRE(
            vm.load("/home/comma/projs/vlinux/tmp/test_start").has_value() ==
            true);
    }

    SECTION("Test run()") {
        (void)vm.load("/home/comma/projs/vlinux/tmp/test_start");
        REQUIRE(vm.run().has_value() == true);
    }
}
