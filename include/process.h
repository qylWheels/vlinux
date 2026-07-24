#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mm.h"

namespace vlinux {
struct Task {
    enum class State {
        New,
        Ready,
        Running,
        Waiting,  // Waiting for I/O completion.
        Stopped,
    };

    // Whether this task is the root task. i.e. the task
    // we run directly in vlinux.
    bool root_task_;

    std::int64_t pid_;
    std::shared_ptr<Task> parent_;
    std::vector<std::shared_ptr<Task>> children_;
    State state_;
    int exit_status_;
};

class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

private:
    std::shared_ptr<Task> task_tree_root_;
};
}  // namespace vlinux
