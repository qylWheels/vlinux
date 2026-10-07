#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vlinux {
class IDEntry;

class ISuperBlock {
public:
    ~ISuperBlock() = default;

public:
    virtual std::size_t block_size() const = 0;
    virtual std::uint64_t magic() const = 0;
    virtual IDEntry* root_dentry() const = 0;
};

class IINode {
public:
    ~IINode() = default;
};

class IDEntry {
public:
    ~IDEntry() = default;
};

class IFile {
public:
    ~IFile() = default;
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
