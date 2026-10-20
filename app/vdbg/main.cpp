#include <capstone/capstone.h>
#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <sys/syscall.h>

#include <CLI/CLI.hpp>
#include <filesystem>
#include <functional>
#include <ranges>

#include "syscall_parse.h"
#include "tui.h"
#include "vm.h"

int main(int argc, char* argv[]) {
    CLI::App app("Debugger based on vlinux library", "vdbg");

    // Parse command line arguments.
    std::filesystem::path filepath;
    bool raw_syscall = false;
    bool list_syscall = false;
    bool show_summary = false;
    bool show_tui = false;
    app.add_option("file", filepath, "Path to the file to debug");
    app.add_flag("-r,--raw", raw_syscall, "Show raw syscall arguments");
    app.add_flag("-s,--syscall", list_syscall,
                 "List all syscall during emulating");
    app.add_flag("-S,--summary", show_summary,
                 "Show summary of syscall during emulating");
    app.add_flag("-t,--tui", show_tui, "Show TUI for debugging");
    CLI11_PARSE(app, argc, argv);

    // TODO: Check arguments.

    // Load the program.
    vlinux::VM vm;
    vm.reset();
    (void)vm.load(filepath);

    // Add syscall hooks.
    if (list_syscall) {
        (void)vm.add_syscall_hook([&](std::uint64_t syscall_id,
                                      std::array<std::uint64_t, 6> args,
                                      std::uint64_t ret) {
            if (!raw_syscall) {
                auto syscall_parser = vdbg::syscall_parse::SyscallParser();
                auto result = syscall_parser.parse(syscall_id, args, ret);
                if (result.has_value()) {
                    fmt::print("{} ", result.value().syscall);
                    auto result_value = result.value();
                    auto valid_args = result_value.args |
                                      std::views::take_while([](auto arg) {
                                          return arg.has_value();
                                      }) |
                                      std::views::transform(
                                          [](auto arg) { return arg.value(); });
                    fmt::print("({}) = {}\n", fmt::join(valid_args, ", "),
                               result_value.ret);
                    return;
                }
            }

            // Fallback.
            fmt::print("<{}> ", syscall_id);
            fmt::print(" ({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x}) = {:#x}\n",
                       args[0], args[1], args[2], args[3], args[4], args[5],
                       ret);
        });
    }
    std::uint64_t syscall_count = 0;
    if (show_summary) {
        (void)vm.add_syscall_hook([&](std::uint64_t syscall_id,
                                      std::array<std::uint64_t, 6> args,
                                      std::uint64_t ret) { ++syscall_count; });
    }

    // Add instruction hooks.
    std::uint64_t instr_count = 0;
    if (show_summary) {
        (void)vm.add_instr_hook([&](std::uint64_t addr) { ++instr_count; });
    }

    // Show TUI if requested.
    ::cs_err err;
    ::csh handle;
    if (show_tui) {
        // Initialize Capstone engine.
        err = ::cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
        if (err != CS_ERR_OK) {
            fmt::println(stderr,
                         "Failed to initialize Capstone engine: {} ({})",
                         ::cs_strerror(err), static_cast<int>(err));
            return err;
        }

        // Initialize TUI.
        vdbg::tui::TUI tui(vm, handle);

        // Show TUI.
        tui.show();
    }

    // Emulate the program straight forward if TUI not enabled.
    if (!show_tui) {
        (void)vm.run(std::chrono::milliseconds(5000),
                     std::chrono::milliseconds(100));
    }

    // Show summary if requested.
    if (show_summary) {
        fmt::println("");
        fmt::println("===== Summary =====");
        fmt::println("Total syscall count: {}",
                     fmt::styled(syscall_count,
                                 fmt::emphasis::bold | fg(fmt::color::gold)));
        fmt::println("Total instruction count: {}",
                     fmt::styled(instr_count,
                                 fmt::emphasis::bold | fg(fmt::color::gold)));
    }

    // Free Capstone engine if TUI enabled.
    if (show_tui) {
        ::cs_close(&handle);
    }

    return 0;
}
