#include "simplefs.h"

#include <memory>
#include <set>
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

class SuperBlock::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    std::size_t block_size_ = 4096;
    std::uint64_t magic_ = 0xf8f90000;
    std::shared_ptr<DEntry> root_dentry_;
    std::set<std::shared_ptr<INode>> inodes_;
    std::uint64_t next_inode_id_ = 0;
};

// Basic information.
std::size_t SuperBlock::block_size() const { return this->impl_->block_size_; }

std::uint64_t SuperBlock::magic() const { return this->impl_->magic_; }

std::shared_ptr<IDEntry> SuperBlock::root_dentry() const {
    return this->impl_->root_dentry_;
}

// Inode operations.
outcome::result<std::shared_ptr<IINode>> SuperBlock::alloc_inode(
    std::shared_ptr<ISuperBlock> sb) {
    auto inode = std::make_shared<INode>();
    inode->impl_->id_ = this->impl_->next_inode_id_++;
    inode->impl_->block_count_ = 0;
    inode->impl_->type_ = 0;
    inode->impl_->permissions_ = 0;
    inode->impl_->atime_ = std::chrono::system_clock::now();
    inode->impl_->mtime_ = std::chrono::system_clock::now();
    inode->impl_->ctime_ = std::chrono::system_clock::now();
    inode->impl_->mem_refcount_ = 0;
    inode->impl_->disk_refcount_ = 0;
    inode->impl_->super_block_ = sb;
    auto ret = this->impl_->inodes_.insert(inode);
    if (!ret.second) {
        return std::errc::file_exists;
    }
    return inode;
}

void SuperBlock::destroy_inode(std::shared_ptr<IINode> inode) {
    auto inode2 = std::dynamic_pointer_cast<INode>(inode);
    this->impl_->inodes_.erase(inode2);
    return;
}

class DEntry::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    std::shared_ptr<DEntry> parent;
    std::string name;
    std::shared_ptr<INode> inode;
    std::shared_ptr<SuperBlock> super_block;
    std::vector<std::shared_ptr<DEntry>> subdirs;
};

std::shared_ptr<IDEntry> DEntry::parent() const { return this->impl_->parent; }

std::string DEntry::name() const { return this->impl_->name; }

bool DEntry::compare(const std::string& name) const {
    return this->impl_->name == name;
}

std::shared_ptr<IINode> DEntry::inode() const { return this->impl_->inode; }

std::shared_ptr<ISuperBlock> DEntry::super_block() const {
    return this->impl_->super_block;
}

std::vector<std::shared_ptr<IDEntry>> DEntry::subdirs() const {
    return std::vector<std::shared_ptr<IDEntry>>(this->impl_->subdirs.begin(),
                                                 this->impl_->subdirs.end());
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
