#pragma once

#include <capstone/capstone.h>

#include <memory>
#include <outcome/outcome.hpp>
#include <string>
#include <string_view>

#include "vm.h"

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vdbg {
namespace repl {
class Repl {
public:
    Repl(vlinux::VM& vm, ::csh& cshandle);
    ~Repl() = default;
    Repl(const Repl&) = delete;
    Repl& operator=(const Repl&) = delete;
    Repl(Repl&&) = delete;
    Repl& operator=(Repl&&) = delete;

public:
    outcome::result<std::string> readline();
    outcome::result<void> execute(std::string_view line);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace repl
}  // namespace vdbg
