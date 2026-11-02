#pragma once

#include <string>
#include <vector>

#include "kr32/container/container.h"
#include "kr32/debug/debug.h"
#include "kr32/ui/canvas.h"

namespace kr32::ui {

class DebuggerUI {
public:
    [[nodiscard]] bool load(const std::string& path, std::string& error);
    void restart();
    void run();
    void step();
    void step_over();
    void step_out();
    void step_line_in();
    void step_line_over();
    void step_line_out();
    void toggle_breakpoint_at_current_line();

    void render(Canvas& canvas);
    [[nodiscard]] bool click(int x, int y);
    [[nodiscard]] bool key(int key, bool shift);
    [[nodiscard]] bool quit_requested() const { return quit_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] const std::string& error() const { return error_; }

    [[nodiscard]] bool headless(const std::string& ppm_path);

private:
    enum class Tab { Memory, Machine, Console };

    void set_status_from_result(const core::StepResult& result);
    void prepare_initial_stop();
    void draw_panel(Canvas& canvas, Rect rect, const std::string& title);
    void draw_toolbar(Canvas& canvas);
    void draw_source(Canvas& canvas, Rect rect);
    void draw_disassembly(Canvas& canvas, Rect rect);
    void draw_registers(Canvas& canvas, Rect rect);
    void draw_variables(Canvas& canvas, Rect rect);
    void draw_bottom(Canvas& canvas, Rect rect);
    void draw_memory(Canvas& canvas, Rect rect);
    void draw_machine(Canvas& canvas, Rect rect);
    void draw_console(Canvas& canvas, Rect rect);
    void draw_error(Canvas& canvas);
    [[nodiscard]] std::string format_hex(std::uint32_t value, int digits = 8) const;
    [[nodiscard]] std::string source_line(int line) const;
    [[nodiscard]] int current_source_line() const;
    [[nodiscard]] bool source_line_to_address(int line, std::uint32_t& address) const;

    container::Image image_;
    debug::Session session_;
    bool loaded_ = false;
    bool quit_ = false;
    Tab tab_ = Tab::Memory;
    std::string path_;
    std::string status_ = "load a .k32 image";
    std::string error_;
    std::vector<std::string> source_lines_;
    int source_scroll_ = 0;
};

// Renders one frame without opening a window. Returns the canvas hash and
// optionally writes a PPM screenshot.
[[nodiscard]] bool render_headless(const std::string& image_path,
                                   const std::string& ppm_path,
                                   std::uint64_t& hash,
                                   std::string& error);

} // namespace kr32::ui
