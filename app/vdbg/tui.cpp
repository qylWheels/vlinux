#include "tui.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <atomic>
#include <cstdint>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/ftxui.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace vdbg {
namespace tui {
class TUI::Impl {
public:
    Impl(vlinux::VM& vm, ::csh& cshandle)
        : screen(ftxui::ScreenInteractive::Fullscreen()),
          vm(&vm),
          cshandle(&cshandle) {}
    ~Impl() {
        if (this->worker_thread.joinable()) this->worker_thread.join();
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

public:
    void set_disassembler_content(const std::vector<std::string>& content) {
        std::lock_guard<std::mutex> lock(this->content_mutex);
        this->disasm_entries = content;
        this->disasm_selected = 0;
        return;
    }

public:
    vlinux::VM* vm;
    ::csh* cshandle;
    std::jthread worker_thread;  // Run VM in a separate thread.
    std::atomic_bool worker_thread_running{false};

public:
    std::set<std::uint64_t> breakpoints;

public:
    // Guard the shared state between the worker thread (which fills it) and
    // the TUI render thread (which reads it).
    mutable std::mutex content_mutex;
    std::vector<std::string> disasm_entries;
    int disasm_selected = 0;
    std::vector<std::string> syscall_entries;
    int syscall_selected = 0;

public:
    ftxui::ScreenInteractive screen;
    ftxui::Component container;
    ftxui::Component disassembler_list;
    ftxui::Component disassembler_area;
    ftxui::Component register_area;
    ftxui::Component syscall_list;
    ftxui::Component syscall_area;
    ftxui::Component memory_area;
    ftxui::Component help_area;
};

TUI::TUI(vlinux::VM& vm, ::csh& cshandle)
    : impl_(std::make_unique<Impl>(vm, cshandle)) {
    // Add breakpoint at entry point.
    this->impl_->breakpoints.insert(vm.get_entry_point());

    // Add instruction hook.
    auto result = vm.add_instr_hook([&](std::uint64_t addr) {
        if (this->impl_->breakpoints.find(addr) ==
            this->impl_->breakpoints.end()) {
            return;
        }

        vm.pause();

        std::lock_guard<std::mutex> lock(this->impl_->content_mutex);

        ::cs_insn* insn;
        std::string insn_str;

        // Get disassembler area height.
        auto disasm_max_lines = 100;
        this->impl_->disasm_entries.clear();

        // 16 is maximum instruction size in x86-64.
        auto code_result = vm.read_vmem(addr, 16 * disasm_max_lines);
        if (code_result.has_error()) {
            this->impl_->disasm_entries.assign(disasm_max_lines,
                                               "<Invalid Read>");
        } else {
            auto code = code_result.value();
            int count = ::cs_disasm(*this->impl_->cshandle, code.data(),
                                    code.size(), addr, disasm_max_lines, &insn);
            if (count == 0) {
                this->impl_->disasm_entries.assign(disasm_max_lines,
                                                   "<Invalid Instruction>");
            } else {
                for (int i = 0; i < count; i++) {
                    this->impl_->disasm_entries.push_back(
                        fmt::format("{:#x}: {} {}", insn[i].address,
                                    insn[i].mnemonic, insn[i].op_str));
                }
                ::cs_free(insn, count);
            }
        }
    });
    if (!result) throw std::runtime_error("Failed to add instruction hook");

    // Add syscall hook.
    result = vm.add_syscall_hook([&](std::uint64_t syscall_id,
                                     std::array<std::uint64_t, 6> args,
                                     std::uint64_t ret) {
        std::lock_guard<std::mutex> lock(this->impl_->content_mutex);
        this->impl_->syscall_entries.push_back(fmt::format(
            "<{}> ({}, {}, {}, {}, {}, {}) = {}", syscall_id, args[0], args[1],
            args[2], args[3], args[4], args[5], ret));
    });
    if (!result) throw std::runtime_error("Failed to add syscall hook");

    // Create components.
    this->impl_->disassembler_list =
        ftxui::Menu(&this->impl_->disasm_entries,
                    &this->impl_->disasm_selected) |
        ftxui::vscroll_indicator | ftxui::hscroll_indicator | ftxui::frame;
    this->impl_->disassembler_area =
        ftxui::Renderer(this->impl_->disassembler_list, [=, this]() {
            std::lock_guard<std::mutex> lock(this->impl_->content_mutex);
            auto screen_width = this->impl_->screen.dimx();
            auto screen_height = this->impl_->screen.dimy();
            return ftxui::Window({.inner = this->impl_->disassembler_list,
                                  .title = "Disassembler",
                                  .width = 2 * screen_width / 3,
                                  .height = 2 * screen_height / 3})
                ->Render();
        });

    this->impl_->register_area = ftxui::Renderer([=, this](bool focused) {
        auto screen_width = this->impl_->screen.dimx();
        auto screen_height = this->impl_->screen.dimy();
        auto window =
            ftxui::window(ftxui::text(focused ? "Register [*]" : "Register"),
                          ftxui::text("<Not Implemented Yet>") | ftxui::center,
                          focused ? ftxui::HEAVY : ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        if (focused) window |= ftxui::color(ftxui::Color::CyanLight);
        return window;
    });

    this->impl_->syscall_list = ftxui::Menu(&this->impl_->syscall_entries,
                                            &this->impl_->syscall_selected) |
                                ftxui::vscroll_indicator |
                                ftxui::hscroll_indicator | ftxui::frame;
    this->impl_->syscall_area =
        ftxui::Renderer(this->impl_->syscall_list, [=, this]() {
            std::lock_guard<std::mutex> lock(this->impl_->content_mutex);
            auto screen_width = this->impl_->screen.dimx();
            auto screen_height = this->impl_->screen.dimy();
            auto focused = this->impl_->syscall_list->Focused();
            return ftxui::Window({.inner = this->impl_->syscall_list,
                                  .title = "Syscall",
                                  .width = screen_width / 3,
                                  .height = screen_height / 3})
                ->Render();
        });

    this->impl_->memory_area = ftxui::Renderer([=, this](bool focused) {
        auto screen_width = this->impl_->screen.dimx();
        auto screen_height = this->impl_->screen.dimy();
        auto window =
            ftxui::window(ftxui::text(focused ? "Memory [*]" : "Memory"),
                          ftxui::text("<Not Implemented Yet>") | ftxui::center,
                          focused ? ftxui::HEAVY : ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width) |
            ftxui::yflex;
        if (focused) window |= ftxui::color(ftxui::Color::CyanLight);
        return window;
    });

    auto help_style = ftxui::color(ftxui::Color::Gold1) | ftxui::inverted;
    this->impl_->help_area = ftxui::Renderer([=, this](bool focused) {
        return ftxui::hbox(
                   {ftxui::text("q: Exit") | help_style, ftxui::filler(),
                    ftxui::text("s: Step Into") | help_style, ftxui::filler(),
                    ftxui::text("n: Step Over") | help_style, ftxui::filler(),
                    ftxui::text("c: Continue") | help_style}) |
               ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);
    });

    this->impl_->container = ftxui::Container::Vertical({
        ftxui::Container::Horizontal(
            {this->impl_->disassembler_area,
             ftxui::Container::Vertical(
                 {this->impl_->register_area, this->impl_->syscall_area})}),
        this->impl_->memory_area,
        this->impl_->help_area,
    });

    this->impl_->container |= ftxui::CatchEvent([&](ftxui::Event e) {
        if (e == ftxui::Event::Character('q')) {
            this->impl_->screen.ExitLoopClosure()();
            return true;
        }
        if (e == ftxui::Event::Character('c')) {
            if (!this->impl_->worker_thread_running) {
                this->impl_->worker_thread = std::jthread([this] {
                    auto result =
                        this->impl_->vm->run(std::chrono::milliseconds(100000),
                                             std::chrono::milliseconds(100));
                });
                this->impl_->worker_thread_running = true;
            }
            return true;
        }
        return false;
    });
}

TUI::~TUI() = default;

void TUI::show() {
    this->impl_->screen.Loop(this->impl_->container);

    return;
}
}  // namespace tui
}  // namespace vdbg