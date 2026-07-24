#include "process.h"

namespace vlinux {
Task::Task() : state_(State::New) {}

Task::~Task() = default;

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;
}  // namespace vlinux
