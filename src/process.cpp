#include "process.h"

namespace vlinux {
Task::Task() : state(State::New) {}

Task::~Task() = default;

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;
}  // namespace vlinux
