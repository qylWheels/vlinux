#include "syscall_parse.h"

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <string>
#include <system_error>
#include <vector>

namespace vdbg {
namespace syscall_parse {
outcome::result<SyscallParseResult> SyscallParser::parse(
    std::uint32_t syscall, std::array<std::uint64_t, 6> args,
    std::uint64_t ret) {
    switch (syscall) {
        case SYS_mprotect: {
            std::vector<std::string> prot;
            if (args[2] & PROT_NONE) prot.push_back("PROT_NONE");
            if (args[2] & PROT_READ) prot.push_back("PROT_READ");
            if (args[2] & PROT_WRITE) prot.push_back("PROT_WRITE");
            if (args[2] & PROT_EXEC) prot.push_back("PROT_EXEC");
            if (args[2] & PROT_GROWSUP) prot.push_back("PROT_GROWSUP");
            if (args[2] & PROT_GROWSDOWN) prot.push_back("PROT_GROWSDOWN");
            auto prot_str = fmt::format("{}", fmt::join(prot, " | "));
            SyscallParseResult result;
            result[2] = prot_str;
            return result;
        }
    }

    return std::errc::not_supported;
}
}  // namespace syscall_parse
}  // namespace vdbg
