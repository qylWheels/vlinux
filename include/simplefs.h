#pragma once

#include "vfs.h"
namespace vlinux {
namespace fs {
namespace simplefs {
class FS : public VFS {
public:
    FS();
    ~FS();
    FS(const FS&) = delete;
    FS& operator=(const FS&) = delete;
    FS(FS&&) = delete;
    FS& operator=(FS&&) = delete;

public:
    virtual std::size_t block_size() const { return 4096; }
    virtual std::uint64_t magic() const { return 0x1919810A; }
    virtual std::shared_ptr<IDEntry> root_dentry() const;

public:  // Operations.
    virtual std::shared_ptr<IINode> alloc_inode();
    virtual void destroy_inode(std::shared_ptr<IINode> inode);
};

class Inode : public IINode {
public:
    Inode();
    ~Inode();
    Inode(const Inode&) = delete;
    Inode& operator=(const Inode&) = delete;
    Inode(Inode&&) = delete;
    Inode& operator=(Inode&&) = delete;

public:
    virtual std::uint64_t mem_refcount() const;
    virtual std::uint64_t disk_refcount() const;
    virtual std::uint64_t mode() const;
    virtual std::shared_ptr<ISuperBlock> super_block() const;
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
    virtual std::shared_ptr<IDEntry> parent() const;
    virtual std::string name() const;
    virtual std::shared_ptr<IINode> inode() const;
    virtual std::shared_ptr<ISuperBlock> super_block() const;
    virtual std::vector<std::shared_ptr<IDEntry>> subdirs() const;
};

class File : public IFile {
public:
    File();
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = delete;
    File& operator=(File&&) = delete;

public:
    virtual std::uint64_t refcount() const;
    virtual std::uint64_t flags() const;
    virtual std::int64_t offset() const;

public:  // Operations.
    virtual std::int64_t lseek(std::int64_t offset);
    virtual std::int64_t iov_read(std::vector<IoVector> iov);
    virtual std::int64_t iov_write(std::vector<IoVector> iov);
};
}  // namespace simplefs
}  // namespace fs
}  // namespace vlinux
