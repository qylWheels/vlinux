#include "task.h"

#include <unicorn/unicorn.h>

#include <algorithm>
#include <future>
#include <mutex>
#include <stop_token>
#include <thread>

namespace vlinux {
Scheduler::Scheduler(uc_engine* uc) : status_(Status::Stopped), uc_(uc) {}

Scheduler::~Scheduler() {
    (void)this->stop_schedule();
    ::uc_close(this->uc_);
};

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
    std::chrono::milliseconds interval) {
    std::unique_lock<std::mutex> lock(this->mutex_);
    if (this->status_ == Status::Running) {
        return outcome::success();
    }
    lock.unlock();

    this->vcpu_ = std::jthread([&, this](std::stop_token st,
                                         std::promise<void> err_promise) {
        while (!st.stop_requested()) {
            try {
                std::unique_lock<std::mutex> lock(this->mutex_);
                uc_err err;

                if (this->ready_task_queue_.empty()) {
                    continue;
                }

                // Select a task.
                auto task = this->ready_task_queue_.front();
                this->ready_task_queue_.pop_front();
                this->current_task_ = task;

                // Restore the context of the task.
                err = ::uc_context_restore(this->uc_, task->ctx);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_context_restore failed");
                }

                // Get the RIP of the task.
                std::uint64_t rip;
                err = ::uc_context_reg_read(task->ctx, UC_X86_REG_RIP, &rip);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_context_reg_read failed");
                }

                // Schedule the task. i.e. run it.
                task->state = Task::State::Running;
                err = ::uc_emu_start(this->uc_, rip, 0, interval.count() * 1000,
                                     0);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_emu_start failed");
                }

                // Time slice ran out.
                task->state = Task::State::Ready;
                this->ready_task_queue_.push_back(task);
            } catch (...) {
                err_promise.set_exception(std::current_exception());
                break;
            }
        }
    });

    this->status_ = Status::Running;

    return outcome::success();
}

outcome::result<void> Scheduler::stop_schedule() {
    std::unique_lock<std::mutex> lock(this->mutex_);
    if (this->status_ == Status::Stopped) {
        return outcome::success();
    }

    this->vcpu_.request_stop();
    this->vcpu_.join();
    this->status_ = Status::Stopped;
    return outcome::success();
}

}  // namespace vlinux
