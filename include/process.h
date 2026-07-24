#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mm.h"

namespace vlinux {
class Task {
public:
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

public:
    bool& root_task() { return this->root_task_; }

    std::int64_t& pid() { return this->pid_; }

    std::shared_ptr<Task> parent() const { return this->parent_; }

    std::vector<std::shared_ptr<Task>> children() const {
        return this->children_;
    }

    State& state() { return this->state_; }

    int& exit_status() { return this->exit_status_; }

private:
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
