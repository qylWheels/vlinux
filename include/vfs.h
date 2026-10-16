#pragma once

#include <unicorn/unicorn.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <outcome/config.hpp>
#include <outcome/outcome.hpp>
#include <stack>
#include <variant>
#include <vector>

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vlinux {
class ISuperBlock;
class File;

class IINode {
public:
    virtual ~IINode() = default;

public:
    enum class Type {
        File,
        Directory,
        Link,
        Device,
    };

    // Normal file inode.
    using NormalFileContent = std::vector<std::uint8_t>;

    // Directory item for directory inode.
    struct DirItem {
        std::string name;
        std::shared_ptr<IINode> inode;
    };
    using DirectoryFileContent = std::vector<DirItem>;

    using Content = std::variant<NormalFileContent, DirectoryFileContent>;

public:
    // ID.
    virtual std::uint64_t id() const = 0;

    // Size.
    virtual std::uint64_t block_count() const = 0;

    // Type and permissions.
    virtual Type type() const = 0;
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
    virtual outcome::result<void> inc_mem_refcount() = 0;
    virtual outcome::result<void> dec_mem_refcount() = 0;
    virtual std::uint64_t mem_refcount() const = 0;
    virtual std::uint64_t disk_refcount() const = 0;

    // Super block.
    virtual std::weak_ptr<ISuperBlock> super_block() const = 0;

    // Lookup inode of subdirectory item.
    virtual outcome::result<std::shared_ptr<IINode>> lookup(
        std::string name) = 0;

    // File data.
    virtual outcome::result<Content*> data() = 0;
};

class ISuperBlock {
public:
    virtual ~ISuperBlock() = default;

public:
    // Basic information.
    virtual std::size_t block_size() const = 0;
    virtual std::uint64_t magic() const = 0;

    // Inode operations.
    virtual outcome::result<std::shared_ptr<IINode>> alloc_inode(
        std::shared_ptr<ISuperBlock> sb, IINode::Type type) = 0;
    virtual void destroy_inode(std::shared_ptr<IINode> inode) = 0;
};

struct IoVector {
    std::uint64_t base;
    std::uint64_t len;
};

class MountInfo {
public:
    MountInfo();
    ~MountInfo();
    MountInfo(const MountInfo&) = delete;
    MountInfo& operator=(const MountInfo&) = delete;
    MountInfo(MountInfo&&) = delete;
    MountInfo& operator=(MountInfo&&) = delete;

public:
    outcome::result<void> mount(std::filesystem::path path,
                                std::shared_ptr<IINode> fs_root_inode) {
        this->mount_maps_[path].push(fs_root_inode);
        return outcome::success();
    }

    outcome::result<void> unmount(std::filesystem::path path) {
        auto it = this->mount_maps_.find(path);
        if (it == this->mount_maps_.end()) {
            return outcome::success();
        }

        if (it->second.empty()) {
            return outcome::success();
        }

        it->second.pop();
        if (it->second.empty()) {
            this->mount_maps_.erase(it);
        }

        return outcome::success();
    }

    std::optional<std::shared_ptr<IINode>> curr_mount_of(
        std::filesystem::path path) {
        auto it = this->mount_maps_.find(path);
        if (it == this->mount_maps_.end()) {
            return std::nullopt;
        }
        if (it->second.empty()) {
            return std::nullopt;
        }
        return it->second.top();
    }

private:
    std::map<std::filesystem::path, std::stack<std::shared_ptr<IINode>>>
        mount_maps_;
};

class File {
public:
    File(std::shared_ptr<IINode> inode, uc_engine* uc)
        : inode_(inode), uc_(uc) {}
    ~File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = delete;
    File& operator=(File&&) = delete;

public:
    // References.
    outcome::result<void> get() {
        this->refcount_++;
        return outcome::success();
    }
    outcome::result<void> put() {
        if (this->refcount_ == 0) {
            return std::errc::invalid_argument;
        }
        this->refcount_--;
        return outcome::success();
    }
    std::uint64_t refcount() const { return this->refcount_; }

    // Flags.
    outcome::result<void> set_flags(std::uint64_t flags) {
        this->flags_ = flags;
        return outcome::success();
    }
    std::uint64_t flags() const { return this->flags_; }

    // Offset.
    outcome::result<void> set_offset(std::uint64_t offset) {
        this->offset_ = offset;
        return outcome::success();
    }
    std::uint64_t offset() const { return this->offset_; }

    // Inode.
    std::shared_ptr<IINode> inode() const { return this->inode_; }

    // Path.
    outcome::result<void> set_path(std::filesystem::path path) {
        this->path_ = path;
        return outcome::success();
    }
    std::filesystem::path path() const { return this->path_; }

    outcome::result<std::size_t> iov_read(std::vector<IoVector> iov);
    outcome::result<std::size_t> iov_write(std::vector<IoVector> iov);

private:
    uc_engine* uc_;
    std::uint64_t refcount_ = 0;
    std::uint64_t flags_ = 0;
    std::uint64_t offset_ = 0;
    std::shared_ptr<IINode> inode_;
    std::filesystem::path path_;
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
