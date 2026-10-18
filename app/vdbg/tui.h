#pragma once

#include <memory>
#include <vector>

namespace vdbg {
namespace tui {
class TUI {
public:
    TUI();
    ~TUI();
    TUI(const TUI&) = delete;
    TUI& operator=(const TUI&) = delete;
    TUI(TUI&&) = delete;
    TUI& operator=(TUI&&) = delete;

public:
    void show();
    void set_disassembler_content(const std::vector<std::string>& content);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace tui
}  // namespace vdbg
