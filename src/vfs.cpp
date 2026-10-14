#include "vfs.h"

#include <system_error>

namespace vlinux {
outcome::result<std::size_t> File::iov_read(std::vector<IoVector> iov) {
    return std::errc::not_supported;
}

outcome::result<std::size_t> File::iov_write(std::vector<IoVector> iov) {
    return std::errc::not_supported;
}
}  // namespace vlinux
