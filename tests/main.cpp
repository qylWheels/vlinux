#include <sys/syscall.h>
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <thread>

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
        REQUIRE(ppa.free(pa_desc.value()).has_value());
        REQUIRE(ppa.free(pa_desc.value()).has_error());
    }

    REQUIRE(::uc_close(uc) == UC_ERR_OK);
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
        auto pa1 = pt.va_to_pa(0xbeef'0000);
        auto pa2 = pt.va_to_pa(0xbeef'0114);
        auto pa3 = pt.va_to_pa(0xbeef'0000 + 4096 - 1);
        REQUIRE((pa1.has_value() && pa1.value() == pa_desc->start_addr));
        REQUIRE(
            (pa2.has_value() && pa2.value() == pa_desc->start_addr + 0x0114));
        REQUIRE(
            (pa3.has_value() && pa3.value() == pa_desc->start_addr + 4096 - 1));

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

    REQUIRE(::uc_close(uc) == UC_ERR_OK);
}

TEST_CASE("Test PidManager", "[pid_manager]") {
    vlinux::PidManager pid_manager;
    SECTION("Test alloc_pid() and free_pid()") {
        // Allocate a PID.
        auto pid = pid_manager.alloc_pid();
        REQUIRE(pid.has_value());
        REQUIRE(pid.value() == 0);

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
        REQUIRE(scheduler
                    .start_schedule(std::chrono::milliseconds(1500),
                                    std::chrono::milliseconds(500))
                    .has_value());

        // Let the scheduler run for a while.
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Test status after schedule ends.
        REQUIRE(scheduler.status() == vlinux::Scheduler::Status::Stopped);
        REQUIRE(::uc_context_free(ctx1) == UC_ERR_OK);
        REQUIRE(::uc_context_free(ctx2) == UC_ERR_OK);
    }

    REQUIRE(::uc_close(uc) == UC_ERR_OK);
}

TEST_CASE("Test VM", "[vm]") {
    vlinux::VM vm;
    vm.reset();
    auto build_path = std::filesystem::path(CMAKE_BUILD_DIR_PATH);
    auto calc_unmap_page_cnt = [](std::uint64_t old_addr,
                                  std::uint64_t new_addr,
                                  std::uint64_t pgsize) -> std::int64_t {
        auto old_round_up = (old_addr + vlinux::mm::PAGE_SIZE - 1) &
                            (~(vlinux::mm::PAGE_SIZE - 1));
        auto new_round_up = (new_addr + vlinux::mm::PAGE_SIZE - 1) &
                            (~(vlinux::mm::PAGE_SIZE - 1));
        return (static_cast<std::int64_t>(new_round_up) -
                static_cast<std::int64_t>(old_round_up)) /
               static_cast<std::int64_t>(vlinux::mm::PAGE_SIZE);
    };

    SECTION("Test load()") {
        REQUIRE(
            vm.load(build_path / "tests/syscall_tests/test_brk").has_value());
    }

    SECTION("Test brk()") {
        auto task =
            vm.load(build_path / "tests/syscall_tests/test_brk").value();

        // Log mm status before calling brk().
        auto mm = task->address_space;
        auto old_vdesc_cnt = static_cast<std::int64_t>(mm->vpages.size());
        auto old_start_brk = mm->start_brk;
        auto old_brk = mm->brk;

        auto result = vm.add_syscall_hook([&](std::uint64_t syscall_id,
                                              std::array<std::uint64_t, 6> args,
                                              std::uint64_t ret) {
            if (syscall_id != SYS_brk) {
                return;
            }

            // start_brk should never change.
            REQUIRE(mm->start_brk == old_start_brk);

            auto addr = args[0];
            if (addr < old_start_brk) {
                REQUIRE(ret == old_brk);
                REQUIRE(mm->vpages.size() == old_vdesc_cnt);
                REQUIRE(mm->brk == old_brk);
            } else {
                REQUIRE(ret == mm->brk);
                auto page_cnt =
                    calc_unmap_page_cnt(old_brk, addr, vlinux::mm::PAGE_SIZE);
                REQUIRE(mm->vpages.size() ==
                        static_cast<std::uint64_t>(old_vdesc_cnt + page_cnt));
                REQUIRE(mm->brk == addr);
            }

            // Update old data.
            old_start_brk = mm->start_brk;
            old_brk = mm->brk;
            old_vdesc_cnt = mm->vpages.size();
        });
        REQUIRE(result.has_value());

        REQUIRE(vm.run(std::chrono::milliseconds(100),
                       std::chrono::milliseconds(10))
                    .has_value());
    }
}
