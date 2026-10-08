#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vlinux {
class IDEntry;
class IINode;

class ISuperBlock {
public:
    ~ISuperBlock() = default;

public:
    virtual std::size_t block_size() const = 0;
    virtual std::uint64_t magic() const = 0;
    virtual std::shared_ptr<IDEntry> root_dentry() const = 0;

public:  // Operations.
    virtual std::shared_ptr<IINode> alloc_inode() = 0;
    virtual void destroy_inode(std::shared_ptr<IINode> inode) = 0;
};

class IINode {
public:
    ~IINode() = default;

public:
    virtual std::uint64_t mem_refcount() const = 0;
    virtual std::uint64_t disk_refcount() const = 0;
    virtual std::uint64_t mode() const = 0;
    virtual std::shared_ptr<ISuperBlock> super_block() const = 0;
};

class IDEntry {
public:
    ~IDEntry() = default;

public:
    virtual std::shared_ptr<IDEntry> parent() const = 0;
    virtual std::string name() const = 0;
    virtual std::shared_ptr<IINode> inode() const = 0;
    virtual std::shared_ptr<ISuperBlock> super_block() const = 0;
    virtual std::vector<std::shared_ptr<IDEntry>> subdirs() const = 0;
};

struct IoVector {
    std::uint64_t base;
    std::uint64_t len;
};

class IFile {
public:
    ~IFile() = default;

public:
    virtual std::uint64_t refcount() const = 0;
    virtual std::uint64_t flags() const = 0;
    virtual std::int64_t offset() const = 0;

public:  // Operations.
    virtual std::int64_t lseek(std::int64_t offset) = 0;
    virtual std::int64_t iov_read(std::vector<IoVector> iov) = 0;
    virtual std::int64_t iov_write(std::vector<IoVector> iov) = 0;
};

class VFS {
public:
    VFS();
    ~VFS();
    VFS(const VFS&) = delete;
    VFS& operator=(const VFS&) = delete;
    VFS(VFS&&) = delete;
    VFS& operator=(VFS&&) = delete;

private:
    std::vector<std::shared_ptr<ISuperBlock>> super_blocks_;
};
}  // namespace vlinux
