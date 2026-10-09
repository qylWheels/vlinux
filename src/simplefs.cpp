#include "simplefs.h"

#include <system_error>

namespace vlinux {
namespace fs {
namespace simplefs {
class SuperBlock::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    std::size_t block_size_;
    std::uint64_t magic_;
    std::shared_ptr<DEntry> root_dentry_;
    std::vector<std::shared_ptr<INode>> inodes_;
};

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

INode::INode() = default;

INode::~INode() = default;

std::uint64_t INode::id() const { return this->impl_->id_; }

// Size.
std::uint64_t INode::block_count() const { return this->impl_->block_count_; }

// Type and permissions.
std::uint64_t INode::type() const { return this->impl_->type_; }

outcome::result<void> INode::set_permissions(std::uint64_t permissions) {
    this->impl_->permissions_ = permissions;
    return outcome::success();
}

std::uint64_t INode::permissions() const { return this->impl_->permissions_; }

// Timestamp.
std::chrono::time_point<std::chrono::system_clock> INode::atime() const {
    return this->impl_->atime_;
}

std::chrono::time_point<std::chrono::system_clock> INode::mtime() const {
    return this->impl_->mtime_;
}

std::chrono::time_point<std::chrono::system_clock> INode::ctime() const {
    return this->impl_->ctime_;
}

// References.
std::uint64_t INode::mem_refcount() const { return this->impl_->mem_refcount_; }

std::uint64_t INode::disk_refcount() const {
    return this->impl_->disk_refcount_;
}

std::shared_ptr<ISuperBlock> INode::super_block() const {
    return this->impl_->super_block_;
}

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
