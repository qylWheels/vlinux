#include "tui.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/ftxui.hpp>
#include <ftxui/screen/screen.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace vdbg {
namespace tui {
class TUI::Impl {
public:
    Impl(vlinux::VM& vm, ::csh& cshandle)
        : screen(ftxui::ScreenInteractive::Fullscreen()),
          vm(&vm),
          cshandle(&cshandle) {}
    ~Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

public:
    vlinux::VM* vm;
    ::csh* cshandle;

public:
    ftxui::ScreenInteractive screen;
    ftxui::Component disassembler_area_content = ftxui::Menu({});
    ftxui::Element disassembler_area;
    ftxui::Component register_area_content = ftxui::Menu({});
    ftxui::Element register_area;
    ftxui::Component syscall_area_content = ftxui::Menu({});
    ftxui::Element syscall_area;
    ftxui::Component memory_area_content = ftxui::Menu({});
    ftxui::Element memory_area;
    ftxui::Element help_area;
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
        this->set_disassembler_content(std::vector<std::string>{insn_str});
    });
    if (!result) throw std::runtime_error("Failed to add instruction hook");
}

TUI::~TUI() = default;

void TUI::show() {
    auto layout = ftxui::Renderer([this] {
        auto screen_width = this->impl_->screen.dimx();
        auto screen_height = this->impl_->screen.dimy();

        this->impl_->disassembler_area =
            ftxui::window(ftxui::text("Disassembler"),
                          this->impl_->disassembler_area_content->Render(),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 2 * screen_width / 3) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 2 * screen_height / 3);
        this->impl_->register_area =
            ftxui::window(ftxui::text("Register"),
                          this->impl_->register_area_content->Render(),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        this->impl_->syscall_area =
            ftxui::window(ftxui::text("Syscall"),
                          this->impl_->syscall_area_content->Render(),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        this->impl_->memory_area =
            ftxui::window(ftxui::text("Memory"),
                          this->impl_->memory_area_content->Render(),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width) |
            ftxui::yflex;
        auto help_style = ftxui::color(ftxui::Color::Gold1) | ftxui::inverted;
        this->impl_->help_area =
            ftxui::hbox(
                {ftxui::text("q: Exit") | help_style, ftxui::filler(),
                 ftxui::text("s: Step Into") | help_style, ftxui::filler(),
                 ftxui::text("n: Step Over") | help_style, ftxui::filler(),
                 ftxui::text("c: Continue") | help_style}) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);

        return ftxui::vbox({
            ftxui::hbox({this->impl_->disassembler_area,
                         ftxui::vbox({this->impl_->register_area,
                                      this->impl_->syscall_area})}),
            this->impl_->memory_area,
            this->impl_->help_area,
        });
    });
    layout |= ftxui::CatchEvent([&](ftxui::Event e) {
        if (e == ftxui::Event::Character('q')) {
            this->impl_->screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    this->impl_->screen.Loop(layout);

    return;
}

void TUI::set_disassembler_content(const std::vector<std::string>& content) {
    int selected = 0;
    this->impl_->disassembler_area_content = ftxui::Menu(content, &selected);
    return;
}
}  // namespace tui
}  // namespace vdbg
