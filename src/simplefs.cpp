#include "simplefs.h"

#include <sys/stat.h>
#include <unicorn/unicorn.h>

#include <cstddef>
#include <memory>
#include <outcome/outcome.hpp>
#include <outcome/try.hpp>
#include <set>
#include <system_error>
#include <vector>

namespace vlinux {
namespace fs {
namespace simplefs {
class INode::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    // Subdirectory item.
    struct SubDirItem {
        std::string name;
        std::shared_ptr<INode> inode;
    };

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
    std::weak_ptr<ISuperBlock> super_block_;
    std::vector<std::uint8_t> data_;  // File data.
    // Subdirectory items, only available for directory inode.
    std::vector<SubDirItem> subdir_items_;
};

INode::INode() : impl_(std::make_unique<Impl>()) {}

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

std::weak_ptr<ISuperBlock> INode::super_block() const {
    return this->impl_->super_block_;
}

outcome::result<std::shared_ptr<IINode>> INode::lookup(std::string name) {
    if ((this->impl_->type_ & S_IFDIR) == 0) {
        return std::errc::not_a_directory;
    }

    for (auto& item : this->impl_->subdir_items_) {
        if (item.name == name) {
            return item.inode;
        }
    }

    return std::errc::no_such_file_or_directory;
}

class SuperBlock::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    uc_engine* uc_;
    std::size_t block_size_ = 4096;
    std::uint64_t magic_ = 0xf8f90000;
    std::set<std::shared_ptr<INode>> inodes_;
    std::uint64_t next_inode_id_ = 0;
};

SuperBlock::SuperBlock(uc_engine* uc) : impl_(std::make_unique<Impl>()) {
    this->impl_->uc_ = uc;
}

SuperBlock::~SuperBlock() = default;

// Basic information.
std::size_t SuperBlock::block_size() const { return this->impl_->block_size_; }

std::uint64_t SuperBlock::magic() const { return this->impl_->magic_; }

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
    inode->impl_->data_ = {};
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
}  // namespace simplefs
}  // namespace fs
}  // namespace vlinux
