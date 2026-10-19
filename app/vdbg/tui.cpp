#include "tui.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <atomic>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/ftxui.hpp>
#include <ftxui/screen/screen.hpp>
#include <functional>
#include <memory>
#include <mutex>
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
          cshandle(&cshandle),
          disassembler_area_content(
              ftxui::Menu(&disasm_entries, &disasm_selected)) {}
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
    // Guard the shared state between the worker thread (which fills it) and
    // the TUI render thread (which reads it).
    mutable std::mutex content_mutex;
    std::vector<std::string> disasm_entries;
    int disasm_selected = 0;
    std::vector<std::string> syscall_entries;
    int syscall_selected = 0;

public:
    ftxui::ScreenInteractive screen;
    ftxui::Component disassembler_area_content;
    ftxui::Component disassembler_area;
    ftxui::Component register_area_content = ftxui::Menu({});
    ftxui::Component register_area;
    ftxui::Component syscall_area_content = ftxui::Menu({});
    ftxui::Component syscall_area;
    ftxui::Component memory_area_content = ftxui::Menu({});
    ftxui::Component memory_area;
    ftxui::Component help_area;

public:
    std::map<int, ftxui::Component> id_to_component{
        {0, disassembler_area_content},
        {1, register_area_content},
        {2, syscall_area_content},
        {3, memory_area_content}};
    int current_component_id = 0;
};

TUI::TUI(vlinux::VM& vm, ::csh& cshandle)
    : impl_(std::make_unique<Impl>(vm, cshandle)) {
    // Add instruction hook.
    auto result = vm.add_instr_hook([&](std::uint64_t addr) {
        ::cs_insn* insn;
        std::string insn_str;
        // 16 is maximum instruction size in x86-64.
        auto code_result = vm.read_mem(addr, 16);
        if (code_result.has_error()) {
            insn_str = "<Invalid Instruction>";
        } else {
            auto code = code_result.value();
            int count = ::cs_disasm(*this->impl_->cshandle, code.data(),
                                    code.size(), addr, 1, &insn);
            if (count == 0)
                insn_str = "<Invalid Instruction>";
            else
                insn_str = fmt::format("{} {}", insn->mnemonic, insn->op_str);
            ::cs_free(insn, 1);
        }
        insn_str = fmt::format("{:#x}: {}", addr, insn_str);
        this->impl_->set_disassembler_content(
            std::vector<std::string>{insn_str});
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
}

TUI::~TUI() = default;

void TUI::show() {
    auto layout = ftxui::Renderer([this] {
        auto screen_width = this->impl_->screen.dimx();
        auto screen_height = this->impl_->screen.dimy();

        std::lock_guard<std::mutex> lock(this->impl_->content_mutex);

        this->impl_->disassembler_area =
            ftxui::Renderer([=, this](bool focused) {
                return ftxui::window(
                           ftxui::text("Disassembler"),
                           this->impl_->disassembler_area_content->Render(),
                           focused ? ftxui::HEAVY : ftxui::LIGHT) |
                       ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
                                   2 * screen_width / 3) |
                       ftxui::size(ftxui::HEIGHT, ftxui::EQUAL,
                                   2 * screen_height / 3);
            });

        this->impl_->register_area = ftxui::Renderer([=, this](bool focused) {
            return ftxui::window(ftxui::text("Register"),
                                 this->impl_->register_area_content->Render(),
                                 ftxui::LIGHT) |
                   ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        });

        this->impl_->syscall_area = ftxui::Renderer([=, this](bool focused) {
            return ftxui::window(ftxui::text("Syscall"),
                                 ftxui::Menu(this->impl_->syscall_entries,
                                             &this->impl_->syscall_selected)
                                     ->Render(),
                                 ftxui::LIGHT) |
                   ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        });

        this->impl_->memory_area = ftxui::Renderer([=, this](bool focused) {
            return ftxui::window(ftxui::text("Memory"),
                                 this->impl_->memory_area_content->Render(),
                                 ftxui::LIGHT) |
                   ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width) |
                   ftxui::yflex;
        });

        auto help_style = ftxui::color(ftxui::Color::Gold1) | ftxui::inverted;
        this->impl_->help_area = ftxui::Renderer([=, this](bool focused) {
            return ftxui::hbox({ftxui::text("q: Exit") | help_style,
                                ftxui::filler(),
                                ftxui::text("<Tab>: Change Focus") | help_style,
                                ftxui::filler(),
                                ftxui::text("s: Step Into") | help_style,
                                ftxui::filler(),
                                ftxui::text("n: Step Over") | help_style,
                                ftxui::filler(),
                                ftxui::text("c: Continue") | help_style}) |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);
        });

        return ftxui::vbox({
            ftxui::hbox({this->impl_->disassembler_area->Render(),
                         ftxui::vbox({this->impl_->register_area->Render(),
                                      this->impl_->syscall_area->Render()})}),
            this->impl_->memory_area->Render(),
            this->impl_->help_area->Render(),
        });
    });
    layout |= ftxui::CatchEvent([&](ftxui::Event e) {
        if (e == ftxui::Event::Character('q')) {
            this->impl_->screen.ExitLoopClosure()();
            return true;
        }
        if (e == ftxui::Event::Tab) {
            this->impl_->current_component_id =
                (this->impl_->current_component_id + 1) % 4;
            auto component =
                this->impl_->id_to_component[this->impl_->current_component_id];
            if (component->Focusable()) {
                component->TakeFocus();
            }
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
    this->impl_->screen.Loop(layout);

    return;
}
}  // namespace tui
}  // namespace vdbg