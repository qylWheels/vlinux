#pragma once

#include <unicorn/unicorn.h>

#include "vfs.h"
namespace vlinux {
namespace fs {
namespace simplefs {
class SuperBlock : public ISuperBlock {
public:
    SuperBlock(uc_engine* uc);
    ~SuperBlock();
    SuperBlock(const SuperBlock&) = delete;
    SuperBlock& operator=(const SuperBlock&) = delete;
    SuperBlock(SuperBlock&&) = delete;
    SuperBlock& operator=(SuperBlock&&) = delete;

public:
    // Basic information.
    virtual std::size_t block_size() const override;
    virtual std::uint64_t magic() const override;

    // Inode operations.
    virtual outcome::result<std::shared_ptr<IINode>> alloc_inode(
        std::shared_ptr<ISuperBlock> sb, IINode::Type type) override;
    virtual void destroy_inode(std::shared_ptr<IINode> inode) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class INode : public IINode {
    friend class SuperBlock;
    friend class File;

public:
    INode();
    ~INode();
    INode(const INode&) = delete;
    INode& operator=(const INode&) = delete;
    INode(INode&&) = delete;
    INode& operator=(INode&&) = delete;

public:
    virtual std::uint64_t id() const override;

    // Size.
    virtual std::uint64_t block_count() const override;

    // Type and permissions.
    virtual Type type() const override;
    virtual outcome::result<void> set_permissions(
        std::uint64_t permissions) override;
    virtual std::uint64_t permissions() const override;

    // Timestamp.
    virtual std::chrono::time_point<std::chrono::system_clock> atime()
        const override;
    virtual std::chrono::time_point<std::chrono::system_clock> mtime()
        const override;
    virtual std::chrono::time_point<std::chrono::system_clock> ctime()
        const override;

    // References.
    virtual outcome::result<void> inc_mem_refcount() override;
    virtual outcome::result<void> dec_mem_refcount() override;
    virtual std::uint64_t mem_refcount() const override;
    virtual std::uint64_t disk_refcount() const override;

    // Super block.
    virtual std::weak_ptr<ISuperBlock> super_block() const override;

    // Lookup inode of subdirectory item.
    virtual outcome::result<std::shared_ptr<IINode>> lookup(
        std::string name) override;

    // File data.
    virtual outcome::result<Content*> data() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace simplefs
}  // namespace fs
}  // namespace vlinux
