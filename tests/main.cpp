#include <unicorn/unicorn.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "mm.h"
#include "task.h"
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
        REQUIRE(pt.map(0x1000, 0x2000).has_value());
        REQUIRE(pt.unmap(0x1001).has_error());
        REQUIRE(pt.unmap(0x1000).has_value());
    }
}

TEST_CASE("Test Task", "[task]") {
    SECTION("Test constructor and destructor") {
        vlinux::Task task(nullptr);
        REQUIRE(task.state == vlinux::Task::State::New);
    }
}

TEST_CASE("Test Scheduler", "[scheduler]") {
    uc_engine *uc = nullptr;
    uc_err err = ::uc_open(UC_ARCH_X86, UC_MODE_64, &uc);
    REQUIRE(err == UC_ERR_OK);

    vlinux::Scheduler scheduler(uc);

    SECTION("Test constructor and destructor") {
        REQUIRE(scheduler.status() == vlinux::Scheduler::Status::Stopped);
    }

    SECTION("Test add_task() and remove_task()") {
        auto task = std::make_shared<vlinux::Task>(nullptr);
        REQUIRE(scheduler.add_task(task).has_value());
        REQUIRE(scheduler.remove_task(task).has_value());
    }

    SECTION("Test start_schedule() and stop_schedule()") {
        // Allocate a context.
        uc_context *ctx1 = nullptr;
        REQUIRE(uc_context_alloc(uc, &ctx1) == UC_ERR_OK);
        uc_context *ctx2 = nullptr;
        REQUIRE(uc_context_alloc(uc, &ctx2) == UC_ERR_OK);

        // Create tasks.
        REQUIRE(scheduler.add_task(std::make_shared<vlinux::Task>(ctx1))
                    .has_value());
        REQUIRE(scheduler.add_task(std::make_shared<vlinux::Task>(ctx2))
                    .has_value());

        // Start schedule.
        std::promise<void> err_promise;
        std::future<void> err_future = err_promise.get_future();
        REQUIRE(scheduler
                    .start_schedule(std::chrono::milliseconds(500), err_promise)
                    .has_value());

        // Stop schedule.
        REQUIRE_NOTHROW(err_future.get());
        REQUIRE(scheduler.stop_schedule().has_value());
    }
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
