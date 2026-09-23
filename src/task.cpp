#include "task.h"

#include <unicorn/unicorn.h>

#include <algorithm>
#include <future>
#include <mutex>
#include <stop_token>
#include <thread>

namespace vlinux {
Scheduler::Scheduler(uc_engine *uc) : status_(Status::Stopped), uc_(uc) {}

Scheduler::~Scheduler() { (void)this->stop_schedule(); };

outcome::result<void> Scheduler::add_task(std::shared_ptr<Task> task) {
    std::unique_lock<std::mutex> lock(this->mutex_);

    auto it = this->ready_task_set_.find(task);
    if (it != this->ready_task_set_.end()) {
        return std::errc::file_exists;
    }

    this->ready_task_queue_.push_back(task);
    this->ready_task_set_.insert(task);

    return outcome::success();
}

outcome::result<void> Scheduler::remove_task(std::shared_ptr<Task> task) {
    std::unique_lock<std::mutex> lock(this->mutex_);

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
    std::chrono::milliseconds interval, std::promise<void> &err_promise) {
    std::unique_lock<std::mutex> lock(this->mutex_);
    if (this->status_ == Status::Running) {
        return outcome::success();
    }

    this->vcpu_ = std::jthread([&, interval, this](std::stop_token st) {
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
                lock.unlock();

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

                // Set TLB fill hook for virtual address translation.
                auto pgtable = &task->page_table;
                err = ::uc_hook_add(
                    this->uc_, &this->tlb_fill_hook_, UC_HOOK_TLB_FILL,
                    reinterpret_cast<void *>(Scheduler::tlb_fill_callback),
                    pgtable, 1, 0);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("Add memory hook failed");
                }

                // Flush translation blocks.
                err = ::uc_ctl_flush_tb(this->uc_);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_ctl_flush_tb failed");
                }

                // Flush TLB.
                err = ::uc_ctl_flush_tlb(this->uc_);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_ctl_flush_tlb failed");
                }

                // Schedule the task. i.e. run it.
                task->state = Task::State::Running;
                err = ::uc_emu_start(this->uc_, rip, 0, interval.count() * 1000,
                                     0);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_emu_start failed");
                }

                // Time slice ran out, set the task to ready state.
                lock.lock();
                task->state = Task::State::Ready;
                lock.unlock();

                // Save context.
                err = ::uc_context_save(this->uc_, task->ctx);
                if (err != UC_ERR_OK) {
                    throw std::runtime_error("uc_context_save failed");
                }

                // Add the task back to the ready queue.
                lock.lock();
                this->ready_task_queue_.push_back(task);
                lock.unlock();
            } catch (...) {
                err_promise.set_exception(std::current_exception());
                this->status_ = Status::Stopped;
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
    lock.unlock();

    // Mustn't hold the lock while do these, or it will deadlock.
    this->vcpu_.request_stop();
    this->vcpu_.join();

    lock.lock();
    this->status_ = Status::Stopped;
    lock.unlock();

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
