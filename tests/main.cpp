#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

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
        // Map code to memory.
        std::uint8_t code[] = {0xEB, 0xFE};
        REQUIRE(::uc_mem_map_ptr(uc, 0x1000, 0x1000,
                                 UC_PROT_READ | UC_PROT_EXEC,
                                 code) == UC_ERR_OK);

        // Allocate and initialize context.
        uc_context *ctx1 = nullptr;
        REQUIRE(uc_context_alloc(uc, &ctx1) == UC_ERR_OK);
        REQUIRE(uc_context_save(uc, ctx1) == UC_ERR_OK);
        uc_context *ctx2 = nullptr;
        REQUIRE(uc_context_alloc(uc, &ctx2) == UC_ERR_OK);
        REQUIRE(uc_context_save(uc, ctx2) == UC_ERR_OK);

        // Set up the context.
        std::uint64_t rip = 0x1000;
        REQUIRE(::uc_context_reg_write(ctx1, UC_X86_REG_RIP, &rip) ==
                UC_ERR_OK);
        REQUIRE(::uc_context_reg_write(ctx2, UC_X86_REG_RIP, &rip) ==
                UC_ERR_OK);

        // Create tasks.
        auto task1 = std::make_shared<vlinux::Task>(ctx1);
        task1->ctx = ctx1;
        REQUIRE(scheduler.add_task(task1).has_value());
        auto task2 = std::make_shared<vlinux::Task>(ctx2);
        task2->ctx = ctx2;
        REQUIRE(scheduler.add_task(task2).has_value());

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
