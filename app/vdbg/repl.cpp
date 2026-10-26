#include "repl.h"

#include <bits/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <numeric>
#include <outcome/result.hpp>
#include <ranges>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vdbg {
namespace repl {
class Repl::Impl {
public:
    vlinux::VM* vm;
    ::csh* cshandle;
    std::shared_ptr<vlinux::Task> task;
    std::unordered_map<std::string, std::function<std::pair<bool, std::string>(
                                        std::vector<std::string> args)>>
        command_handlers;
    std::vector<std::string> history;
    std::set<std::uint64_t> breakpoints;
};

Repl::Repl(vlinux::VM& vm, ::csh& cshandle, std::shared_ptr<vlinux::Task> task)
    : impl_(std::make_unique<Impl>()) {
    impl_->vm = &vm;
    impl_->cshandle = &cshandle;
    impl_->task = task;

    // Add instruction hook.
    auto result = impl_->vm->add_instr_hook([&](std::uint64_t addr) {
        if (impl_->breakpoints.find(addr) != impl_->breakpoints.end()) {
            impl_->vm->pause();
        }
    });

    impl_->command_handlers["entrypoint"] =
        [&](std::vector<std::string> args) -> std::pair<bool, std::string> {
        if (args.size() != 0) {
            return {false, "Invalid arguments. Usage: entrypoint"};
        }

        return {true, fmt::format("Entry point is {:#x}",
                                  impl_->vm->get_entry_point())};
    };

    impl_->command_handlers["break"] =
        [&](std::vector<std::string> args) -> std::pair<bool, std::string> {
        if (args.size() != 1) {
            return {false, "Invalid arguments. Usage: break <addr>"};
        }

        // Parse address.
        std::uint64_t addr;
        auto [ptr, ec] = std::from_chars(args[0].data(),
                                         args[0].data() + args[0].size(), addr);
        if (ec != std::errc{} || ptr != args[0].data() + args[0].size()) {
            return {false, "Invalid address"};
        }

        // Add breakpoint.
        auto [it, inserted] = impl_->breakpoints.insert(addr);
        if (!inserted) {
            return {false,
                    fmt::format("Breakpoint already set at {:#x}", addr)};
        }

        return {true, fmt::format("Breakpoint set at {:#x}", addr)};
    };

    // Usage: disassemble <addr> <count>
    impl_->command_handlers["disassemble"] =
        [&](std::vector<std::string> args) -> std::pair<bool, std::string> {
        if (args.size() != 2) {
            return {false,
                    "Invalid arguments. Usage: disassemble <addr> <count>"};
        }

        // Parse args.
        std::uint64_t addr;
        auto [addr_ptr, addr_ec] = std::from_chars(
            args[0].data(), args[0].data() + args[0].size(), addr);
        if (addr_ec != std::errc{} ||
            addr_ptr != args[0].data() + args[0].size()) {
            return {false, "Invalid address"};
        }
        std::uint64_t count;
        auto [count_ptr, count_ec] = std::from_chars(
            args[1].data(), args[1].data() + args[1].size(), count);
        if (count_ec != std::errc{} ||
            count_ptr != args[1].data() + args[1].size()) {
            return {false, "Invalid count"};
        }

        // Disassemble.
        std::vector<std::string> insn_strs;
        std::uint64_t offset = 0;  // Offset from addr.
        for (std::uint64_t i = 0; i < count; i++) {
            ::cs_insn* insn;
            // 16 is maximum instruction size in x86-64.
            auto code_result = vm.read_vmem(impl_->task, addr + offset, 16);
            if (code_result.has_error()) {
                return {false, fmt::format("Failed to read memory at {:#x}",
                                           addr + offset)};
            }
            auto code = code_result.value();
            int count = ::cs_disasm(*this->impl_->cshandle, code.data(),
                                    code.size(), addr + offset, 1, &insn);
            if (count == 0) {
                insn_strs.push_back("<Invalid Instruction>");
            } else {
                insn_strs.push_back(fmt::format("{:#x}: {} {}", insn->address,
                                                insn->mnemonic, insn->op_str));
                offset += insn->size;
                ::cs_free(insn, count);
            }
        }

        return {true, fmt::format("{}", fmt::join(insn_strs, "\n"))};
    };

    impl_->command_handlers["run"] =
        [&](std::vector<std::string> args) -> std::pair<bool, std::string> {
        if (args.size() != 0) {
            return {false, "Invalid arguments. Usage: run"};
        }

        auto result = vm.run(
            // FIXME: Set a reasonable timeout.
            std::chrono::milliseconds(100'000'000),
            std::chrono::milliseconds(200));
        if (result.has_error()) {
            return {false,
                    fmt::format("Failed to run: {}", result.error().message())};
        }

        return {true, "Run completed"};
    };
}

Repl::~Repl() = default;

std::pair<bool, std::string> Repl::execute(std::string_view line) {
    std::vector<std::string> args;

    auto it = line | std::views::split(' ');
    for (auto arg : it) {
        args.push_back(std::string(arg.begin(), arg.end()));
    }

    if (args.empty()) {
        return {true, ""};  // Empty line is allowed.
    }

    // Parse and execute the command.
    std::string cmd = args.front();
    args.erase(args.begin());
    auto candidates =
        impl_->command_handlers | std::views::filter([&](const auto& kv) {
            return kv.first.starts_with(cmd);
        });
    auto dist = std::ranges::distance(candidates.begin(), candidates.end());
    if (dist > 1) {
        auto candidates_strs = std::accumulate(
            candidates.begin(), candidates.end(), std::vector<std::string>(),
            [](std::vector<std::string> acc, const auto& kv) {
                acc.push_back(kv.first);
                return acc;
            });
        return {false,
                fmt::format("Ambiguous command: \"{}\", candidates are: {}",
                            cmd, candidates_strs)};
    }
    if (dist == 0) {
        return {false, fmt::format("Command {} not supported", cmd)};
    }
    return candidates.begin()->second(args);
}
}  // namespace repl
}  // namespace vdbg
