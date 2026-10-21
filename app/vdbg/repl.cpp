#include "repl.h"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <charconv>
#include <cstdint>
#include <functional>
#include <ios>
#include <iostream>
#include <outcome/result.hpp>
#include <ranges>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace vdbg {
namespace repl {
class Repl::Impl {
public:
    vlinux::VM* vm;
    ::csh* cshandle;
    std::unordered_map<std::string, std::function<outcome::result<std::string>(
                                        std::vector<std::string> args)>>
        command_handlers;
    std::vector<std::string> history;
    std::set<std::uint64_t> breakpoints;
};

Repl::Repl(vlinux::VM& vm, ::csh& cshandle) : impl_(std::make_unique<Impl>()) {
    impl_->vm = &vm;
    impl_->cshandle = &cshandle;

    // Add instruction hook.
    auto result = impl_->vm->add_instr_hook([&](std::uint64_t addr) {
        if (impl_->breakpoints.find(addr) != impl_->breakpoints.end()) {
            impl_->vm->pause();
        }
    });

    impl_->command_handlers["break"] =
        [&](std::vector<std::string> args) -> outcome::result<std::string> {
        if (args.size() != 1) {
            return std::errc::invalid_argument;
        }

        // Parse address.
        std::uint64_t addr;
        auto [ptr, ec] = std::from_chars(args[0].data(),
                                         args[0].data() + args[0].size(), addr);
        if (ec != std::errc{} || ptr != args[0].data() + args[0].size()) {
            return std::errc(ec);
        }

        // Add breakpoint.
        auto [it, inserted] = impl_->breakpoints.insert(addr);
        if (!inserted) {
            return std::errc::address_in_use;
        }

        return fmt::format("Breakpoint set at {:#x}", addr);
    };

    // Usage: disassemble <addr> <count>
    impl_->command_handlers["disassemble"] =
        [&](std::vector<std::string> args) -> outcome::result<std::string> {
        if (args.size() != 2) {
            return std::errc::invalid_argument;
        }

        // Parse args.
        std::uint64_t addr;
        auto [addr_ptr, addr_ec] = std::from_chars(
            args[0].data(), args[0].data() + args[0].size(), addr);
        if (addr_ec != std::errc{} ||
            addr_ptr != args[0].data() + args[0].size()) {
            return std::errc(addr_ec);
        }
        std::uint64_t count;
        auto [count_ptr, count_ec] = std::from_chars(
            args[1].data(), args[1].data() + args[1].size(), count);
        if (count_ec != std::errc{} ||
            count_ptr != args[1].data() + args[1].size()) {
            return std::errc(count_ec);
        }

        // Disassemble.
        std::vector<std::string> insn_strs;
        std::uint64_t offset = 0;  // Offset from addr.
        for (std::uint64_t i = 0; i < count; i++) {
            ::cs_insn* insn;
            // 16 is maximum instruction size in x86-64.
            OUTCOME_TRY(auto code, vm.read_vmem(addr + offset, 16));
            int count = ::cs_disasm(*this->impl_->cshandle, code.data(),
                                    code.size(), addr, 1, &insn);
            if (count == 0) {
                insn_strs.push_back("<Invalid Instruction>");
            } else {
                insn_strs.push_back(fmt::format("{:#x}: {} {}", insn->address,
                                                insn->mnemonic, insn->op_str));
                offset += insn->size;
                ::cs_free(insn, count);
            }
        }

        return fmt::format("{}", fmt::join(insn_strs, "\n"));
    };
}

outcome::result<std::string> Repl::readline() {
    std::string line;
    if (std::getline(std::cin, line).eof()) {
        return std::io_errc::stream;
    }
    return line;
}

outcome::result<void> Repl::execute(std::string_view line) {
    std::vector<std::string> args;

    auto it = line | std::views::split(' ');
    for (auto arg : it) {
        args.push_back(std::string(arg.begin(), arg.end()));
    }

    if (args.empty()) {
        return outcome::success();  // Empty line is allowed.
    }

    // Parse and execute the command.
    std::string cmd = args.front();
    args.erase(args.begin());
    auto handler_it = impl_->command_handlers.find(cmd);
    if (handler_it == impl_->command_handlers.end()) {
        return std::errc::not_supported;
    }
    return handler_it->second(args);
}
}  // namespace repl
}  // namespace vdbg
