#pragma once

#include <chrono>
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
class IFile;

class ISuperBlock {
public:
    virtual ~ISuperBlock() = default;

public:
    // Basic information.
    virtual std::size_t block_size() const = 0;
    virtual std::uint64_t magic() const = 0;

    // Inode operations.
    virtual outcome::result<std::shared_ptr<IINode>> alloc_inode(
        std::shared_ptr<ISuperBlock>) = 0;
    virtual void destroy_inode(std::shared_ptr<IINode> inode) = 0;
};

class IINode {
public:
    virtual ~IINode() = default;

public:
    // ID.
    virtual std::uint64_t id() const = 0;

    // Size.
    virtual std::uint64_t block_count() const = 0;

    // Type and permissions.
    virtual std::uint64_t type() const = 0;
    virtual outcome::result<void> set_permissions(
        std::uint64_t permissions) = 0;
    virtual std::uint64_t permissions() const = 0;

    // Timestamp.
    virtual std::chrono::time_point<std::chrono::system_clock> atime()
        const = 0;
    virtual std::chrono::time_point<std::chrono::system_clock> mtime()
        const = 0;
    virtual std::chrono::time_point<std::chrono::system_clock> ctime()
        const = 0;

    // References.
    virtual std::uint64_t mem_refcount() const = 0;
    virtual std::uint64_t disk_refcount() const = 0;

    // Super block.
    virtual std::weak_ptr<ISuperBlock> super_block() const = 0;

    // Lookup inode of subdirectory item.
    virtual outcome::result<std::shared_ptr<IINode>> lookup(
        std::string name) = 0;

    // File.
    virtual outcome::result<std::shared_ptr<IFile>> open() = 0;
};

struct IoVector {
    std::uint64_t base;
    std::uint64_t len;
};

class IFile {
public:
    virtual ~IFile() = default;

public:  // Operations.
    // References.
    virtual outcome::result<void> get() = 0;
    virtual outcome::result<void> put() = 0;
    virtual std::uint64_t refcount() const = 0;

    // Flags.
    virtual outcome::result<void> set_flags(std::uint64_t flags) = 0;
    virtual std::uint64_t flags() const = 0;

    // Offset.
    virtual outcome::result<void> set_offset(std::uint64_t offset) = 0;
    virtual std::uint64_t offset() const = 0;

    // Inode.
    virtual outcome::result<void> set_inode(std::shared_ptr<IINode> inode) = 0;
    virtual std::shared_ptr<IINode> inode() const = 0;

    virtual outcome::result<std::size_t> iov_read(
        std::vector<IoVector> iov) = 0;
    virtual outcome::result<std::size_t> iov_write(
        std::vector<IoVector> iov) = 0;
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
