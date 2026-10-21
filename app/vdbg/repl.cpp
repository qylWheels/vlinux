#include "repl.h"

#include <ios>
#include <iostream>
#include <string>
#include <vector>

namespace vdbg {
namespace repl {
class Repl::Impl {
public:
    std::vector<std::string> history;
};

Repl::Repl() : impl_(std::make_unique<Impl>()) {}

outcome::result<std::string> Repl::readline() {
    std::string line;
    if (std::getline(std::cin, line).eof()) {
        return std::io_errc::stream;
    }
    return line;
}

outcome::result<void> Repl::execute(std::string_view line) {}

}  // namespace repl
}  // namespace vdbg
