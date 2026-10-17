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
    app.add_option("file", filepath, "Path to the file to debug");
    app.add_flag("-s,--syscall", list_syscall,
                 "List all syscall during emulating");
    CLI11_PARSE(app, argc, argv);

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

    // Emulate the program.
    vlinux::VM vm;
    vm.reset();
    (void)vm.load(filepath);
    if (list_syscall) {
        (void)vm.add_syscall_hook([&](std::uint64_t syscall_id,
                                      std::array<std::uint64_t, 6> args,
                                      std::uint64_t ret) {
            auto it = parse_methods.find(syscall_id);
            if (it == parse_methods.end()) {
                // Fallback.
                fmt::print(
                    "<{}> ({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x}) = {:#x}\n",
                    syscall_id, args[0], args[1], args[2], args[3], args[4],
                    args[5], ret);
            } else {
                it->second(syscall_id, args, ret);
            }
        });
    }
    (void)vm.run(std::chrono::milliseconds(5000),
                 std::chrono::milliseconds(100));
    return 0;
}
