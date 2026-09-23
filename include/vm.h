#pragma once

#include <unicorn/unicorn.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <outcome.hpp>
#include <outcome/result.hpp>
#include <set>

#include "mm.h"
#include "task.h"

namespace outcome = OUTCOME_V2_NAMESPACE;

namespace vlinux {
struct TaskInitializer {
public:
    outcome::result<void> init();

public:
    std::function<outcome::result<uc_context*>()> create_task_ctx;
    std::function<outcome::result<void>(uc_context*, std::uint64_t rip)>
        setup_task_ctx;
    std::function<outcome::result<void>(uc_context*)>
        add_task_ctx_to_context_manager;
    std::function<outcome::result<std::shared_ptr<Task>>()> create_task;
    std::function<outcome::result<void>(std::shared_ptr<Task>)>
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

private:
    static void syscall_hook_callback(uc_engine* engine, void* user_data);

private:
    std::uint64_t entrypoint_;

    // Task-related fields.
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
