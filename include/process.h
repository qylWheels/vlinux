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

    struct Context {
        struct Registers {
            std::uint64_t rsp;
            std::uint64_t rbp;
            std::uint64_t rip;

            std::uint64_t rax, rbx, rcx, rdx;
            std::uint64_t rsi, rdi;
            std::uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

            std::uint64_t rflags;
        };
        Registers regs;
        PageTable page_table;
    };

public:
    std::int64_t pid() const { return this->pid_; }

    std::shared_ptr<Task> parent() const { return this->parent_; }

    std::vector<std::shared_ptr<Task>> children() const {
        return this->children_;
    }

    State& state() { return this->state_; }

    Context& context() { return this->context_; }

    int exit_status() const { return this->exit_status_; }

private:
    std::int64_t pid_;
    std::shared_ptr<Task> parent_;
    std::vector<std::shared_ptr<Task>> children_;
    State state_;
    Context context_;
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
