#pragma once

#include <memory>
#include <vector>

namespace vlinux {
class ISuperBlock {
public:
    ~ISuperBlock() = default;
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
