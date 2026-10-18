#include "tui.h"

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
    Impl() : screen(ftxui::ScreenInteractive::Fullscreen()) {}
    ~Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

public:
    ftxui::ScreenInteractive screen;
    ftxui::Component disassembler_area_content = ftxui::Menu({});
    ftxui::Element disassembler_area;
    ftxui::Element register_area;
    ftxui::Element syscall_area;
    ftxui::Element memory_area;
    ftxui::Element help_area;
};

TUI::TUI() : impl_(std::make_unique<Impl>()) {}

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
            ftxui::window(ftxui::text("Register"), ftxui::text("Register"),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        this->impl_->syscall_area =
            ftxui::window(ftxui::text("Syscall"), ftxui::text("Syscall"),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width / 3) |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, screen_height / 3);
        this->impl_->memory_area =
            ftxui::window(ftxui::text("Memory"), ftxui::text("Memory"),
                          ftxui::LIGHT) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, screen_width) |
            ftxui::yflex;
        auto help_style = ftxui::color(ftxui::Color::Gold1) | ftxui::inverted;
        this->impl_->help_area =
            ftxui::hbox({ftxui::text("q: Exit") | help_style,
                         ftxui::text("s: Step Into") | help_style,
                         ftxui::text("n: Step Over") | help_style,
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
