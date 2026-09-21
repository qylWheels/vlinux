#pragma once

#include <unicorn/unicorn.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "mm.h"
#include "vfs.h"

namespace vlinux {
class PidManager {
public:
    PidManager() = default;
    ~PidManager() = default;
    PidManager(const PidManager&) = delete;
    PidManager& operator=(const PidManager&) = delete;
    PidManager(PidManager&&) = delete;
    PidManager& operator=(PidManager&&) = delete;

public:
    static const std::size_t kMaxPid = 4096;

public:
    outcome::result<std::uint64_t> alloc_pid() {
        if (this->pid_using_.size() >= kMaxPid) {
            return std::errc::resource_unavailable_try_again;
        }

        for (std::uint64_t i = 0; i < kMaxPid; ++i) {
            auto pid = (this->next_pid_ + i) % kMaxPid;
            if (this->pid_using_.find(pid) == this->pid_using_.end()) {
                this->pid_using_.insert(pid);
                this->next_pid_ = pid + 1;
                return pid;
            }
        }

        std::abort();  // Unreachable.
    }

    outcome::result<void> free_pid(std::uint64_t pid) {
        if (this->pid_using_.find(pid) == this->pid_using_.end()) {
            return outcome::success();
        }
        this->pid_using_.erase(pid);
        return outcome::success();
    }

private:
    std::set<std::uint64_t> pid_using_;
    std::uint64_t next_pid_ = 2;
};

struct Task {
    Task(uc_context* ctx) : ctx(ctx), state(State::New) {}
    ~Task() = default;
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
    std::string name;

    // Whether this task is the root task, whose PID is 0.
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
    mm::VirtualMemoryAddressSpace address_space;

    // File system related fields.
    std::vector<std::shared_ptr<IFile>> files;

    // Context saved when scheduled.
    uc_context* ctx;
};

// Schedule tasks whose status is Ready.
class Scheduler {
public:
    Scheduler(uc_engine* uc);
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

public:
    outcome::result<void> add_task(std::shared_ptr<Task> task);
    outcome::result<void> remove_task(std::shared_ptr<Task> task);
    outcome::result<void> start_schedule(std::chrono::milliseconds interval,
                                         std::promise<void>& err_promise);
    outcome::result<void> stop_schedule();

public:
    enum class Status { Stopped, Running };
    Status status() const { return status_; }

private:
    uc_engine* uc_;

    // Protect the following fields.
    std::mutex mutex_;

    Status status_;
    std::jthread vcpu_;
    std::shared_ptr<Task> current_task_;
    std::deque<std::shared_ptr<Task>> ready_task_queue_;  // Ready queue.
    std::set<std::shared_ptr<Task>>
        ready_task_set_;  // For boosting find operation.
};
}  // namespace vlinux
