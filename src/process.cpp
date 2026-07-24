#include "process.h"

namespace vlinux {
Task::Task() : state_(State::New) {}

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;
}  // namespace vlinux
