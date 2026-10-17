#include "vfs.h"

#include <unicorn/unicorn.h>

#include <cassert>
#include <cstdint>
#include <outcome/try.hpp>
#include <system_error>

namespace vlinux {
outcome::result<std::size_t> File::iov_read(std::vector<IoVector> iov) {
    if (this->inode_->type() != IINode::Type::File) {
        return std::errc::is_a_directory;
    }

    OUTCOME_TRY(auto data_result, this->inode_->data());
    auto data = std::get_if<IINode::NormalFileContent>(data_result);
    assert(data);

    std::uint64_t read_len = 0;
    for (const auto& v : iov) {
        auto len = std::min(v.len, data->size() - this->offset_);
        if (::uc_mem_write(this->uc_, v.base, data->data() + this->offset_,
                           len) != UC_ERR_OK) {
            return std::errc::bad_address;
        }
        read_len += len;
        this->offset_ += len;
    }

    return read_len;
}

outcome::result<std::size_t> File::iov_write(std::vector<IoVector> iov) {
    if (this->inode_->type() != IINode::Type::File) {
        return std::errc::is_a_directory;
    }

    OUTCOME_TRY(auto data_result, this->inode_->data());
    auto data = std::get_if<IINode::NormalFileContent>(data_result);
    assert(data);

    std::uint64_t write_len = 0;
    for (const auto& v : iov) {
        if (data->size() < this->offset_ + v.len) {
            data->resize(this->offset_ + v.len);
        }
        if (::uc_mem_read(this->uc_, v.base, data->data() + this->offset_,
                          v.len) != UC_ERR_OK) {
            return std::errc::bad_address;
        }
        write_len += v.len;
        this->offset_ += v.len;
    }

    return write_len;
}
}  // namespace vlinux
