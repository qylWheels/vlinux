#include "task.h"

#include <unicorn/unicorn.h>

#include <algorithm>

#include "error.h"

namespace vlinux {
Scheduler::Scheduler(uc_engine *uc) : status_(Status::Stopped), uc_(uc) {}

Scheduler::~Scheduler() = default;

outcome::result<void> Scheduler::add_task(std::shared_ptr<Task> task) {
    if (this->status_ == Status::Running) {
        return std::errc::device_or_resource_busy;
    }

    return this->add_task_running(task);
}

outcome::result<void> Scheduler::add_task_running(std::shared_ptr<Task> task) {
    auto it = this->ready_task_set_.find(task);
    if (it != this->ready_task_set_.end()) {
        return std::errc::file_exists;
    }

    this->ready_task_queue_.push_back(task);
    this->ready_task_set_.insert(task);

    return outcome::success();
}

outcome::result<void> Scheduler::remove_task(std::shared_ptr<Task> task) {
    if (this->status_ == Status::Running) {
        return std::errc::device_or_resource_busy;
    }

    return this->remove_task_running(task);
}

outcome::result<void> Scheduler::remove_task_running(
    std::shared_ptr<Task> task) {
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

std::shared_ptr<Task> Scheduler::current_task() { return this->current_task_; }

outcome::result<void> Scheduler::start_schedule(
    std::chrono::milliseconds timeout, std::chrono::milliseconds interval) {
    if (this->status_ == Status::Running) {
        return outcome::success();
    }

    // Set start and end time.
    this->start_time_ = std::chrono::steady_clock::now();
    this->end_time_ = this->start_time_ + timeout;

    // Set status to running.
    this->status_ = Status::Running;

    while (true) {
        uc_err err;

        // Check timeout.
        if (std::chrono::steady_clock::now() >= this->end_time_) {
            break;
        }

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
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Get the RIP of the task.
        std::uint64_t rip;
        err = ::uc_context_reg_read(task->ctx, UC_X86_REG_RIP, &rip);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Set TLB fill hook for virtual address translation.
        auto pgtable = task->page_table.get();
        err = ::uc_hook_add(
            this->uc_, &this->tlb_fill_hook_, UC_HOOK_TLB_FILL,
            reinterpret_cast<void *>(Scheduler::tlb_fill_callback), pgtable, 1,
            0);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Flush translation blocks.
        err = ::uc_ctl_flush_tb(this->uc_);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Flush TLB.
        err = ::uc_ctl_flush_tlb(this->uc_);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Schedule the task. i.e. run it.
        task->state = Task::State::Running;
        err = ::uc_emu_start(this->uc_, rip, 0, interval.count() * 1000, 0);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Time slice ran out, set the task to ready state.
        task->state = Task::State::Ready;

        // Remove hook.
        err = ::uc_hook_del(this->uc_, this->tlb_fill_hook_);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Save context.
        err = ::uc_context_save(this->uc_, task->ctx);
        if (err != UC_ERR_OK) {
            this->status_ = Status::Stopped;
            return make_error_code(err);
        }

        // Add the task back to the ready queue.
        this->ready_task_queue_.push_back(task);
    }

    // Set status to stopped.
    this->status_ = Status::Stopped;

    return outcome::success();
}

bool Scheduler::tlb_fill_callback(uc_engine *uc, uint64_t vaddr,
                                  uc_mem_type type, uc_tlb_entry *result,
                                  void *user_data) {
    auto pgtable = static_cast<mm::PageTable *>(user_data);

    auto pa = pgtable->va_to_pa(vaddr);
    if (!pa.has_value()) {
        return false;
    }

    auto va_desc = pgtable->va_to_desc(vaddr);
    if (!va_desc.has_value()) {
        return false;
    }

    result->paddr = pa.value();

    switch (type) {
        case UC_MEM_READ: {
            if (va_desc.value()->perm & UC_PROT_READ) {
                result->perms = UC_PROT_READ;
            } else {
                return false;
            }
            break;
        }
        case UC_MEM_WRITE: {
            if (va_desc.value()->perm & UC_PROT_WRITE) {
                result->perms = UC_PROT_WRITE;
            } else {
                return false;
            }
            break;
        }
        case UC_MEM_FETCH: {
            if (va_desc.value()->perm & UC_PROT_EXEC) {
                result->perms = UC_PROT_EXEC;
            } else {
                return false;
            }
            break;
        }
        default: {
            std::abort();  // Unreachable.
        }
    }

    return true;
}
}  // namespace vlinux
