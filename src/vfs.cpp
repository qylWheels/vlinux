#include "vfs.h"

#include <unicorn/unicorn.h>

#include <cstdint>
#include <outcome/try.hpp>
#include <system_error>

namespace vlinux {
outcome::result<std::size_t> File::iov_read(std::vector<IoVector> iov) {
    OUTCOME_TRY(auto data, this->inode_->data());

    std::uint64_t read_len = 0;
    std::uint64_t offset = this->offset_;
    for (const auto& v : iov) {
        auto len = std::min(v.len, data->size() - offset);
        if (::uc_mem_write(this->uc_, v.base, data->data() + offset, len) !=
            UC_ERR_OK) {
            return std::errc::bad_address;
        }
        read_len += len;
        offset += len;
    }

    return read_len;
}

outcome::result<std::size_t> File::iov_write(std::vector<IoVector> iov) {
    OUTCOME_TRY(auto data, this->inode_->data());

    std::uint64_t write_len = 0;
    std::uint64_t offset = this->offset_;
    for (const auto& v : iov) {
        if (data->size() < offset + v.len) {
            data->resize(offset + v.len);
        }
        if (::uc_mem_read(this->uc_, v.base, data->data() + offset, v.len) !=
            UC_ERR_OK) {
            return std::errc::bad_address;
        }
        write_len += v.len;
        offset += v.len;
    }

    return write_len;
}
}  // namespace vlinux
