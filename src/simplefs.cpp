#include "simplefs.h"

namespace vlinux {
namespace fs {
namespace simplefs {
class File::Impl {
public:
    Impl() = default;
    ~Impl() = default;

public:
    std::uint64_t refcount = 0;
    std::uint64_t flags = 0;
    std::uint64_t offset = 0;
    std::shared_ptr<Inode> inode;
};

File::File() = default;
File::~File() = default;

std::int64_t File::get() { return ++this->impl_->refcount; }

std::int64_t File::put() { return --this->impl_->refcount; }

std::uint64_t File::refcount() const { return this->impl_->refcount; }

// Flags.
void File::set_flags(std::uint64_t flags) { this->impl_->flags = flags; }

std::uint64_t File::flags() const { return this->impl_->flags; }

// Offset.
std::int64_t File::lseek(std::int64_t offset) {
    this->impl_->offset = offset;
    return this->impl_->offset;
}

std::int64_t File::offset() const { return this->impl_->offset; }

std::shared_ptr<IINode> File::inode() const { return this->impl_->inode; }

std::int64_t File::iov_read(std::vector<IoVector> iov) { return -1; }

std::int64_t File::iov_write(std::vector<IoVector> iov) { return -1; }

}  // namespace simplefs
}  // namespace fs
}  // namespace vlinux
