#pragma once

#include <capstone/capstone.h>

#include <memory>
#include <outcome/outcome.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "vm.h"

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vdbg {
namespace repl {
class Repl {
public:
    Repl(vlinux::VM& vm, ::csh& cshandle, std::shared_ptr<vlinux::Task> task);
    ~Repl();
    Repl(const Repl&) = delete;
    Repl& operator=(const Repl&) = delete;
    Repl(Repl&&) = delete;
    Repl& operator=(Repl&&) = delete;

public:
    outcome::result<std::string> readline();
    std::pair<bool, std::string> execute(std::string_view line);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace repl
}  // namespace vdbg
