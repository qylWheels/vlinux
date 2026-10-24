#include "vfs.h"

#include <unicorn/unicorn.h>

#include <cassert>
#include <cstdint>
#include <outcome/try.hpp>
#include <system_error>

namespace vlinux {
outcome::result<std::size_t> File::preadv(std::vector<IoVector> iov,
                                          std::int64_t offset) {
    if (this->inode_->type() != IINode::Type::File) {
        return std::errc::is_a_directory;
    }

    OUTCOME_TRY(auto data_result, this->inode_->data());
    auto data = std::get_if<IINode::NormalFileContent>(data_result);
    assert(data);

    std::uint64_t read_len = 0;
    for (const auto& v : iov) {
        std::uint64_t len;
        if (offset >= data->size()) {
            len = 0;
        } else {
            len = std::min(v.len, data->size() - offset);
        }
        if (::uc_vmem_write(this->uc_, v.base, UC_PROT_WRITE,
                            data->data() + offset, len) != UC_ERR_OK) {
            return std::errc::bad_address;
        }
        read_len += len;
        offset += len;
    }

    return read_len;
}

outcome::result<std::size_t> File::pwritev(std::vector<IoVector> iov,
                                           std::int64_t offset) {
    if (this->inode_->type() != IINode::Type::File) {
        return std::errc::is_a_directory;
    }

    OUTCOME_TRY(auto data_result, this->inode_->data());
    auto data = std::get_if<IINode::NormalFileContent>(data_result);
    assert(data);

    std::uint64_t write_len = 0;
    for (const auto& v : iov) {
        if (data->size() < offset + v.len) {
            data->resize(offset + v.len);
        }
        if (::uc_vmem_read(this->uc_, v.base, UC_PROT_READ,
                           data->data() + offset, v.len) != UC_ERR_OK) {
            return std::errc::bad_address;
        }
        write_len += v.len;
        offset += v.len;
    }

    return write_len;
}
}  // namespace vlinux
