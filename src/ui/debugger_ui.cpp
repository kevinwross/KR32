#include "kr32/ui/debugger_ui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/dis/disassembler.h"
#include "kr32/isa/encoding.h"
#include "kr32/link/linker.h"
#include "kr32/platform/window.h"
#include "kr32/ui/help.h"

namespace kr32::ui {
namespace {

constexpr Color kBackground{12, 16, 20, 255};
constexpr Color kPanel{22, 28, 34, 255};
constexpr Color kPanelAlt{28, 36, 44, 255};
constexpr Color kBorder{54, 68, 80, 255};
constexpr Color kText{220, 228, 236, 255};
constexpr Color kDim{132, 146, 158, 255};
constexpr Color kAccent{76, 170, 255, 255};
constexpr Color kBad{255, 104, 104, 255};

constexpr int kToolbarHeight = 30;
constexpr int kSourceGutter = 58;
constexpr int kSourceBreakGutter = 18;
constexpr int kAsmGutter = 104;
constexpr int kAsmBreakGutter = 18;

int source_line_height(int width) {
    return width < 380 ? 16 : 18;
}

std::string hex(std::uint32_t value, int digits) {
    char buffer[24] = {};
    if (digits == 2) {
        std::snprintf(buffer, sizeof(buffer), "%02X", value & 0xFFU);
    } else if (digits == 4) {
        std::snprintf(buffer, sizeof(buffer), "%04X", value & 0xFFFFU);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%08X", value);
    }
    return buffer;
}

std::string decimal(std::uint64_t value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return buffer;
}

std::string trim_to(std::string value, std::size_t limit) {
    if (value.size() > limit) {
        value.resize(limit);
    }
    return value;
}

bool compile_c_source(const std::string& path, container::Image& image, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    const cc::Result compiled = cc::compile(source, path);
    if (!compiled.ok) {
        if (!compiled.diagnostics.empty()) {
            const cc::Diagnostic& diagnostic = compiled.diagnostics.front();
            error = path + ":" + std::to_string(diagnostic.line) + ":" +
                    std::to_string(diagnostic.column) + ": " + diagnostic.message;
        } else {
            error = path + ": compile failed";
        }
        return false;
    }
    container::Image user;
    assembler::Error assembly_error;
    if (!assembler::assemble_object(compiled.assembly, user, assembly_error)) {
        error = path + ": " + assembly_error.message;
        return false;
    }
    container::Image runtime;
    if (!assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error)) {
        error = "runtime: " + assembly_error.message;
        return false;
    }
    linker::Map map;
    linker::Error link_error;
    if (!linker::link({user, runtime}, linker::Options{}, image, map, link_error)) {
        error = path + ": " + link_error.message;
        return false;
    }
    return true;
}

bool has_c_extension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == "c";
}

} // namespace

bool DebuggerUI::load(const std::string& path, std::string& error) {
    std::string source_path;
    if (has_c_extension(path)) {
        if (!compile_c_source(path, image_, error)) {
            return false;
        }
        source_path = path;
    } else {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "cannot open " + path;
            return false;
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                              std::istreambuf_iterator<char>());
        container::Error container_error;
        if (!container::read(bytes, image_, container_error)) {
            error = path + ": " + container_error.message;
            return false;
        }
    }
    if (image_.type != container::ImageType::Executable) {
        error = path + ": not an executable";
        return false;
    }
    if (!session_.load(image_, error)) {
        return false;
    }
    path_ = path;
    source_lines_.clear();
    if (source_path.empty() && !session_.info().sources.empty()) {
        source_path = session_.info().sources.front().path;
    }
    if (!source_path.empty()) {
        std::ifstream source(source_path);
        if (!source) {
            source.open("../" + source_path);
        }
        if (!source) {
            source.open("../../" + source_path);
        }
        if (source) {
            std::string line;
            while (std::getline(source, line)) {
                source_lines_.push_back(line);
            }
        }
    }
    loaded_ = true;
    source_scroll_ = 0;
    error_.clear();
    prepare_initial_stop();
    status_ = "paused at " + hex(session_.machine().pc(), 8);
    return true;
}

void DebuggerUI::prepare_initial_stop() {
    const debug::Function* main_function = nullptr;
    for (const debug::Function& function : session_.info().functions) {
        if (function.name == "main") {
            main_function = &function;
            break;
        }
    }
    if (main_function == nullptr) {
        return;
    }
    std::uint32_t target = 0;
    bool found = false;
    for (const debug::Line& line : session_.info().lines) {
        if (line.address >= main_function->start && line.address < main_function->end) {
            target = line.address;
            found = true;
            break;
        }
    }
    if (!found) {
        session_.machine().set_pc(main_function->start);
        return;
    }
    session_.set_breakpoint(target);
    (void)session_.run(10000);
    session_.clear_breakpoint(target);
}

void DebuggerUI::restart() {
    if (!loaded_ || path_.empty()) {
        return;
    }
    std::string error;
    if (!load(path_, error)) {
        error_ = error;
        return;
    }
    status_ = "restarted at " + hex(session_.machine().pc(), 8);
    error_.clear();
}

void DebuggerUI::run() {
    if (!loaded_) {
        return;
    }
    const core::StepResult result = session_.run(200000);
    set_status_from_result(result);
}

void DebuggerUI::step() {
    if (!loaded_) {
        return;
    }
    set_status_from_result(session_.step());
}

void DebuggerUI::step_over() {
    if (!loaded_) {
        return;
    }
    set_status_from_result(session_.step_over());
}

void DebuggerUI::step_out() {
    if (!loaded_) {
        return;
    }
    set_status_from_result(session_.step_out());
}

void DebuggerUI::step_line_in() {
    if (!loaded_) {
        return;
    }
    set_status_from_result(session_.step_line_in());
}

void DebuggerUI::step_line_over() {
    if (!loaded_) {
        return;
    }
    set_status_from_result(session_.step_line_over());
}

void DebuggerUI::step_line_out() {
    if (!loaded_) {
        return;
    }
    set_status_from_result(session_.step_line_out());
}

void DebuggerUI::set_status_from_result(const core::StepResult& result) {
    if (session_.at_breakpoint()) {
        status_ = "breakpoint at " + hex(session_.machine().pc(), 8);
        return;
    }
    if (result.kind == core::StepResult::Kind::Halted) {
        status_ = "halted exit=" + decimal(session_.machine().exit_code());
        return;
    }
    if (result.kind == core::StepResult::Kind::Raised ||
        result.kind == core::StepResult::Kind::FaultDelivered) {
        status_ = "fault vector=" + decimal(result.fault.vector) + " at " +
                  hex(result.fault.address, 8);
        return;
    }
    status_ = "paused at " + hex(session_.machine().pc(), 8);
}

int DebuggerUI::current_source_line() const {
    if (!loaded_) {
        return 0;
    }
    const debug::Line* line = session_.line_at(session_.machine().pc());
    return line == nullptr ? 0 : line->line;
}

bool DebuggerUI::source_line_to_address(int line, std::uint32_t& address) const {
    for (const debug::Line& candidate : session_.info().lines) {
        if (candidate.line == line) {
            address = candidate.address;
            return true;
        }
    }
    return false;
}

std::string DebuggerUI::source_line(int line) const {
    if (line <= 0 || static_cast<std::size_t>(line) > source_lines_.size()) {
        return {};
    }
    return source_lines_[static_cast<std::size_t>(line - 1)];
}

void DebuggerUI::toggle_breakpoint_at_current_line() {
    const int line = current_source_line();
    std::uint32_t address = 0;
    if (line == 0 || !source_line_to_address(line, address)) {
        status_ = "no breakpoint address for the current line";
        return;
    }
    if (session_.breakpoints().find(address) != session_.breakpoints().end()) {
        session_.clear_breakpoint(address);
        status_ = "breakpoint cleared at " + hex(address, 8);
    } else {
        session_.set_breakpoint(address);
        status_ = "breakpoint set at " + hex(address, 8);
    }
}

std::string DebuggerUI::format_hex(std::uint32_t value, int digits) const {
    return hex(value, digits);
}

void DebuggerUI::draw_panel(Canvas& canvas, Rect rect, const std::string& title) {
    canvas.fill(rect, kPanel);
    canvas.frame(rect, kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2, 20}, kPanelAlt);
    canvas.text(rect.x + 6, rect.y + 3,
                trim_to(title, static_cast<std::size_t>((rect.width - 12) / 7)), kAccent,
                FontRole::UiBold);
}

void DebuggerUI::draw_toolbar(Canvas& canvas) {
    canvas.fill(Rect{0, 0, canvas.width(), kToolbarHeight}, kPanelAlt);
    canvas.fill(Rect{0, kToolbarHeight - 1, canvas.width(), 1}, kBorder);
    canvas.text(8, 7, "KR32", kAccent, FontRole::UiBold);
    const Color button{42, 54, 66, 255};
    const struct {
        const char* label;
        int x;
        int width;
    } buttons[] = {
        {"RUN", 82, 40},   {"STEP", 126, 44}, {"OVER", 174, 44},
        {"OUT", 222, 36},  {"BKPT", 262, 44}, {"RST", 310, 34}, {"LOAD", 348, 44},
        {"BUILD", 396, 48},
    };
    for (const auto& button_entry : buttons) {
        canvas.fill(Rect{button_entry.x, 4, button_entry.width, 22}, button);
        canvas.frame(Rect{button_entry.x, 4, button_entry.width, 22}, kBorder);
        canvas.text(button_entry.x + 6, 7, button_entry.label, kText, FontRole::Ui);
    }
    canvas.text(452, 7, trim_to(status_, static_cast<std::size_t>((canvas.width() - 460) / 7)),
                kDim, FontRole::MonoSmall);
}

void DebuggerUI::draw_source(Canvas& canvas, Rect rect) {
    std::string title = "SOURCE";
    if (!session_.info().sources.empty()) {
        title += "  " + session_.info().sources.front().path;
    }
    draw_panel(canvas, rect, title);
    const int button_width = 40;
    const int button_start = rect.x + rect.width - 3 * (button_width + 2) - 4;
    canvas.fill(Rect{button_start - 1, rect.y + 1, rect.width - (button_start - rect.x) + 1, 20},
                kPanelAlt);
    const char* labels[] = {"OVER", "IN", "OUT"};
    for (int index = 0; index < 3; ++index) {
        const int x = button_start + index * (button_width + 2);
        canvas.fill(Rect{x, rect.y + 2, button_width, 18}, Color{42, 54, 66, 255});
        canvas.frame(Rect{x, rect.y + 2, button_width, 18}, kBorder);
        canvas.text(x + 5, rect.y + 3, labels[index], kText, FontRole::MonoSmall);
    }
    const int line_height = source_line_height(rect.width);
    const FontRole source_role =
        rect.width < 380 ? FontRole::MonoSmall : FontRole::Mono;
    const int top = rect.y + 24;
    const int visible = std::max(1, (rect.height - 28) / line_height);
    canvas.fill(Rect{rect.x + 1, top, kSourceGutter - 1, rect.height - 26}, kPanelAlt);
    canvas.fill(Rect{rect.x + kSourceGutter, top, 1, rect.height - 26}, kBorder);
    const int current = current_source_line();
    if (current > 0 && (current - 1 < source_scroll_ || current - 1 >= source_scroll_ + visible)) {
        source_scroll_ = std::max(0, current - visible / 2);
    }
    if (source_lines_.empty()) {
        canvas.text(rect.x + 8, top, "source unavailable", kDim, FontRole::MonoSmall);
        return;
    }
    for (int row = 0; row < visible; ++row) {
        const int index = source_scroll_ + row;
        if (index < 0 || static_cast<std::size_t>(index) >= source_lines_.size()) {
            break;
        }
        const int line_number = index + 1;
        const int y = top + row * line_height;
        if (hover_source_ && hover_row_ == row) {
            canvas.fill(Rect{rect.x + 1, y - 1, kSourceGutter - 1, line_height},
                        Color{32, 46, 58, 255});
        }
        if (line_number == current) {
            canvas.fill(Rect{rect.x + 1, y - 1, rect.width - 2, line_height},
                        Color{38, 66, 92, 255});
        }
        canvas.fill(Rect{rect.x + 1, y - 1, kSourceGutter - 1, line_height},
                    hover_source_ && hover_row_ == row ? Color{32, 46, 58, 255} : kPanelAlt);
        canvas.fill(Rect{rect.x + kSourceBreakGutter, y - 1, 1, line_height}, kBorder);
        canvas.fill(Rect{rect.x + kSourceGutter, y - 1, 1, line_height}, kBorder);
        std::uint32_t address = 0;
        const bool has_breakpoint =
            source_line_to_address(line_number, address) &&
            session_.breakpoints().find(address) != session_.breakpoints().end();
        if (has_breakpoint) {
            canvas.fill(Rect{rect.x + 7, y + 5, 7, 8}, kBad);
        }
        const std::string number = std::to_string(line_number);
        const int number_width = Canvas::text_width(number, source_role);
        canvas.text(rect.x + kSourceGutter - 8 - number_width, y, number,
                    line_number == current ? kText : kDim, source_role);
        canvas.text(rect.x + kSourceGutter + 6, y,
                    trim_to(source_lines_[static_cast<std::size_t>(index)],
                            static_cast<std::size_t>((rect.width - kSourceGutter - 12) /
                                                     (rect.width < 380 ? 7 : 8))),
                    line_number == current ? kText : Color{180, 194, 206, 255},
                    source_role);
    }
}

void DebuggerUI::draw_disassembly(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "DISASSEMBLY");
    const int button_width = 38;
    const int button_start = rect.x + rect.width - 3 * (button_width + 2) - 4;
    canvas.fill(Rect{button_start - 1, rect.y + 1, rect.width - (button_start - rect.x) + 1, 20},
                kPanelAlt);
    const char* labels[] = {"OVER", "IN", "OUT"};
    for (int index = 0; index < 3; ++index) {
        const int x = button_start + index * (button_width + 2);
        canvas.fill(Rect{x, rect.y + 2, button_width, 18}, Color{42, 54, 66, 255});
        canvas.frame(Rect{x, rect.y + 2, button_width, 18}, kBorder);
        canvas.text(x + 4, rect.y + 3, labels[index], kText, FontRole::MonoSmall);
    }
    const std::uint32_t pc = session_.machine().pc();
    const int line_height = 20;
    const int rows = std::max(1, (rect.height - 28) / line_height);
    const std::uint32_t before = static_cast<std::uint32_t>((rows / 2) * 4);
    std::uint32_t start = pc >= before ? pc - before : 0;
    start = std::max(start, session_.machine().ram_base());
    const int address_width = Canvas::text_width("00000000", FontRole::Mono);
    const int bytes_width = std::clamp(rect.width - kAsmGutter - 160, 100, 125);
    const int bytes_x = rect.x + kAsmGutter + 6;
    const int instruction_x = bytes_x + bytes_width + 4;
    asm_rows_.clear();
    canvas.fill(Rect{rect.x + 1, rect.y + 24, kAsmGutter - 1, rect.height - 26}, kPanelAlt);
    canvas.fill(Rect{rect.x + kAsmGutter, rect.y + 24, 1, rect.height - 26}, kBorder);
    canvas.fill(Rect{instruction_x - 4, rect.y + 24, 1, rect.height - 26}, kBorder);
    canvas.text(bytes_x, rect.y + 7, "HEX", kDim, FontRole::MonoSmall);
    for (int row = 0; row < rows; ++row) {
        const std::uint32_t address = start + static_cast<std::uint32_t>(row * 4);
        const int y = rect.y + 24 + row * line_height;
        if (!hover_source_ && hover_row_ == row) {
            canvas.fill(Rect{rect.x + 1, y - 1, kAsmGutter - 1, line_height},
                        Color{32, 46, 58, 255});
        }
        if (address == pc) {
            canvas.fill(Rect{rect.x + 1, y - 1, rect.width - 2, line_height},
                        Color{38, 66, 92, 255});
        }
        canvas.fill(Rect{rect.x + 1, y - 1, kAsmGutter - 1, line_height},
                    !hover_source_ && hover_row_ == row ? Color{32, 46, 58, 255}
                                                        : kPanelAlt);
        canvas.fill(Rect{rect.x + kAsmBreakGutter, y - 1, 1, line_height}, kBorder);
        canvas.fill(Rect{rect.x + kAsmGutter, y - 1, 1, line_height}, kBorder);
        std::uint8_t bytes[8] = {};
        bool read_ok = true;
        for (int index = 0; index < 8; ++index) {
            read_ok = read_ok && session_.machine().read8(address + index, bytes[index]);
        }
        std::string text;
        if (read_ok) {
            std::vector<std::uint8_t> data(bytes, bytes + 8);
            disassembler::Options options;
            options.base = address;
            options.max_words = 1;
            text = disassembler::disassemble(data, options);
            const std::size_t newline = text.find('\n');
            if (newline != std::string::npos) {
                text.resize(newline);
            }
            const std::size_t colon = text.find(':');
            if (colon != std::string::npos) {
                std::size_t position = colon + 1;
                auto skip_space = [&]() {
                    while (position < text.size() && text[position] == ' ') {
                        ++position;
                    }
                };
                skip_space();
                position = std::min(text.size(), position + 8);
                skip_space();
                if (position + 8 <= text.size() &&
                    std::all_of(text.begin() + static_cast<std::ptrdiff_t>(position),
                                text.begin() + static_cast<std::ptrdiff_t>(position + 8),
                                [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
                    position += 8;
                }
                skip_space();
                if (position < text.size()) {
                    text = text.substr(position);
                }
            }
        } else {
            text = "??";
        }
        std::string encoded;
        if (read_ok) {
            const bool extended = (bytes[1] & 0x10U) != 0;
            const auto word_at = [&](int offset) {
                return static_cast<std::uint32_t>(bytes[offset]) |
                       (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                       (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                       (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
            };
            encoded = hex(word_at(0), 8);
            if (extended) {
                encoded += hex(word_at(4), 8);
            }
        } else {
            encoded = "??";
        }
        const bool has_breakpoint =
            session_.breakpoints().find(address) != session_.breakpoints().end();
        if (has_breakpoint) {
            canvas.fill(Rect{rect.x + 8, y + 5, 7, 8}, kBad);
        }
        const std::size_t split = text.find(' ');
        const std::string mnemonic =
            split == std::string::npos ? text : text.substr(0, split);
        const int mnemonic_width = Canvas::text_width(mnemonic, FontRole::Mono);
        const int text_width = Canvas::text_width(text, FontRole::Mono);
        const int space_width = Canvas::text_width(" ", FontRole::Mono);
        const Rect mnemonic_rect{instruction_x, y, mnemonic_width, line_height};
        Rect operand_rect{};
        if (split != std::string::npos) {
            operand_rect = Rect{instruction_x + mnemonic_width + space_width, y,
                                std::max(0, text_width - mnemonic_width - space_width),
                                line_height};
        }
        if (hover_asm_index_ == static_cast<int>(asm_rows_.size())) {
            const Rect& highlight = hover_asm_operand_ ? operand_rect : mnemonic_rect;
            if (highlight.width > 0 && highlight.height > 0) {
                canvas.fill(highlight, Color{44, 64, 84, 255});
                canvas.frame(highlight, kAccent);
            }
        }
        asm_rows_.push_back(
            AsmRow{address, Rect{rect.x, y - 1, rect.width, line_height}, mnemonic_rect,
                   operand_rect, text});
        canvas.text(rect.x + kAsmGutter - 10 - address_width, y, hex(address, 8),
                    address == pc ? kAccent : kDim, FontRole::Mono);
        canvas.text(bytes_x, y, trim_to(encoded, static_cast<std::size_t>(bytes_width / 7)),
                    address == pc ? kText : kDim, FontRole::MonoSmall);
        canvas.text(instruction_x, y,
                    trim_to(text, static_cast<std::size_t>((rect.x + rect.width - instruction_x) / 8)),
                    address == pc ? kText : Color{180, 194, 206, 255}, FontRole::Mono);
    }
}

void DebuggerUI::draw_registers(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "REGISTERS");
    const core::Machine& machine = session_.machine();
    const int column_width = std::max(110, (rect.width - 16) / 2);
    for (int index = 0; index < 16; ++index) {
        const int column = index / 8;
        const int row = index % 8;
        const int x = rect.x + 8 + column * column_width;
        const int y = rect.y + 24 + row * 14;
        canvas.text(x, y, "R" + std::to_string(index), kDim, FontRole::MonoSmall);
        canvas.text(x + 26, y, hex(machine.reg(static_cast<std::size_t>(index)), 8), kText,
                    FontRole::MonoSmall);
    }
    const int y = rect.y + 24 + 8 * 14 + 6;
    canvas.text(rect.x + 8, y, "PC " + hex(machine.pc(), 8), kAccent, FontRole::MonoSmall);
    canvas.text(rect.x + 8, y + 14,
                "SP " + hex(machine.sp(), 8) + "  FLAGS " + hex(machine.flags(), 8), kText,
                FontRole::MonoSmall);
    canvas.text(rect.x + 8, y + 28,
                "TICKS " + decimal(machine.ticks()) + "  CORE " +
                    std::to_string(machine.core_id()) +
                    (machine.kernel_mode() ? " KERNEL" : " USER"),
                kDim, FontRole::MonoSmall);
}

void DebuggerUI::draw_variables(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "VARIABLES / CALL STACK");
    int y = rect.y + 24;
    const debug::Function* function = session_.function_at(session_.machine().pc());
    if (function != nullptr) {
        canvas.text(rect.x + 6, y, "in " + function->name, kAccent);
        y += 16;
        for (const debug::Variable& variable : session_.info().variables) {
            if (variable.function != function->name || y > rect.y + rect.height - 30) {
                continue;
            }
            std::uint32_t value = 0;
            std::string error;
            const bool ok = session_.read_variable(variable.name, value, error);
            canvas.text(rect.x + 10, y, variable.name + " = " + (ok ? "0x" + hex(value, 8) : "?"),
                        ok ? kText : kBad, FontRole::MonoSmall);
            y += 14;
        }
    }
    if (!session_.info().globals.empty() && y < rect.y + rect.height - 30) {
        canvas.text(rect.x + 6, y, "WATCH", kDim);
        y += 14;
        for (const debug::Global& global : session_.info().globals) {
            if (y > rect.y + rect.height - 20) {
                break;
            }
            std::uint32_t value = 0;
            std::string error;
            const bool ok = session_.read_global(global.name, value, error);
            canvas.text(rect.x + 10, y, global.name + " = " + (ok ? "0x" + hex(value, 8) : "?"),
                        ok ? kText : kBad, FontRole::MonoSmall);
            y += 14;
        }
    }
    std::vector<debug::Frame> frames = session_.call_stack();
    if (!frames.empty() && y < rect.y + rect.height - 12) {
        canvas.text(rect.x + 6, y, "STACK", kDim);
        y += 14;
        for (const debug::Frame& frame : frames) {
            if (y > rect.y + rect.height - 10) {
                break;
            }
            canvas.text(rect.x + 10, y, trim_to(frame.function + " " + hex(frame.pc, 8), 46),
                        kText, FontRole::MonoSmall);
            y += 14;
        }
    }
}

void DebuggerUI::draw_memory(Canvas& canvas, Rect rect) {
    const std::uint32_t rows = std::max<std::uint32_t>(
        3, static_cast<std::uint32_t>(std::max(0, rect.height - 8) / 18));
    const std::uint32_t sp = session_.machine().sp();
    std::uint32_t base = sp > rows * 16U + 16U ? sp - rows * 16U - 16U : session_.machine().ram_base();
    base &= 0xFFFF'FFF0U;
    if (base < session_.machine().ram_base()) {
        base = session_.machine().ram_base();
    }
    for (std::uint32_t row = 0; row < rows; ++row) {
        const std::uint32_t address = base + row * 16U;
        const int y = rect.y + 4 + static_cast<int>(row) * 18;
        std::string line = hex(address, 8) + "  ";
        for (int column = 0; column < 16; ++column) {
            std::uint8_t value = 0;
            const std::uint32_t byte_address = address + static_cast<std::uint32_t>(column);
            const bool ok = session_.machine().read8(byte_address, value) ||
                            session_.machine().read_physical8(byte_address, value);
            line += ok ? hex(value, 2) + " " : "?? ";
        }
        canvas.text(rect.x + 6, y, line, kText, FontRole::MonoSmall);
    }
}

void DebuggerUI::draw_machine(Canvas& canvas, Rect rect) {
    const core::Machine& machine = session_.machine();
    int y = rect.y + 4;
    for (int index = 0; index < 8; ++index) {
        std::string line = "F" + std::to_string(index) + " " + hex(static_cast<std::uint32_t>(machine.fpr(index) & 0xFFFFFFFFU), 8);
        line += "  F" + std::to_string(index + 8) + " " +
                hex(static_cast<std::uint32_t>(machine.fpr(index + 8) & 0xFFFFFFFFU), 8);
        canvas.text(rect.x + 6, y, line, kText, FontRole::MonoSmall);
        y += 12;
    }
    y += 2;
    canvas.text(rect.x + 6, y, "TSP " + hex(machine.tsp(), 8), kAccent, FontRole::MonoSmall);
    y += 12;
    for (int row = 0; row < 4; ++row) {
        std::string line;
        for (int column = 0; column < 4; ++column) {
            char buffer[24] = {};
            std::snprintf(buffer, sizeof(buffer), "%7.2f", machine.tile_value(0, static_cast<std::size_t>(row * 4 + column)));
            line += buffer;
            line += " ";
        }
        canvas.text(rect.x + 6, y, line, kText, FontRole::MonoSmall);
        y += 11;
    }
    y += 2;
    canvas.text(rect.x + 6, y, "CORE " + std::to_string(machine.core_id()) + "  MODE " +
                (machine.kernel_mode() ? "KERNEL" : "USER"), kAccent, FontRole::MonoSmall);
    y += 12;
    std::uint32_t ptbr = 0;
    if (machine.read_control_register(8, ptbr)) {
        canvas.text(rect.x + 6, y, "PTBR " + hex(ptbr, 8), kDim, FontRole::MonoSmall);
        y += 12;
        for (int index = 0; index < 4; ++index) {
            std::uint32_t entry = 0;
            if (machine.read_physical32(ptbr + static_cast<std::uint32_t>(index * 4), entry)) {
                canvas.text(rect.x + 6, y, "PTE" + std::to_string(index) + " " + hex(entry, 8),
                            kDim, FontRole::MonoSmall);
                y += 12;
            }
        }
    }
    if (y > rect.y + rect.height - 10) {
        return;
    }
    for (std::uint32_t number : {3U, 6U, 10U}) {
        std::uint32_t value = 0;
        if (!machine.read_control_register(number, value)) {
            continue;
        }
        canvas.text(rect.x + 6, y, "CR" + std::to_string(number) + " " + hex(value, 8), kDim,
                    FontRole::MonoSmall);
        y += 12;
        if (y > rect.y + rect.height - 10) {
            break;
        }
    }
}

void DebuggerUI::draw_console(Canvas& canvas, Rect rect) {
    int y = rect.y + 4;
    std::istringstream stream(session_.console());
    std::string line;
    while (std::getline(stream, line) && y < rect.y + rect.height - 10) {
        canvas.text(rect.x + 6, y, trim_to(line, static_cast<std::size_t>((rect.width - 12) / 8)),
                    kText, FontRole::Mono);
        y += 16;
    }
    if (session_.console().empty()) {
        canvas.text(rect.x + 6, y, "(no console output)", kDim, FontRole::MonoSmall);
    }
}

void DebuggerUI::draw_bottom(Canvas& canvas, Rect rect) {
    canvas.fill(rect, kPanel);
    canvas.frame(rect, kBorder);
    const char* labels[] = {"MEMORY", "MACHINE", "CONSOLE"};
    for (int index = 0; index < 3; ++index) {
        const int x = rect.x + 6 + index * 100;
        if (static_cast<int>(tab_) == index) {
            canvas.fill(Rect{x, rect.y + 2, 92, 20}, Color{38, 66, 92, 255});
        }
        canvas.text(x + 7, rect.y + 5, labels[index],
                    static_cast<int>(tab_) == index ? kAccent : kDim, FontRole::Ui);
    }
    const Rect content{rect.x + 1, rect.y + 24, rect.width - 2, rect.height - 25};
    if (tab_ == Tab::Memory) {
        draw_memory(canvas, content);
    } else if (tab_ == Tab::Machine) {
        draw_machine(canvas, content);
    } else {
        draw_console(canvas, content);
    }
}

DebuggerUI::Layout DebuggerUI::layout(int width, int height) const {
    const int right_width = std::clamp(width * 23 / 100, 230, 360);
    int source_width = std::clamp(width * 36 / 100, 240, width - right_width - 300);
    int disassembly_width = width - source_width - right_width;
    if (disassembly_width < 300) {
        disassembly_width = 300;
        source_width = width - disassembly_width - right_width;
    }
    const int bottom_height = std::clamp(height / 3, 170, 240);
    const int bottom_top = height - bottom_height;
    const int top_height = bottom_top - kToolbarHeight;
    int registers_height = std::clamp(top_height / 3, 190, 220);

    Layout result;
    result.source = Rect{0, kToolbarHeight, source_width, top_height};
    result.disassembly = Rect{source_width, kToolbarHeight, disassembly_width, top_height};
    result.registers =
        Rect{source_width + disassembly_width, kToolbarHeight, right_width, registers_height};
    result.variables = Rect{source_width + disassembly_width,
                            kToolbarHeight + registers_height, right_width,
                            top_height - registers_height};
    result.bottom = Rect{0, bottom_top, width, bottom_height};
    return result;
}

void DebuggerUI::draw_error(Canvas& canvas) {
    if (error_.empty()) {
        return;
    }
    const Rect dialog{260, 240, 500, 110};
    canvas.fill(dialog, Color{44, 24, 28, 255});
    canvas.frame(dialog, kBad, 2);
    canvas.text(dialog.x + 12, dialog.y + 12, "ERROR", kBad);
    canvas.text(dialog.x + 12, dialog.y + 32, trim_to(error_, 74), kText);
    canvas.text(dialog.x + 12, dialog.y + 78, "click to dismiss", kDim);
}

void DebuggerUI::toggle_source_breakpoint(int line) {
    std::uint32_t address = 0;
    if (line <= 0 || !source_line_to_address(line, address)) {
        status_ = "no executable address on line " + std::to_string(line);
        return;
    }
    toggle_instruction_breakpoint(address);
}

void DebuggerUI::toggle_instruction_breakpoint(std::uint32_t address) {
    if (session_.breakpoints().find(address) != session_.breakpoints().end()) {
        session_.clear_breakpoint(address);
        status_ = "breakpoint cleared at " + hex(address, 8);
    } else {
        session_.set_breakpoint(address);
        status_ = "breakpoint set at " + hex(address, 8);
    }
}

void DebuggerUI::run_to(std::uint32_t address) {
    if (!loaded_ || address == 0) {
        return;
    }
    if (session_.machine().pc() == address) {
        status_ = "already at " + hex(address, 8);
        return;
    }
    const bool owned = session_.breakpoints().find(address) == session_.breakpoints().end();
    if (owned) {
        session_.set_breakpoint(address);
    }
    set_status_from_result(session_.run(200000));
    if (owned) {
        session_.clear_breakpoint(address);
    }
    if (session_.machine().pc() == address) {
        status_ = "stopped at " + hex(address, 8);
    }
}

void DebuggerUI::open_context_menu(int x, int y, int width, int height) {
    if (!loaded_) {
        return;
    }
    const Layout areas = layout(width, height);
    bool source = false;
    int line = 0;
    std::uint32_t address = 0;
    if (x >= areas.source.x && x < areas.source.x + kSourceGutter &&
        y >= areas.source.y + 24 && y < areas.source.y + areas.source.height) {
        const int row = (y - (areas.source.y + 24)) / source_line_height(areas.source.width);
        line = source_scroll_ + row + 1;
        if (line < 1 || line > static_cast<int>(source_lines_.size())) {
            return;
        }
        source = true;
    } else if (x >= areas.disassembly.x && x < areas.disassembly.x + kAsmGutter &&
               y >= areas.disassembly.y + 24 &&
               y < areas.disassembly.y + areas.disassembly.height) {
        const int row = (y - (areas.disassembly.y + 24)) / 20;
        const int rows = std::max(1, (areas.disassembly.height - 28) / 20);
        const std::uint32_t pc = session_.machine().pc();
        const std::uint32_t before = static_cast<std::uint32_t>((rows / 2) * 4);
        std::uint32_t start = pc >= before ? pc - before : 0;
        start = std::max(start, session_.machine().ram_base());
        address = start + static_cast<std::uint32_t>(row * 4);
    } else {
        return;
    }
    const int menu_width = 220;
    const int menu_height = 58;
    const int menu_x = std::clamp(x, 4, std::max(4, width - menu_width - 4));
    const int menu_y = std::clamp(y, 4, std::max(4, height - menu_height - 4));
    menu_ = ContextMenu{};
    menu_.open = true;
    menu_.source = source;
    menu_.line = line;
    menu_.address = address;
    menu_.rect = Rect{menu_x, menu_y, menu_width, menu_height};
    menu_.toggle_rect = Rect{menu_x + 1, menu_y + 2, menu_width - 2, 25};
    menu_.run_rect = Rect{menu_x + 1, menu_y + 29, menu_width - 2, 27};
    menu_.hover = -1;
}

void DebuggerUI::draw_asm_tooltip(Canvas& canvas) {
    if (menu_.open || hover_asm_index_ < 0 ||
        hover_asm_index_ >= static_cast<int>(asm_rows_.size())) {
        return;
    }
    const AsmRow& row = asm_rows_[static_cast<std::size_t>(hover_asm_index_)];
    const std::size_t split = row.text.find(' ');
    const std::string mnemonic =
        split == std::string::npos ? row.text : row.text.substr(0, split);
    const std::string title = hover_asm_operand_ ? "Addressing mode" : mnemonic;
    const std::string body =
        hover_asm_operand_ ? addressing_help(row.text, mnemonic)
                           : instruction_help(mnemonic);

    std::vector<std::string> lines;
    lines.push_back(title);
    const int max_width = 360;
    std::string line;
    std::size_t begin = 0;
    while (begin < body.size()) {
        const std::size_t end = body.find(' ', begin);
        const std::string word =
            body.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!word.empty()) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() &&
                Canvas::text_width(candidate, FontRole::MonoSmall) > max_width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.size() == 1) lines.push_back("No description available.");

    int width = 0;
    for (const std::string& value : lines) {
        width = std::max(width, Canvas::text_width(value, FontRole::MonoSmall));
    }
    width += 20;
    const int height = 8 + static_cast<int>(lines.size()) * 17;
    int x = mouse_x_ + 16;
    int y = mouse_y_ - height - 12;
    if (y < 4) y = mouse_y_ + 22;
    if (x + width > canvas.width() - 4) x = canvas.width() - width - 4;
    x = std::max(4, x);
    y = std::min(y, canvas.height() - height - 4);
    y = std::max(4, y);
    canvas.fill(Rect{x, y, width, height}, kPanelAlt);
    canvas.frame(Rect{x, y, width, height}, kAccent);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        canvas.text(x + 10, y + 4 + static_cast<int>(index) * 17, lines[index],
                    index == 0 ? kText : kDim, FontRole::MonoSmall);
    }
}

void DebuggerUI::draw_context_menu(Canvas& canvas) {
    if (!menu_.open) {
        return;
    }
    canvas.fill(menu_.rect, kPanelAlt);
    canvas.frame(menu_.rect, kAccent);
    canvas.fill(menu_.toggle_rect, menu_.hover == 0 ? Color{38, 66, 92, 255} : kPanelAlt);
    canvas.fill(menu_.run_rect, menu_.hover == 1 ? Color{38, 66, 92, 255} : kPanelAlt);
    canvas.text(menu_.rect.x + 10, menu_.toggle_rect.y + 4, "Toggle breakpoint", kText,
                FontRole::MonoSmall);
    const std::string run_label = menu_.source
                                      ? "Run to line " + std::to_string(menu_.line)
                                      : "Run to " + hex(menu_.address, 8);
    canvas.text(menu_.rect.x + 10, menu_.run_rect.y + 4, run_label, kText,
                FontRole::MonoSmall);
}

void DebuggerUI::activate_context_menu(bool run_to_address) {
    if (!menu_.open) {
        return;
    }
    const bool source = menu_.source;
    const int line = menu_.line;
    const std::uint32_t address = menu_.address;
    menu_.open = false;
    if (!run_to_address) {
        if (source) {
            toggle_source_breakpoint(line);
        } else {
            toggle_instruction_breakpoint(address);
        }
        return;
    }
    if (source) {
        std::uint32_t target = 0;
        if (!source_line_to_address(line, target)) {
            for (const debug::Line& candidate : session_.info().lines) {
                if (candidate.line >= line) {
                    target = candidate.address;
                    break;
                }
            }
        }
        run_to(target);
    } else {
        run_to(address);
    }
}

void DebuggerUI::hover(int x, int y, int width, int height) {
    mouse_x_ = x;
    mouse_y_ = y;
    if (menu_.open) {
        const int hover = menu_.toggle_rect.contains(x, y)
                              ? 0
                              : (menu_.run_rect.contains(x, y) ? 1 : -1);
        menu_.hover = hover;
        return;
    }
    const Layout areas = layout(width, height);
    hover_row_ = -1;
    hover_source_ = false;
    hover_asm_index_ = -1;
    hover_asm_operand_ = false;
    if (x >= areas.source.x && x < areas.source.x + kSourceGutter &&
        y >= areas.source.y + 24 && y < areas.source.y + areas.source.height) {
        hover_row_ = (y - (areas.source.y + 24)) / source_line_height(areas.source.width);
        hover_source_ = true;
    } else if (x >= areas.disassembly.x && x < areas.disassembly.x + kAsmGutter &&
               y >= areas.disassembly.y + 24 &&
               y < areas.disassembly.y + areas.disassembly.height) {
        hover_row_ = (y - (areas.disassembly.y + 24)) / 20;
        hover_source_ = false;
    } else if (x >= areas.disassembly.x &&
               x < areas.disassembly.x + areas.disassembly.width &&
               y >= areas.disassembly.y + 24 &&
               y < areas.disassembly.y + areas.disassembly.height) {
        for (std::size_t index = 0; index < asm_rows_.size(); ++index) {
            if (!asm_rows_[index].row.contains(x, y)) continue;
            if (asm_rows_[index].operand.contains(x, y)) {
                hover_asm_index_ = static_cast<int>(index);
                hover_asm_operand_ = true;
            } else if (asm_rows_[index].mnemonic.contains(x, y)) {
                hover_asm_index_ = static_cast<int>(index);
                hover_asm_operand_ = false;
            }
            break;
        }
    }
}

bool DebuggerUI::context_menu(int x, int y, int width, int height) {
    open_context_menu(x, y, width, height);
    return menu_.open;
}

void DebuggerUI::render(Canvas& canvas) {
    canvas.clear(kBackground);
    draw_toolbar(canvas);
    if (!loaded_) {
        canvas.text(40, 100, "KR32 WINDOWED DEBUGGER", kAccent, FontRole::UiBold);
        canvas.text(40, 140, "usage: kr32 gui <image.k32>", kText, FontRole::Mono);
        canvas.text(40, 160, "F5 run  F10 over  F11 step  shift+F11 out  F9 breakpoint",
                    kDim, FontRole::MonoSmall);
        draw_error(canvas);
        return;
    }
    const Layout areas = layout(canvas.width(), canvas.height());
    draw_source(canvas, areas.source);
    draw_disassembly(canvas, areas.disassembly);
    draw_registers(canvas, areas.registers);
    draw_variables(canvas, areas.variables);
    draw_bottom(canvas, areas.bottom);
    draw_error(canvas);
    draw_asm_tooltip(canvas);
    draw_context_menu(canvas);
}

bool DebuggerUI::click(int x, int y, int width, int height) {
    if (!error_.empty()) {
        error_.clear();
        return true;
    }
    if (menu_.open) {
        if (menu_.toggle_rect.contains(x, y)) {
            activate_context_menu(false);
        } else if (menu_.run_rect.contains(x, y)) {
            activate_context_menu(true);
        } else {
            menu_.open = false;
        }
        return true;
    }
    const Layout areas = layout(width, height);
    if (y < kToolbarHeight) {
        if (x >= 82 && x < 122) {
            run();
        } else if (x >= 126 && x < 170) {
            step();
        } else if (x >= 174 && x < 218) {
            step_over();
        } else if (x >= 222 && x < 258) {
            step_out();
        } else if (x >= 262 && x < 306) {
            toggle_breakpoint_at_current_line();
        } else if (x >= 310 && x < 344) {
            restart();
        } else if (x >= 348 && x < 392) {
            restart();
        } else if (x >= 396 && x < 444) {
            restart();
        }
        return true;
    }
    if (y >= areas.bottom.y + 2 && y < areas.bottom.y + 22) {
        if (x < areas.bottom.x + 106) {
            tab_ = Tab::Memory;
        } else if (x < areas.bottom.x + 206) {
            tab_ = Tab::Machine;
        } else if (x < areas.bottom.x + 306) {
            tab_ = Tab::Console;
        }
        return true;
    }
    if (y >= areas.source.y + 2 && y < areas.source.y + 20) {
        const int button_start = areas.source.x + areas.source.width - 3 * 42 - 4;
        if (x >= button_start && x < button_start + 3 * 42) {
            const int index = (x - button_start) / 42;
            if (index == 0) step_line_over();
            if (index == 1) step_line_in();
            if (index == 2) step_line_out();
            return true;
        }
    }
    if (y >= areas.disassembly.y + 2 && y < areas.disassembly.y + 20) {
        const int button_start = areas.disassembly.x + areas.disassembly.width - 3 * 40 - 4;
        if (x >= button_start && x < button_start + 3 * 40) {
            const int index = (x - button_start) / 40;
            if (index == 0) step_over();
            if (index == 1) step();
            if (index == 2) step_out();
            return true;
        }
    }
    if (x < areas.source.x + kSourceGutter && y >= areas.source.y + 24 &&
        y < areas.source.y + areas.source.height) {
        const int row = (y - (areas.source.y + 24)) / source_line_height(areas.source.width);
        const int line = source_scroll_ + row + 1;
        toggle_source_breakpoint(line);
        return true;
    }
    if (x >= areas.disassembly.x && x < areas.disassembly.x + kAsmGutter &&
        y >= areas.disassembly.y + 24 && y < areas.disassembly.y + areas.disassembly.height) {
        const int row = (y - (areas.disassembly.y + 24)) / 20;
        const std::uint32_t pc = session_.machine().pc();
        const int rows = std::max(1, (areas.disassembly.height - 28) / 20);
        const std::uint32_t before = static_cast<std::uint32_t>((rows / 2) * 4);
        std::uint32_t start = pc >= before ? pc - before : 0;
        start = std::max(start, session_.machine().ram_base());
        toggle_instruction_breakpoint(start + static_cast<std::uint32_t>(row * 4));
        return true;
    }
    return false;
}

bool DebuggerUI::key(int key_value, bool shift) {
    if (key_value == kr32::platform::key::Escape) {
        if (menu_.open) {
            menu_.open = false;
        } else {
            quit_ = true;
        }
        return true;
    }
    if (key_value == kr32::platform::key::F5) {
        run();
        return true;
    }
    if (key_value == kr32::platform::key::F10) {
        step_over();
        return true;
    }
    if (key_value == kr32::platform::key::F11) {
        if (shift) {
            step_out();
        } else {
            step();
        }
        return true;
    }
    if (key_value == kr32::platform::key::F9) {
        toggle_breakpoint_at_current_line();
        return true;
    }
    if (key_value == 'R' || key_value == 'r') {
        restart();
        return true;
    }
    if (key_value == kr32::platform::key::Tab) {
        tab_ = static_cast<Tab>((static_cast<int>(tab_) + 1) % 3);
        return true;
    }
    return false;
}

bool DebuggerUI::headless(const std::string& ppm_path) {
    const Tab original_tab = tab_;
    Canvas canvas(1440, 900);

    tab_ = Tab::Memory;
    render(canvas);
    const std::uint64_t memory_hash = canvas.hash();
    if (!ppm_path.empty() && !canvas.write_ppm(ppm_path)) {
        error_ = "cannot write " + ppm_path;
        return false;
    }

    tab_ = Tab::Machine;
    render(canvas);
    const std::uint64_t machine_hash = canvas.hash();
    if (!ppm_path.empty()) {
        std::string machine_path = ppm_path;
        const std::size_t dot = machine_path.find_last_of('.');
        if (dot != std::string::npos) {
            machine_path.insert(dot, "-machine");
        }
        if (!canvas.write_ppm(machine_path)) {
            error_ = "cannot write " + machine_path;
            return false;
        }
    }

    tab_ = Tab::Console;
    render(canvas);
    const std::uint64_t console_hash = canvas.hash();
    tab_ = original_tab;
    std::printf("ui-render width=%d height=%d hash=%016llX machine=%016llX console=%016llX\n",
                canvas.width(), canvas.height(), static_cast<unsigned long long>(memory_hash),
                static_cast<unsigned long long>(machine_hash),
                static_cast<unsigned long long>(console_hash));
    return true;
}

bool render_headless(const std::string& image_path,
                     const std::string& ppm_path,
                     std::uint64_t& hash,
                     std::string& error) {
    DebuggerUI debugger;
    if (!debugger.load(image_path, error)) {
        return false;
    }
    Canvas canvas(1440, 900);
    debugger.render(canvas);
    if (!ppm_path.empty() && !canvas.write_ppm(ppm_path)) {
        error = "cannot write " + ppm_path;
        return false;
    }
    hash = canvas.hash();
    return true;
}

} // namespace kr32::ui
