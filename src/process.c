#include "process.h"

namespace vlinux {
Task::Task() : state_(State::New) {}

Task::~Task() = default;
}  // namespace vlinux
