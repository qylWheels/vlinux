#pragma once

#include "vfs.h"
namespace vlinux {
namespace fs {
namespace simplefs {
class SuperBlock : public ISuperBlock {
public:
    SuperBlock();
    ~SuperBlock();
    SuperBlock(const SuperBlock&) = delete;
    SuperBlock& operator=(const SuperBlock&) = delete;
    SuperBlock(SuperBlock&&) = delete;
    SuperBlock& operator=(SuperBlock&&) = delete;

public:
    // Basic information.
    virtual std::size_t block_size() const override;
    virtual std::uint64_t magic() const override;
    virtual std::shared_ptr<IDEntry> root_dentry() const override;

    // Inode operations.
    virtual outcome::result<std::shared_ptr<IINode>> alloc_inode(
        std::shared_ptr<ISuperBlock>) override;
    virtual void destroy_inode(std::shared_ptr<IINode> inode) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class INode : public IINode {
    friend class SuperBlock;

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
    virtual std::uint64_t type() const override;
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
    virtual std::uint64_t mem_refcount() const override;
    virtual std::uint64_t disk_refcount() const override;

    // Super block.
    virtual std::shared_ptr<ISuperBlock> super_block() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class DEntry : public IDEntry {
public:
    DEntry();
    ~DEntry();
    DEntry(const DEntry&) = delete;
    DEntry& operator=(const DEntry&) = delete;
    DEntry(DEntry&&) = delete;
    DEntry& operator=(DEntry&&) = delete;

public:
    virtual std::shared_ptr<IDEntry> parent() const override;
    virtual std::string name() const override;
    virtual bool compare(const std::string& name) const override;
    virtual std::shared_ptr<IINode> inode() const override;
    virtual std::shared_ptr<ISuperBlock> super_block() const override;
    virtual std::vector<std::shared_ptr<IDEntry>> subdirs() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class File : public IFile {
public:
    File(std::shared_ptr<INode> inode);
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = delete;
    File& operator=(File&&) = delete;

public:  // Operations.
    // References.
    virtual outcome::result<void> get() override;
    virtual outcome::result<void> put() override;
    virtual std::uint64_t refcount() const override;

    // Flags.
    virtual outcome::result<void> set_flags(std::uint64_t flags) override;
    virtual std::uint64_t flags() const override;

    // Offset.
    virtual outcome::result<void> set_offset(std::uint64_t offset) override;
    virtual std::uint64_t offset() const override;

    virtual std::shared_ptr<IINode> inode() const override;
    virtual outcome::result<void> iov_read(std::vector<IoVector> iov) override;
    virtual outcome::result<void> iov_write(std::vector<IoVector> iov) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace simplefs
}  // namespace fs
}  // namespace vlinux
