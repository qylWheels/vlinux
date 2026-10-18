#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <sys/syscall.h>

#include <CLI/CLI.hpp>
#include <filesystem>
#include <functional>
#include <map>

#include "vm.h"

int main(int argc, char* argv[]) {
    CLI::App app("Debugger based on vlinux library", "vdbg");

    // Parse command line arguments.
    std::filesystem::path filepath;
    bool list_syscall = false;
    bool show_summary = false;
    bool show_tui = false;
    app.add_option("file", filepath, "Path to the file to debug");
    app.add_flag("-s,--syscall", list_syscall,
                 "List all syscall during emulating");
    app.add_flag("-S,--summary", show_summary,
                 "Show summary of syscall during emulating");
    app.add_flag("-t,--tui", show_tui, "Show TUI for debugging");
    CLI11_PARSE(app, argc, argv);

    // TODO: Check arguments.

    // Parse methods.
    std::map<int, std::function<void(std::uint64_t syscall_id,
                                     std::array<std::uint64_t, 6> args,
                                     std::uint64_t ret)>>
        parse_methods = {
            {SYS_mmap,
             [](std::uint64_t syscall_id, std::array<std::uint64_t, 6> args,
                std::uint64_t ret) {
                 fmt::print(fg(fmt::color::gold) | fmt::emphasis::bold, "{} ",
                            "mmap");
                 fmt::print("({:#x}, {:#x}, {:#x}, {:#x}, {}, {}) = {:#x}\n",
                            args[0], args[1], args[2], args[3],
                            static_cast<int>(args[4]),
                            static_cast<int>(args[5]), ret);
             }},
            {SYS_munmap,
             [](std::uint64_t syscall_id, std::array<std::uint64_t, 6> args,
                std::uint64_t ret) {
                 fmt::print(fg(fmt::color::gold) | fmt::emphasis::bold, "{} ",
                            "munmap");
                 fmt::print("({:#x}, {:#x}) = {}\n", args[0], args[1],
                            static_cast<int>(ret));
             }}};

    // Load the program.
    vlinux::VM vm;
    vm.reset();
    (void)vm.load(filepath);

    // Add syscall hooks.
    if (list_syscall) {
        (void)vm.add_syscall_hook([&](std::uint64_t syscall_id,
                                      std::array<std::uint64_t, 6> args,
                                      std::uint64_t ret) {
            auto it = parse_methods.find(syscall_id);
            if (it == parse_methods.end()) {
                // Fallback.
                fmt::print(fg(fmt::color::gold) | fmt::emphasis::bold, "<{}> ",
                           syscall_id);
                fmt::print(
                    " ({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x}) = {:#x}\n",
                    syscall_id, args[0], args[1], args[2], args[3], args[4],
                    args[5], ret);
            } else {
                it->second(syscall_id, args, ret);
            }
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

    // Emulate the program.
    (void)vm.run(std::chrono::milliseconds(5000),
                 std::chrono::milliseconds(100));

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

    return 0;
}
