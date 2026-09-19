#include "task.h"

#include <algorithm>

namespace vlinux {
Task::Task() : state(State::New) {}

Task::~Task() = default;

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;

outcome::result<void> Scheduler::add_task(std::shared_ptr<Task> task) {
    auto it = this->ready_task_set_.find(task);
    if (it != this->ready_task_set_.end()) {
        return std::errc::file_exists;
    }

    this->ready_task_queue_.push_back(task);
    this->ready_task_set_.insert(task);

    return outcome::success();
}

outcome::result<void> Scheduler::remove_task(std::shared_ptr<Task> task) {
    auto set_it = this->ready_task_set_.find(task);
    if (set_it == this->ready_task_set_.end()) {
        return std::errc::no_such_file_or_directory;
    }

    auto deque_it = std::find(this->ready_task_queue_.begin(),
                              this->ready_task_queue_.end(), task);
    this->ready_task_queue_.erase(deque_it);
    this->ready_task_set_.erase(set_it);

    return outcome::success();
}

outcome::result<void> Scheduler::start_schedule(
    std::chrono::milliseconds interval) {}

outcome::result<void> Scheduler::stop_schedule() {}

}  // namespace vlinux
