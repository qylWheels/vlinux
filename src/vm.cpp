#include "vm.h"

#include <unicorn/unicorn.h>
#include <unicorn/x86.h>

#include <elfio/elfio.hpp>
#include <format>
#include <fstream>
#include <limits>
#include <outcome.hpp>
#include <system_error>

#include "error.h"
#include "mm.h"

namespace vlinux {
VM::VM() {
    uc_err result;
    result = uc_open(UC_ARCH_X86, UC_MODE_64, &this->engine_);
    if (result != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_open failed: {}", uc_strerror(result)));
    }
}

VM::~VM() { uc_close(this->engine_); }

void VM::reset() {}

outcome::result<void> VM::load(const std::filesystem::path& path) noexcept {
    uc_err err;
    ELFIO::elfio reader;

    // Create task.
    this->curr_task_ = std::make_shared<Task>(nullptr);
    this->curr_task_->state = Task::State::New;
    this->curr_task_->root_task = true;
    this->curr_task_->pid = 1145;  // TODO: Generate a unique PID.

    if (!reader.load(path)) {
        return std::make_error_code(std::errc::io_error);
    }

    for (const auto& segment : reader.segments) {
        if (segment->get_type() != ELFIO::PT_LOAD) {
            // We only care about LOAD segments.
            continue;
        }

        auto file_offset = segment->get_offset();
        auto file_size = segment->get_file_size();
        auto virtual_address = segment->get_virtual_address();
        auto virtual_size = segment->get_memory_size();

        // Read the segment data from the file.
        std::vector<char> data;
        std::ifstream file(path.string(), std::ios::binary);
        file.seekg(file_offset);
        data.resize(file_size);
        file.read(data.data(), file_size);
        file.close();

        // Create memory mapping.
        std::uint32_t perms = 0;
        auto segment_flags = segment->get_flags();
        if (segment_flags & ELFIO::PF_R) {
            perms |= UC_PROT_READ;
        }
        if (segment_flags & ELFIO::PF_W) {
            perms |= UC_PROT_WRITE;
        }
        if (segment_flags & ELFIO::PF_X) {
            perms |= UC_PROT_EXEC;
        }
        err = uc_mem_map(
            this->engine_, virtual_address,
            (virtual_size + mm::PAGE_SIZE - 1) & ~(mm::PAGE_SIZE - 1), perms);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }

        // Write the segment data to the memory.
        err = uc_mem_write(this->engine_, virtual_address, data.data(),
                           file_size);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
        std::vector<char> zeros(virtual_size - file_size);
        err = uc_mem_write(this->engine_, virtual_address + file_size,
                           zeros.data(), virtual_size - file_size);
        if (err != UC_ERR_OK) {
            return make_error_code(err);
        }
    }

    // Set the entrypoint.
    this->entrypoint_ = reader.get_entry();

    // Set the state to Ready.
    this->curr_task_->state = Task::State::Ready;

    return outcome::success();
}

outcome::result<void> VM::run() noexcept {
    uc_err err;

    // Add syscall hook.
    err = uc_hook_add(this->engine_, &this->syscall_hook_, UC_HOOK_INSN,
                      reinterpret_cast<void*>(VM::syscall_hook_callback), this,
                      0, std::numeric_limits<std::uint64_t>::max(),
                      UC_X86_INS_SYSCALL);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Setup stack.
    OUTCOME_TRY(std::uint64_t stack_bottom_pa, this->ppa_.alloc());
    std::uint64_t stack_bottom_va = 0xf000'0000;
    std::uint64_t stack_top_va = stack_bottom_va + mm::PAGE_SIZE;
    err = uc_mem_map_ptr(this->engine_, stack_bottom_va, mm::PAGE_SIZE,
                         UC_PROT_READ | UC_PROT_WRITE,
                         reinterpret_cast<void*>(stack_bottom_pa));
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }
    err = uc_reg_write(this->engine_, UC_X86_REG_RSP, &stack_top_va);
    if (err != UC_ERR_OK) {
        return make_error_code(err);
    }

    // Set state to Running.
    this->curr_task_->state = Task::State::Running;

    // Run!
    err = uc_emu_start(this->engine_, this->entrypoint_, 0, 0, 0);
    if (err != UC_ERR_OK) {
        std::uint64_t rip;
        uc_reg_read(this->engine_, UC_X86_REG_RIP, &rip);
        std::cout << std::format("rip: {:#x}", rip) << std::endl;
        return make_error_code(err);
    }

    return outcome::success();
}

void VM::exit(int status) {
    this->curr_task_->exit_status = status;
    this->curr_task_->state = Task::State::Stopped;
    if (this->curr_task_->root_task) {
        uc_emu_stop(this->engine_);
    }
}

void VM::syscall_hook_callback(uc_engine* engine, void* user_data) {
    VM* self = reinterpret_cast<VM*>(user_data);
    std::uint64_t syscall_number;
    std::uint64_t args[6];
    std::uint64_t* argptrs[6] = {&args[0], &args[1], &args[2],
                                 &args[3], &args[4], &args[5]};
    int argregs[] = {UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX,
                     UC_X86_REG_R10, UC_X86_REG_R8,  UC_X86_REG_R9};
    std::uint64_t ret;
    uc_err err;

    // Read syscall number.
    err = uc_reg_read(engine, UC_X86_REG_RAX, &syscall_number);
    if (err != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_reg_read failed: {}", uc_strerror(err)));
    }
    std::cout << std::format("syscall: {}", syscall_number) << std::endl;

    // Read syscall arguments.
    err = uc_reg_read_batch(engine, argregs, reinterpret_cast<void**>(argptrs),
                            6);
    if (err != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_reg_read_batch failed: {}", uc_strerror(err)));
    }
    std::cout << std::format("args: {}, {}, {}, {}, {}, {}", *argptrs[0],
                             *argptrs[1], *argptrs[2], *argptrs[3], *argptrs[4],
                             *argptrs[5])
              << std::endl;

    // Dispatch.
    switch (syscall_number) {
        case 60: {
            self->exit(args[0]);
            break;
        }
        default: {
            std::cout << std::format("syscall {} not implemented",
                                     syscall_number)
                      << std::endl;
            break;
        }
    }
}
}  // namespace vlinux
