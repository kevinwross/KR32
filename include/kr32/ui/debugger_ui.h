#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "kr32/container/container.h"
#include "kr32/core/devices.h"
#include "kr32/debug/debug.h"
#include "kr32/ui/canvas.h"

namespace kr32::ui {

class DebuggerUI {
public:
    DebuggerUI();
    [[nodiscard]] bool load(const std::string& path, std::string& error);
    [[nodiscard]] bool load_os(const std::string& kernel_path,
                               const std::string& bios_path,
                               std::string& error);
    void show_error(std::string message);
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
    void mouse_up();
    [[nodiscard]] bool wheel(int x, int y, int width, int height, int delta);
    [[nodiscard]] bool key(int key, bool shift, bool control = false);
    [[nodiscard]] bool quit_requested() const { return quit_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] const std::string& error() const { return error_; }

    [[nodiscard]] bool headless(const std::string& ppm_path);
    void open_settings();
    void update();

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
    void stop_continuous(bool reached_target, const core::StepResult& result,
                         bool budget_exhausted = false);
    void reset_stats();
    void record_stats_time(std::chrono::steady_clock::time_point start);
    void draw_panel(Canvas& canvas, Rect rect, const std::string& title);
    void draw_toolbar(Canvas& canvas);
    void draw_status_bar(Canvas& canvas);
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
    void begin_tile_edit(int index);
    void finish_tile_edit(bool commit);
    void draw_fpr_popup(Canvas& canvas);
    void draw_tile_popup(Canvas& canvas);
    void draw_fpr_tooltip(Canvas& canvas);
    void draw_tile_tooltip(Canvas& canvas);
    void draw_control_tooltip(Canvas& canvas);
    void draw_register_tooltip(Canvas& canvas);
    void draw_flag_tooltip(Canvas& canvas);
    void draw_memory_tooltip(Canvas& canvas);
    [[nodiscard]] std::string format_fpr(std::uint64_t value, bool scientific) const;
    [[nodiscard]] std::string format_fpr_float(std::uint32_t value, bool scientific) const;
    void draw_variable_tooltip(Canvas& canvas);
    void draw_scrollbar(Canvas& canvas, Rect panel, int offset, int total, int visible,
                        Rect& track, Rect& thumb);
    void draw_asm_tooltip(Canvas& canvas);
    void draw_settings(Canvas& canvas);
    void draw_gear(Canvas& canvas, Rect rect, bool hover);
    void close_settings(bool save);
    void apply_settings();
    void load_settings();
    void save_settings();
    void draw_error(Canvas& canvas);
    [[nodiscard]] std::string format_hex(std::uint32_t value, int digits = 8) const;
    [[nodiscard]] std::string source_line(int line) const;
    [[nodiscard]] int current_source_line() const;
    [[nodiscard]] bool source_line_to_address(int line, std::uint32_t& address) const;
    void load_source_files();
    void ensure_source_for_current();
    [[nodiscard]] std::string source_path_for(int source_id) const;

    container::Image image_;
    debug::Session session_;
    bool loaded_ = false;
    bool quit_ = false;
    std::string path_;
    std::string status_ = "load a .k32 image";
    std::string error_;
    std::vector<std::string> source_lines_;
    std::map<int, std::vector<std::string>> source_cache_;
    int source_view_id_ = -1;
    std::string source_view_path_;
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
    bool scroll_dragging_ = false;
    int scroll_drag_panel_ = -1;
    int scroll_drag_last_y_ = 0;
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
    int hover_register_ = -1;
    int hover_memory_byte_ = -1;
    struct MemoryByteCell {
        Rect rect{};
        std::uint32_t address = 0;
    };
    std::vector<MemoryByteCell> memory_byte_cells_;
    int editing_fpr_ = -1;
    bool fpr_float_view_ = false;
    std::string fpr_edit_;
    bool fpr_edit_fresh_ = true;
    Rect fpr_view_rect_{};
    Rect fpr_popup_{};
    Rect fpr_input_rect_{};
    Rect fpr_ok_rect_{};
    Rect fpr_cancel_rect_{};
    std::vector<Rect> tile_cells_;
    int hover_tile_ = -1;
    int editing_tile_ = -1;
    std::string tile_edit_;
    bool tile_edit_fresh_ = true;
    Rect tile_popup_{};
    Rect tile_input_rect_{};
    Rect tile_ok_rect_{};
    Rect tile_cancel_rect_{};
    struct ControlHoverRow {
        Rect row{};
        std::string title;
        std::string detail;
    };
    std::vector<ControlHoverRow> control_hover_rows_;
    int hover_control_ = -1;

    Rect settings_rect_{};
    Rect stats_reset_rect_{};
    std::uint64_t stats_base_ticks_ = 0;
    double stats_seconds_ = 0.0;
    Rect settings_modal_{};
    Rect settings_close_{};
    Rect settings_args_rect_{};
    Rect settings_run_rect_{};
    Rect settings_stop_box_{};
    Rect settings_listing_box_{};
    Rect settings_apply_rect_{};
    Rect settings_cancel_rect_{};
    bool settings_open_ = false;
    bool settings_args_focused_ = false;
    bool settings_run_focused_ = false;
    bool settings_stop_at_main_ = true;
    bool settings_write_listing_ = false;
    std::string settings_arguments_;
    std::string settings_run_limit_text_ = "5000000";
    std::uint64_t settings_run_limit_ = 5'000'000;
    std::string settings_error_;
    bool continuous_run_ = false;
    bool continuous_has_target_ = false;
    bool continuous_owned_breakpoint_ = false;
    std::uint32_t continuous_target_ = 0;
    std::uint64_t continuous_remaining_ = 0;
    int hover_settings_control_ = -1;
    bool os_mode_ = false;
    std::string bios_path_;
    core::DeviceBus devices_;
    std::string serial_text_;
    bool console_hover_ = false;

    struct VarHoverRow {
        Rect row{};
        std::string name;
        std::string type;
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        std::uint32_t size = 0;
        bool local = false;
        bool ok = false;
        bool header = false;
        bool array = false;
        bool pager = false;
        std::string key;
        int page = 0;
        int page_count = 1;
    };
    std::vector<VarHoverRow> var_hover_rows_;
    int hover_var_ = -1;
    bool locals_expanded_ = true;
    bool globals_expanded_ = true;
    std::map<std::string, bool> array_expanded_;
    std::map<std::string, int> array_pages_;
    int last_variable_click_ = -1;
    std::chrono::steady_clock::time_point last_variable_click_time_{};
    int last_register_click_ = -1;
    std::chrono::steady_clock::time_point last_register_click_time_{};

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
