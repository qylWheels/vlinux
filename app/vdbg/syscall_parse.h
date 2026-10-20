#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <outcome/outcome.hpp>
#include <string>

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vdbg {
namespace syscall_parse {
using SyscallParseResult = std::array<std::optional<std::string>, 6>;

class SyscallParser {
public:
    outcome::result<SyscallParseResult> parse(std::uint32_t syscall,
                                              std::array<std::uint64_t, 6> args,
                                              std::uint64_t ret);
};
}  // namespace syscall_parse
}  // namespace vdbg
