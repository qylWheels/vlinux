#pragma once

#include <memory>
#include <vector>

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
    std::shared_ptr<Task> parent() const { return this->parent_; }

    std::vector<std::shared_ptr<Task>> children() const {
        return this->children_;
    }

    State state() const { return this->state_; }

private:
    std::shared_ptr<Task> parent_;
    std::vector<std::shared_ptr<Task>> children_;
    State state_;
};
}  // namespace vlinux
