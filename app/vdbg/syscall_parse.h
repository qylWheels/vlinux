#pragma once

#include <array>
#include <cstdint>
#include <string>
namespace vdbg {
namespace syscall_parse {
using SyscallParseResult = std::array<std::string, 6>;

class SyscallParser {
public:
    SyscallParseResult parse(std::uint32_t syscall,
                             std::array<std::uint64_t, 6> args,
                             std::uint64_t ret);
};
}  // namespace syscall_parse
}  // namespace vdbg
