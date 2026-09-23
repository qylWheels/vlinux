#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>

#include "mm.h"
#include "task.h"
#include "vm.h"

TEST_CASE("Test PhysicalPageAllocator", "[physical_page_allocator]") {
    uc_engine *uc;
    REQUIRE(::uc_open(UC_ARCH_X86, UC_MODE_64, &uc) == UC_ERR_OK);
    vlinux::mm::PhysicalPageAllocator ppa(uc);

    SECTION("Test alloc(), get_page() and free()") {
        auto pa_desc = ppa.alloc();
        REQUIRE(pa_desc.has_value());
        REQUIRE(ppa.get_page(pa_desc.value()).has_value());
        REQUIRE(ppa.free(pa_desc.value()).has_value());
        REQUIRE(ppa.free(pa_desc.value()).has_error());
        REQUIRE(ppa.get_page(pa_desc.value()).has_error());
    }
}

TEST_CASE("Test PageTable", "[page_table]") {
    uc_engine *uc;
    REQUIRE(::uc_open(UC_ARCH_X86, UC_MODE_64, &uc) == UC_ERR_OK);
    vlinux::mm::PageTable pt;
    vlinux::mm::PhysicalPageAllocator ppa(uc);

    SECTION("Test map(), unmap(), va_to_pa() and va_to_desc()") {
        auto result = ppa.alloc();
        REQUIRE(result.has_value());
        auto pa_desc = result.value();

        auto va_desc = std::make_shared<vlinux::mm::VirtualPageDescriptor>();
        va_desc->start_addr = 0xbeef'0000;
        va_desc->len = vlinux::mm::PAGE_SIZE;
        va_desc->perm = UC_PROT_READ | UC_PROT_WRITE;

        // Test map().
        REQUIRE(pt.map(va_desc, pa_desc).has_value());

        // Test va_to_pa().
        REQUIRE(pt.va_to_pa(0xbeef'0000) == pa_desc->start_addr);
        REQUIRE(pt.va_to_pa(0xbeef'1145) == pa_desc->start_addr + 0x1145);
        REQUIRE(pt.va_to_pa(0xbeef'0000 + 4096 - 1) ==
                pa_desc->start_addr + 4096 - 1);

        // Test va_to_desc().
        auto va_desc2 = pt.va_to_desc(0xbeef'0000);
        REQUIRE(va_desc2.has_value());
        REQUIRE(va_desc2.value().get() == va_desc.get());

        // Test unmap().
        REQUIRE(pt.unmap(va_desc).has_value());

        // Test va_to_pa() after unmap().
        REQUIRE(pt.va_to_pa(0xbeef'0000) == std::nullopt);

        // Free physical page.
        REQUIRE(ppa.free(pa_desc).has_value());
    }
}

TEST_CASE("Test PidManager", "[pid_manager]") {
    vlinux::PidManager pid_manager;
    SECTION("Test alloc_pid() and free_pid()") {
        // Allocate a PID.
        auto pid = pid_manager.alloc_pid();
        REQUIRE(pid.has_value());
        REQUIRE(pid.value() == 2);

        // Allocate more PIDs to reach the limit.
        for (std::uint64_t i = 0; i < vlinux::PidManager::kMaxPid - 1; ++i) {
            REQUIRE(pid_manager.alloc_pid().has_value());
        }

        // Allocate a PID after the limit, expect error.
        REQUIRE(pid_manager.alloc_pid().has_error());

        // Free a PID.
        REQUIRE(pid_manager.free_pid(pid.value()).has_value());

        // Allocate a PID after freeing, expect success.
        REQUIRE(pid_manager.alloc_pid().has_value());

        // Free all PIDs.
        for (std::uint64_t i = 0; i < vlinux::PidManager::kMaxPid; ++i) {
            REQUIRE(pid_manager.free_pid(i).has_value());
        }
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

        // Let the scheduler run for a while.
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Stop schedule.
        REQUIRE(scheduler.stop_schedule().has_value());
        REQUIRE(scheduler.status() == vlinux::Scheduler::Status::Stopped);
        REQUIRE(err_future.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready);
        REQUIRE(::uc_context_free(ctx1) == UC_ERR_OK);
        REQUIRE(::uc_context_free(ctx2) == UC_ERR_OK);
    }

    ::uc_close(uc);
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
