#pragma once

#include <unicorn/unicorn.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "mm.h"
#include "vfs.h"

namespace vlinux {
struct Task {
    Task(uc_context* ctx) : ctx(ctx), state(State::New) {}
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

    // Process related fields.
    std::int64_t pid;
    std::int64_t tgid;
    std::shared_ptr<Task> parent;
    std::vector<std::shared_ptr<Task>> children;
    State state;
    int exit_status;

    // Memory related fields.
    std::uint64_t stack_top;
    std::uint64_t stack_bottom;
    VirtualMemoryAddressSpace address_space;

    // File system related fields.
    std::vector<std::shared_ptr<IFile>> files;

    // Context saved when scheduled.
    uc_context* ctx;
};

// Schedule tasks whose status is Ready.
class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

public:
    outcome::result<void> add_task(std::shared_ptr<Task> task);
    outcome::result<void> remove_task(std::shared_ptr<Task> task);
    outcome::result<void> start_schedule(std::chrono::milliseconds interval);
    outcome::result<void> stop_schedule();

public:
    enum class Status { Stopped, Running };
    Status status() const { return status_; }

private:
    // Protect the following fields.
    std::mutex mutex_;

    Status status_;
    std::jthread vcpu_;
    std::shared_ptr<Task> current_task_;
    std::deque<std::shared_ptr<Task>> ready_task_queue_;  // Ready queue.
    std::set<std::shared_ptr<Task>>
        ready_task_set_;  // For boosting find operation.
    uc_engine* uc_;
};
}  // namespace vlinux
