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
    [[nodiscard]] bool click(int x, int y, int width, int height);
    [[nodiscard]] bool context_menu(int x, int y, int width, int height);
    void hover(int x, int y, int width, int height);
    [[nodiscard]] bool wheel(int x, int y, int width, int height, int delta);
    [[nodiscard]] bool key(int key, bool shift);
    [[nodiscard]] bool quit_requested() const { return quit_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] const std::string& error() const { return error_; }

    [[nodiscard]] bool headless(const std::string& ppm_path);

private:
    struct Layout {
        Rect source;
        Rect disassembly;
        Rect cores;
        Rect memory;
        Rect variables;
        Rect call_stack;
        Rect console;
    };

    struct CoreRow {
        std::size_t index = 0;
        Rect row{};
        bool expanded = false;
    };

    [[nodiscard]] Layout layout(int width, int height) const;
    void set_status_from_result(const core::StepResult& result);
    void open_context_menu(int x, int y, int width, int height);
    void draw_context_menu(Canvas& canvas);
    void activate_context_menu(bool run_to);
    void run_to(std::uint32_t address);
    void toggle_source_breakpoint(int line);
    void toggle_instruction_breakpoint(std::uint32_t address);
    void prepare_initial_stop();
    void draw_panel(Canvas& canvas, Rect rect, const std::string& title);
    void draw_toolbar(Canvas& canvas);
    void draw_source(Canvas& canvas, Rect rect);
    void draw_disassembly(Canvas& canvas, Rect rect);
    void draw_cores(Canvas& canvas, Rect rect);
    int draw_core_state(Canvas& canvas, Rect rect, std::size_t core_index);
    void draw_variables(Canvas& canvas, Rect rect);
    void draw_call_stack(Canvas& canvas, Rect rect);
    void draw_memory(Canvas& canvas, Rect rect);
    void draw_console(Canvas& canvas, Rect rect);
    void begin_memory_address_edit();
    void finish_memory_address_edit(bool commit);
    void begin_register_edit(int index);
    void finish_register_edit(bool commit);
    void begin_fpr_edit(int index);
    void finish_fpr_edit(bool commit);
    void draw_fpr_popup(Canvas& canvas);
    void draw_fpr_tooltip(Canvas& canvas);
    [[nodiscard]] std::string format_fpr(std::uint64_t value, bool scientific) const;
    void draw_variable_tooltip(Canvas& canvas);
    void draw_scrollbar(Canvas& canvas, Rect panel, int offset, int total, int visible,
                        Rect& track, Rect& thumb);
    void draw_asm_tooltip(Canvas& canvas);
    void draw_error(Canvas& canvas);
    [[nodiscard]] std::string format_hex(std::uint32_t value, int digits = 8) const;
    [[nodiscard]] std::string source_line(int line) const;
    [[nodiscard]] int current_source_line() const;
    [[nodiscard]] bool source_line_to_address(int line, std::uint32_t& address) const;

    container::Image image_;
    debug::Session session_;
    bool loaded_ = false;
    bool quit_ = false;
    std::string path_;
    std::string status_ = "load a .k32 image";
    std::string error_;
    std::vector<std::string> source_lines_;
    std::vector<CoreRow> core_rows_;
    std::size_t expanded_core_ = 0;
    std::size_t selected_core_ = 0;
    std::uint32_t memory_address_ = 0;
    bool memory_address_editing_ = false;
    std::string memory_edit_;
    bool memory_edit_fresh_ = true;
    Rect memory_address_rect_{};
    Rect memory_pointer_rect_{};
    Rect memory_up_rect_{};
    Rect memory_down_rect_{};
    Rect memory_header_rect_{};
    int source_scroll_ = 0;
    bool source_follow_ = true;
    int asm_scroll_ = 0;
    int cores_scroll_ = 0;
    int variables_scroll_ = 0;
    int stack_scroll_ = 0;
    int console_scroll_ = 0;
    Rect source_scroll_track_{};
    Rect source_scroll_thumb_{};
    Rect asm_scroll_track_{};
    Rect asm_scroll_thumb_{};
    Rect cores_scroll_track_{};
    Rect cores_scroll_thumb_{};
    Rect variables_scroll_track_{};
    Rect variables_scroll_thumb_{};
    Rect stack_scroll_track_{};
    Rect stack_scroll_thumb_{};
    Rect console_scroll_track_{};
    Rect console_scroll_thumb_{};

    int editing_register_ = -1;
    std::string register_edit_;
    bool register_edit_fresh_ = true;
    std::vector<Rect> register_cells_;
    std::vector<Rect> fpr_cells_;
    Rect pc_cell_{};
    Rect sp_cell_{};
    Rect flags_value_cell_{};
    std::vector<Rect> flag_cells_;
    int hover_flag_ = -1;
    int hover_fpr_ = -1;
    int editing_fpr_ = -1;
    std::string fpr_edit_;
    bool fpr_edit_fresh_ = true;
    Rect fpr_popup_{};
    Rect fpr_input_rect_{};
    Rect fpr_ok_rect_{};
    Rect fpr_cancel_rect_{};

    struct VarHoverRow {
        Rect row{};
        std::string name;
        std::string type;
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        std::uint32_t size = 0;
        bool local = false;
        bool ok = false;
    };
    std::vector<VarHoverRow> var_hover_rows_;
    int hover_var_ = -1;

    struct ContextMenu {
        bool open = false;
        bool source = false;
        int line = 0;
        std::uint32_t address = 0;
        Rect rect{};
        Rect toggle_rect{};
        Rect run_rect{};
        int hover = -1;
    };
    ContextMenu menu_;
    int hover_row_ = -1;
    bool hover_source_ = false;

    struct AsmRow {
        std::uint32_t address = 0;
        Rect row{};
        Rect mnemonic{};
        Rect operand{};
        std::string text;
    };
    std::vector<AsmRow> asm_rows_;
    int hover_asm_index_ = -1;
    bool hover_asm_operand_ = false;
    int mouse_x_ = 0;
    int mouse_y_ = 0;
};

// Renders one frame without opening a window. Returns the canvas hash and
// optionally writes a PPM screenshot.
[[nodiscard]] bool render_headless(const std::string& image_path,
                                   const std::string& ppm_path,
                                   std::uint64_t& hash,
                                   std::string& error);

} // namespace kr32::ui
