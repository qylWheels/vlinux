#include "repl.h"

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
    std::unordered_map<std::string, std::function<outcome::result<void>(
                                        std::vector<std::string>)>>
        command_handlers;
    std::vector<std::string> history;
    std::set<std::uint64_t> breakpoints;
};

Repl::Repl() : impl_(std::make_unique<Impl>()) {}

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
