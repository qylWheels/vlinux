#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <outcome/config.hpp>
#include <outcome/outcome.hpp>
#include <vector>

namespace outcome = OUTCOME_V2_NAMESPACE;

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

public:  // Operations.
    // References.
    virtual outcome::result<void> get();
    virtual outcome::result<void> put();
    virtual std::uint64_t refcount() const;

    // Flags.
    virtual outcome::result<void> set_flags(std::uint64_t flags);
    virtual std::uint64_t flags() const;

    // Offset.
    virtual outcome::result<void> set_offset(std::uint64_t offset);
    virtual std::uint64_t offset() const;

    virtual std::shared_ptr<IINode> inode() const;
    virtual outcome::result<void> iov_read(std::vector<IoVector> iov);
    virtual outcome::result<void> iov_write(std::vector<IoVector> iov);
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
