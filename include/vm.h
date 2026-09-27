#pragma once

#include <unicorn/unicorn.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <outcome.hpp>
#include <outcome/result.hpp>
#include <set>
#include <string>

#include "mm.h"
#include "task.h"

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vlinux {
struct TaskInitializer {
public:
    outcome::result<std::shared_ptr<Task>> init_task(
        std::uint64_t rip, std::string name, bool root_task, std::int64_t pid,
        std::int64_t tgid, std::shared_ptr<Task> parent,
        std::vector<std::shared_ptr<Task>> children, Task::State state,
        std::uint64_t stack_top, std::uint64_t stack_bottom,
        std::shared_ptr<mm::VirtualMemoryAddressSpace> address_space,
        std::shared_ptr<mm::PageTable> page_table) {
        OUTCOME_TRY(uc_context * ctx, create_task_ctx());
        OUTCOME_TRY(setup_task_ctx(ctx, rip));
        OUTCOME_TRY(add_task_ctx_to_context_manager(ctx));
        OUTCOME_TRY(auto task, create_task(ctx));
        OUTCOME_TRY(setup_task_properties(
            task, name, root_task, pid, tgid, parent, children, state,
            stack_top, stack_bottom, address_space, page_table));
        OUTCOME_TRY(add_task_to_task_manager(task));
        OUTCOME_TRY(add_task_to_scheduler(task));

        return outcome::success(task);
    }

public:
    std::function<outcome::result<uc_context*>()> create_task_ctx;
    std::function<outcome::result<void>(uc_context*, std::uint64_t rip)>
        setup_task_ctx;
    std::function<outcome::result<void>(uc_context*)>
        add_task_ctx_to_context_manager;
    std::function<outcome::result<std::shared_ptr<Task>>(uc_context* ctx)>
        create_task;
    std::function<outcome::result<void>(
        std::shared_ptr<Task> task, std::string name, bool root_task,
        std::int64_t pid, std::int64_t tgid, std::shared_ptr<Task> parent,
        std::vector<std::shared_ptr<Task>> children, Task::State state,
        std::uint64_t stack_top, std::uint64_t stack_bottom,
        std::shared_ptr<mm::VirtualMemoryAddressSpace> address_space,
        std::shared_ptr<mm::PageTable> page_table)>
        setup_task_properties;
    std::function<outcome::result<void>(std::shared_ptr<Task>)>
        add_task_to_task_manager;
    std::function<outcome::result<void>(std::shared_ptr<Task>)>
        add_task_to_scheduler;
};

class VM {
public:
    VM();
    ~VM();
    VM(const VM& other) = delete;
    VM& operator=(const VM& other) = delete;
    VM(VM&& other) = delete;
    VM& operator=(VM&& other) = delete;

public:
    void reset();
    outcome::result<void> load(const std::filesystem::path& path) noexcept;
    outcome::result<void> run() noexcept;

private:
    outcome::result<void> setup_idle_and_init_task();

    // Syscalls.
public:
    void exit(int status);

    // brk() syscall.
    void* brk(void* addr);
    void* brk_expand(std::shared_ptr<Task> task, void* addr);
    void* brk_shrink(std::shared_ptr<Task> task, void* addr);

private:
    static void syscall_hook_callback(uc_engine* engine, void* user_data);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t entrypoint_;

    // Error-related fields.
    std::promise<void> err_promise_;

    // Task-related fields.
    std::uint8_t* infinite_loop_code_;
    TaskInitializer task_initializer_;
    PidManager pid_manager_;
    std::set<uc_context*> contexts_;
    std::set<std::shared_ptr<Task>> tasks_;
    std::shared_ptr<Scheduler> scheduler_;

    // Memory-related fields.
    std::shared_ptr<mm::PhysicalPageAllocator> ppa_;

    uc_engine* uc_;
    uc_hook syscall_hook_;
    std::shared_ptr<Task> curr_task_;
};
}  // namespace vlinux
