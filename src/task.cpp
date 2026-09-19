#include "task.h"

namespace vlinux {
Task::Task() : state(State::New) {}

Task::~Task() = default;

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;

void start_schedule() {}

void stop_schedule() {}

}  // namespace vlinux
