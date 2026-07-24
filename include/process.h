#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mm.h"

namespace vlinux {
struct Task {
    Task();
    ~Task();
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;

public:
    enum class State {
        New,
        Ready,
        Running,
        Waiting,  // Waiting for I/O completion.
        Stopped,
    };

    // Whether this task is the root task. i.e. the task
    // we run directly in vlinux.
    bool root_task;

    std::int64_t pid;
    std::shared_ptr<Task> parent;
    std::vector<std::shared_ptr<Task>> children;
    State state;
    int exit_status;
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
