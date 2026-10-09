#include "simplefs.h"

#include <system_error>

namespace vlinux {
namespace fs {
namespace simplefs {
class INode::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    std::uint64_t id_;
    std::uint64_t block_count_;
    std::uint64_t type_;
    std::uint64_t permissions_;
    std::chrono::time_point<std::chrono::system_clock> atime_;
    std::chrono::time_point<std::chrono::system_clock> mtime_;
    std::chrono::time_point<std::chrono::system_clock> ctime_;
    std::uint64_t mem_refcount_;
    std::uint64_t disk_refcount_;
    std::shared_ptr<ISuperBlock> super_block_;
};

class File::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    std::uint64_t refcount = 0;
    std::uint64_t flags = 0;
    std::uint64_t offset = 0;
    std::shared_ptr<INode> inode;
};

File::File(std::shared_ptr<INode> inode) { this->impl_->inode = inode; }

File::~File() = default;

outcome::result<void> File::get() {
    ++this->impl_->refcount;
    return outcome::success();
}

outcome::result<void> File::put() {
    --this->impl_->refcount;
    return outcome::success();
}

std::uint64_t File::refcount() const { return this->impl_->refcount; }

// Flags.
outcome::result<void> File::set_flags(std::uint64_t flags) {
    this->impl_->flags = flags;
    return outcome::success();
}

std::uint64_t File::flags() const { return this->impl_->flags; }

// Offset.
outcome::result<void> File::set_offset(std::uint64_t offset) {
    this->impl_->offset = offset;
    return outcome::success();
}

std::uint64_t File::offset() const { return this->impl_->offset; }

std::shared_ptr<IINode> File::inode() const { return this->impl_->inode; }

outcome::result<void> File::iov_read(std::vector<IoVector> iov) {
    return std::errc::not_supported;
}

outcome::result<void> File::iov_write(std::vector<IoVector> iov) {
    return std::errc::not_supported;
}

}  // namespace simplefs
}  // namespace fs
}  // namespace vlinux
