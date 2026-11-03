#include "kr32/ui/debugger_ui.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/cxx/cxx.h"
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

bool compile_cxx_source(const std::string& path, container::Image& image, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    cxx::Options options;
    options.source_name = path;
    const cxx::Result compiled = cxx::compile(source, options);
    if (!compiled.ok) {
        if (!compiled.diagnostics.empty()) {
            const cxx::Diagnostic& diagnostic = compiled.diagnostics.front();
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

bool has_cxx_extension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == "cpp" || extension == "cc" || extension == "cxx";
}

} // namespace

std::string DebuggerUI::format_fpr(std::uint64_t value, bool scientific) const {
    const double number = std::bit_cast<double>(value);
    if (std::isnan(number)) return "nan";
    if (std::isinf(number)) return std::signbit(number) ? "-inf" : "+inf";
    std::ostringstream out;
    out << std::showpos;
    if (scientific) {
        out << std::scientific << std::setprecision(10) << number;
    } else {
        out << std::defaultfloat << std::setprecision(17) << number;
    }
    return out.str();
}

std::string DebuggerUI::format_fpr_float(std::uint32_t value, bool scientific) const {
    const float number = std::bit_cast<float>(value);
    if (std::isnan(number)) return "nan";
    if (std::isinf(number)) return std::signbit(number) ? "-inf" : "+inf";
    std::ostringstream out;
    out << std::showpos;
    if (scientific) {
        out << std::scientific << std::setprecision(6) << number;
    } else {
        out << std::defaultfloat << std::setprecision(8) << number;
    }
    return out.str();
}

bool DebuggerUI::load(const std::string& path, std::string& error) {
    std::string source_path;
    if (has_c_extension(path)) {
        if (!compile_c_source(path, image_, error)) {
            return false;
        }
        source_path = path;
    } else if (has_cxx_extension(path)) {
        if (!compile_cxx_source(path, image_, error)) {
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
    source_cache_.clear();
    source_view_id_ = -1;
    source_view_path_.clear();
    load_source_files();
    int initial_source = -1;
    if (!session_.info().sources.empty()) {
        initial_source = session_.info().sources.front().id;
    }
    if (!source_path.empty()) {
        for (const debug::Source& source : session_.info().sources) {
            if (source.path == source_path) {
                initial_source = source.id;
                break;
            }
        }
        if (initial_source < 0) {
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
                source_view_path_ = source_path;
            }
        }
    }
    if (initial_source >= 0) {
        const auto cached = source_cache_.find(initial_source);
        if (cached != source_cache_.end()) {
            source_lines_ = cached->second;
            source_view_id_ = initial_source;
            source_view_path_ = source_path_for(initial_source);
        }
    }
    loaded_ = true;
    source_scroll_ = 0;
    error_.clear();
    prepare_initial_stop();
    memory_address_ = session_.machine().sp() & ~0xFU;
    hover_memory_byte_ = -1;
    status_ = "paused at " + hex(session_.machine().pc(), 8);
    return true;
}

void DebuggerUI::show_error(std::string message) {
    error_ = std::move(message);
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
    source_follow_ = true;
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

std::string DebuggerUI::source_path_for(int source_id) const {
    for (const debug::Source& source : session_.info().sources) {
        if (source.id == source_id) {
            return source.path;
        }
    }
    return {};
}

void DebuggerUI::load_source_files() {
    for (const debug::Source& source : session_.info().sources) {
        if (source.path.empty() || source_cache_.find(source.id) != source_cache_.end()) {
            continue;
        }
        std::ifstream input(source.path);
        if (!input) {
            input.open("../" + source.path);
        }
        if (!input) {
            input.open("../../" + source.path);
        }
        std::vector<std::string> lines;
        if (input) {
            std::string line;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }
        }
        source_cache_[source.id] = std::move(lines);
    }
}

void DebuggerUI::ensure_source_for_current() {
    const debug::Line* line = session_.line_at(session_.machine().pc());
    if (line == nullptr) {
        return;
    }
    const int source_id = line->source;
    if (source_id == source_view_id_) {
        return;
    }
    auto cached = source_cache_.find(source_id);
    if (cached == source_cache_.end()) {
        load_source_files();
        cached = source_cache_.find(source_id);
    }
    if (cached == source_cache_.end()) {
        return;
    }
    source_view_id_ = source_id;
    source_view_path_ = source_path_for(source_id);
    source_lines_ = cached->second;
    source_scroll_ = 0;
    source_follow_ = true;
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
        if (candidate.source == source_view_id_ && candidate.line == line) {
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
    ensure_source_for_current();
    std::string title = "SOURCE";
    if (!source_view_path_.empty()) {
        title += "  " + source_view_path_;
    } else if (!session_.info().sources.empty()) {
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
    if (current > 0 && source_follow_ &&
        (current - 1 < source_scroll_ || current - 1 >= source_scroll_ + visible)) {
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
    draw_scrollbar(canvas, rect, source_scroll_, static_cast<int>(source_lines_.size()),
                   visible, source_scroll_track_, source_scroll_thumb_);
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
    std::uint32_t before = static_cast<std::uint32_t>((rows / 2) * 4);
    std::uint32_t start = pc >= before ? pc - before : 0;
    start = std::max(start, session_.machine().ram_base());
    const std::int64_t scrolled =
        static_cast<std::int64_t>(start / 4U) + asm_scroll_;
    const std::uint32_t max_row = session_.machine().ram_size() / 4U;
    const std::uint32_t start_row = static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(scrolled, session_.machine().ram_base() / 4U,
                                 static_cast<std::int64_t>(max_row)));
    start = start_row * 4U;
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
    draw_scrollbar(canvas, rect,
                   static_cast<int>(start_row - session_.machine().ram_base() / 4U),
                   static_cast<int>(max_row), rows, asm_scroll_track_,
                   asm_scroll_thumb_);
}

void DebuggerUI::draw_cores(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "CORES");
    core_rows_.clear();
    register_cells_.clear();
    fpr_cells_.clear();
    flag_cells_.clear();
    const std::size_t count = session_.core_count();
    if (count == 0) {
        canvas.text(rect.x + 8, rect.y + 26, "(no cores)", kDim, FontRole::MonoSmall);
        return;
    }
    if (selected_core_ >= count) selected_core_ = 0;
    if (expanded_core_ > count) expanded_core_ = count;
    const int visible_rows = std::max(1, (rect.height - 26) / 14);
    const int total_rows = 80;
    cores_scroll_ = std::clamp(cores_scroll_, 0, std::max(0, total_rows - visible_rows) * 14);
    int y = rect.y + 24 - cores_scroll_;
    for (std::size_t index = 0; index < count; ++index) {
        const bool expanded = index == expanded_core_;
        const Rect row{rect.x + 1, y, rect.width - 12, 18};
        if (index == selected_core_) {
            canvas.fill(row, Color{38, 66, 92, 255});
        }
        const core::Machine& machine = session_.core(index);
        canvas.text(rect.x + 6, y + 2, expanded ? "v" : ">", kAccent,
                    FontRole::MonoSmall);
        canvas.text(rect.x + 20, y + 2,
                    "Core " + std::to_string(index) + "  PC " + hex(machine.pc(), 8),
                    index == selected_core_ ? kText : kDim, FontRole::MonoSmall);
        core_rows_.push_back(CoreRow{index, row, expanded});
        y += 18;
        if (expanded) {
            const int used = draw_core_state(
                canvas, Rect{rect.x + 6, y + 2, rect.width - 18,
                             std::max(0, rect.y + rect.height - y - 4)},
                index);
            y += used + 6;
        }
        if (y > rect.y + rect.height + 400) {
            break;
        }
    }
    draw_scrollbar(canvas, rect, cores_scroll_ / 14, total_rows, visible_rows,
                   cores_scroll_track_, cores_scroll_thumb_);
}

int DebuggerUI::draw_core_state(Canvas& canvas, Rect rect, std::size_t core_index) {
    const core::Machine& machine = session_.core(core_index);
    const int start_y = rect.y;
    int y = rect.y;
    register_cells_.clear();
    fpr_cells_.clear();
    flag_cells_.clear();
    const int column_width = std::max(100, (rect.width - 8) / 2);

    pc_cell_ = Rect{rect.x - 2, y - 2, column_width - 4, 18};
    sp_cell_ = Rect{rect.x + column_width - 2, y - 2, column_width - 4, 18};
    if (editing_register_ == 16) {
        canvas.fill(pc_cell_, Color{44, 64, 84, 255});
        canvas.frame(pc_cell_, kAccent);
    }
    if (editing_register_ == 17) {
        canvas.fill(sp_cell_, Color{44, 64, 84, 255});
        canvas.frame(sp_cell_, kAccent);
    }
    if (hover_register_ == 16) {
        canvas.fill(pc_cell_, Color{42, 58, 74, 255});
        canvas.frame(pc_cell_, kAccent);
    }
    if (hover_register_ == 17) {
        canvas.fill(sp_cell_, Color{42, 58, 74, 255});
        canvas.frame(sp_cell_, kAccent);
    }
    canvas.text(rect.x, y,
                "PC " + (editing_register_ == 16 ? register_edit_ + "_" : hex(machine.pc(), 8)),
                editing_register_ == 16 ? kAccent : kText, FontRole::MonoSmall);
    canvas.text(rect.x + column_width, y,
                "SP " + (editing_register_ == 17 ? register_edit_ + "_" : hex(machine.sp(), 8)),
                editing_register_ == 17 ? kAccent : kText, FontRole::MonoSmall);
    y += 20;

    static const char* flag_names[] = {"Z", "N", "C", "V", "I", "K"};
    static const std::uint32_t flag_bits[] = {core::kFlagZ, core::kFlagN, core::kFlagC,
                                              core::kFlagV, core::kFlagI, core::kFlagK};
    canvas.text(rect.x, y, "FLAGS", kDim, FontRole::MonoSmall);
    const int flag_start = rect.x + 52;
    for (int index = 0; index < 6; ++index) {
        const Rect box{flag_start + index * 20, y - 2, 17, 17};
        flag_cells_.push_back(box);
        const bool set = (machine.flags() & flag_bits[index]) != 0;
        canvas.fill(box, set ? Color{40, 112, 80, 255} : kPanelAlt);
        canvas.frame(box, hover_flag_ == index ? kAccent : kBorder);
        canvas.text(box.x + 4, box.y + 2, flag_names[index], set ? kText : kDim,
                    FontRole::MonoSmall);
    }
    canvas.text(rect.x + 178, y,
                (machine.kernel_mode() ? "KERNEL" : "USER"), kDim, FontRole::MonoSmall);
    y += 22;
    canvas.text(rect.x, y, "TICKS " + decimal(machine.ticks()) + "  CORE " +
                               std::to_string(machine.core_id()),
                kDim, FontRole::MonoSmall);
    y += 20;

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 2; ++column) {
            const int index = row + column * 8;
            const int x = rect.x + column * column_width;
            const Rect cell{x - 2, y - 2, column_width - 4, 16};
            register_cells_.push_back(cell);
            if (editing_register_ == index) {
                canvas.fill(cell, Color{44, 64, 84, 255});
                canvas.frame(cell, kAccent);
            } else if (hover_register_ == index) {
                canvas.fill(cell, Color{42, 58, 74, 255});
                canvas.frame(cell, kAccent);
            }
            canvas.text(x, y, "R" + std::to_string(index), kDim, FontRole::MonoSmall);
            canvas.text(x + 24, y,
                        editing_register_ == index
                            ? register_edit_ + "_"
                            : hex(machine.reg(static_cast<std::size_t>(index)), 8),
                        editing_register_ == index ? kAccent : kText, FontRole::MonoSmall);
        }
        y += 17;
    }

    canvas.fill(Rect{rect.x, y + 2, rect.width, 1}, kBorder);
    y += 8;
    canvas.text(rect.x, y, "FPR", kDim, FontRole::MonoSmall);
    fpr_view_rect_ = Rect{rect.x + rect.width - 62, y - 3, 58, 17};
    canvas.fill(fpr_view_rect_, kPanelAlt);
    canvas.frame(fpr_view_rect_, kBorder);
    canvas.text(fpr_view_rect_.x + 6, fpr_view_rect_.y + 2,
                fpr_float_view_ ? "FLOAT" : "DOUBLE", kText, FontRole::MonoSmall);
    y += 17;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 2; ++column) {
            const std::int32_t index = row + column * 8;
            const int x = rect.x + column * column_width;
            const std::uint64_t bits = machine.fpr(static_cast<std::size_t>(index));
            const std::uint32_t low_bits = static_cast<std::uint32_t>(bits & 0xFFFFFFFFU);
            const bool float_like = !fpr_float_view_ && (bits >> 32) == 0 && low_bits != 0;
            std::string display =
                fpr_float_view_
                    ? format_fpr_float(low_bits, false)
                    : format_fpr(bits, false);
            if (float_like) {
                display = format_fpr_float(low_bits, false) + " f32";
            }
            if (Canvas::text_width(display, FontRole::MonoSmall) > column_width - 36) {
                if (float_like) {
                    display = format_fpr_float(low_bits, true) + " f32";
                } else {
                    display =
                        fpr_float_view_ ? format_fpr_float(low_bits, true)
                                        : format_fpr(bits, true);
                }
            }
            const Rect cell{x - 2, y - 2, column_width - 4, 16};
            fpr_cells_.push_back(cell);
            if (hover_fpr_ == index) {
                canvas.fill(cell, Color{42, 58, 74, 255});
                canvas.frame(cell, kAccent);
            }
            canvas.text(x, y, "F" + std::to_string(index), kDim, FontRole::MonoSmall);
            canvas.text(x + 28, y, display, kText, FontRole::MonoSmall);
        }
        y += 17;
    }

    canvas.fill(Rect{rect.x, y + 2, rect.width, 1}, kBorder);
    y += 8;
    canvas.text(rect.x, y, "TSP " + hex(machine.tsp(), 8), kAccent, FontRole::MonoSmall);
    y += 14;
    for (int row = 0; row < 4; ++row) {
        std::string line;
        for (int column = 0; column < 4; ++column) {
            char buffer[24] = {};
            std::snprintf(buffer, sizeof(buffer), "%7.2f",
                          machine.tile_value(0, static_cast<std::size_t>(row * 4 + column)));
            line += buffer;
            line += " ";
        }
        canvas.text(rect.x, y, line, kText, FontRole::MonoSmall);
        y += 11;
    }

    canvas.fill(Rect{rect.x, y + 2, rect.width, 1}, kBorder);
    y += 8;
    for (std::uint32_t number : {3U, 6U, 10U}) {
        std::uint32_t value = 0;
        if (machine.read_control_register(number, value)) {
            canvas.text(rect.x, y, "CR" + std::to_string(number) + " " + hex(value, 8),
                        kDim, FontRole::MonoSmall);
            y += 12;
        }
    }
    std::uint32_t ptbr = 0;
    if (machine.read_control_register(8, ptbr)) {
        canvas.text(rect.x, y, "PTBR " + hex(ptbr, 8), kDim, FontRole::MonoSmall);
        y += 12;
        for (int index = 0; index < 4; ++index) {
            std::uint32_t entry = 0;
            if (machine.read_physical32(ptbr + static_cast<std::uint32_t>(index * 4), entry)) {
                canvas.text(rect.x, y, "PTE" + std::to_string(index) + " " + hex(entry, 8),
                            kDim, FontRole::MonoSmall);
                y += 12;
            }
        }
    }
    return std::min(y - start_y, rect.height);
}

void DebuggerUI::draw_variables(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "VARIABLES");
    struct Entry {
        std::string name;
        std::string type;
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        std::uint32_t size = 0;
        bool local = false;
        bool ok = false;
        int indent = 0;
    };
    std::vector<Entry> entries;
    const debug::Function* function = session_.function_at(session_.machine().pc());
    if (function != nullptr) {
        for (const debug::Variable& variable : session_.info().variables) {
            if (variable.function != function->name) continue;
            Entry entry;
            entry.name = variable.name;
            entry.type = variable.type;
            entry.address = session_.machine().reg(7) + variable.offset;
            entry.size = variable.size;
            entry.local = true;
            std::string error;
            entry.ok = session_.read_variable(variable.name, entry.value, error);
            entries.push_back(entry);
            if (entry.ok && !variable.type.empty() && variable.type.back() == '*') {
                for (const debug::ClassInfo& klass : session_.info().classes) {
                    if (variable.type.find(klass.name) == std::string::npos) {
                        continue;
                    }
                    for (const debug::Member& member : klass.members) {
                        Entry member_entry;
                        member_entry.name = "." + member.name;
                        member_entry.type = member.type;
                        member_entry.address = entry.value + member.offset;
                        member_entry.size = member.size;
                        member_entry.local = true;
                        member_entry.indent = 1;
                        member_entry.ok = session_.read_memory(
                            member_entry.address, member_entry.size, member_entry.value);
                        entries.push_back(std::move(member_entry));
                    }
                    break;
                }
            }
        }
    }
    for (const debug::Global& global : session_.info().globals) {
        Entry entry;
        entry.name = global.name;
        entry.type = global.type;
        entry.address = global.address;
        entry.size = global.size;
        std::string error;
        entry.ok = session_.read_global(global.name, entry.value, error);
        entries.push_back(std::move(entry));
    }

    const int row_height = 14;
    const int visible = std::max(1, (rect.height - 26) / row_height);
    variables_scroll_ =
        std::clamp(variables_scroll_, 0, std::max(0, static_cast<int>(entries.size()) - visible));
    var_hover_rows_.clear();
    int visible_row = 0;
    for (int index = variables_scroll_;
         index < static_cast<int>(entries.size()) && visible_row < visible;
         ++index, ++visible_row) {
        const Entry& entry = entries[static_cast<std::size_t>(index)];
        const int y = rect.y + 24 + visible_row * row_height;
        const Rect row{rect.x + 1, y - 1, rect.width - 12, row_height};
        if (hover_var_ == visible_row) {
            canvas.fill(row, Color{42, 58, 74, 255});
        }
        const std::string indent_text(static_cast<std::size_t>(entry.indent * 2), ' ');
        const std::string text =
            indent_text + entry.name + " = " +
            (entry.ok ? "0x" + hex(entry.value, 8) : "?");
        canvas.text(rect.x + 6, y, trim_to(text, static_cast<std::size_t>((rect.width - 20) / 7)),
                    entry.ok ? (entry.indent > 0 ? kDim : kText) : kBad,
                    FontRole::MonoSmall);
        var_hover_rows_.push_back(
            VarHoverRow{row, entry.name, entry.type, entry.address, entry.value, entry.size,
                        entry.local, entry.ok});
    }
    if (entries.empty()) {
        canvas.text(rect.x + 6, rect.y + 24, "(no variables)", kDim, FontRole::MonoSmall);
    }
    draw_scrollbar(canvas, rect, variables_scroll_, static_cast<int>(entries.size()), visible,
                   variables_scroll_track_, variables_scroll_thumb_);
}

void DebuggerUI::begin_register_edit(int index) {
    if (index < 0 || index > 17) return;
    const core::Machine& machine = session_.core(selected_core_);
    editing_register_ = index;
    if (index == 16) {
        register_edit_ = hex(machine.pc(), 8);
    } else if (index == 17) {
        register_edit_ = hex(machine.sp(), 8);
    } else {
        register_edit_ = hex(machine.reg(static_cast<std::size_t>(index)), 8);
    }
    register_edit_fresh_ = true;
}

void DebuggerUI::finish_register_edit(bool commit) {
    if (editing_register_ < 0) return;
    if (commit && !register_edit_.empty()) {
        unsigned value = 0;
        bool valid = true;
        for (char ch : register_edit_) {
            const int digit = std::isdigit(static_cast<unsigned char>(ch)) != 0
                                  ? ch - '0'
                                  : (std::isxdigit(static_cast<unsigned char>(ch)) != 0
                                         ? std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10
                                         : -1);
            if (digit < 0) {
                valid = false;
                break;
            }
            value = (value << 4) | static_cast<unsigned>(digit);
        }
        if (valid) {
            core::Machine& machine = session_.core(selected_core_);
            if (editing_register_ == 16) {
                machine.set_pc(value & ~0x3U);
            } else if (editing_register_ == 17) {
                machine.set_sp(value & ~0x3U);
            } else {
                machine.set_reg(static_cast<std::size_t>(editing_register_), value);
            }
            status_ = "register updated";
        }
    }
    editing_register_ = -1;
    register_edit_.clear();
    register_edit_fresh_ = false;
}

void DebuggerUI::begin_fpr_edit(int index) {
    if (index < 0 || index > 15) return;
    editing_fpr_ = index;
    const std::uint64_t bits =
        session_.core(selected_core_).fpr(static_cast<std::size_t>(index));
    fpr_edit_ = fpr_float_view_
                    ? format_fpr_float(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU), false)
                    : format_fpr(bits, false);
    fpr_edit_fresh_ = true;
}

void DebuggerUI::finish_fpr_edit(bool commit) {
    if (editing_fpr_ < 0) return;
    if (commit && !fpr_edit_.empty()) {
        char* end = nullptr;
        if (fpr_float_view_) {
            const float value = std::strtof(fpr_edit_.c_str(), &end);
            if (end != nullptr && *end == '\0') {
                session_.core(selected_core_)
                    .set_fpr(static_cast<std::size_t>(editing_fpr_),
                             static_cast<std::uint64_t>(
                                 std::bit_cast<std::uint32_t>(value)));
                status_ = "F" + std::to_string(editing_fpr_) + " updated (float)";
            } else {
                status_ = "invalid floating-point value";
            }
        } else {
            const double value = std::strtod(fpr_edit_.c_str(), &end);
            if (end != nullptr && *end == '\0') {
                session_.core(selected_core_)
                    .set_fpr(static_cast<std::size_t>(editing_fpr_),
                             std::bit_cast<std::uint64_t>(value));
                status_ = "F" + std::to_string(editing_fpr_) + " updated (double)";
            } else {
                status_ = "invalid floating-point value";
            }
        }
    }
    editing_fpr_ = -1;
    fpr_edit_.clear();
    fpr_edit_fresh_ = false;
}

void DebuggerUI::draw_fpr_popup(Canvas& canvas) {
    if (editing_fpr_ < 0) return;
    const int width = 430;
    const int height = 178;
    const int x = (canvas.width() - width) / 2;
    const int y = (canvas.height() - height) / 2;
    fpr_popup_ = Rect{x, y, width, height};
    fpr_input_rect_ = Rect{x + 16, y + 42, width - 32, 24};
    fpr_ok_rect_ = Rect{x + width - 190, y + height - 40, 80, 24};
    fpr_cancel_rect_ = Rect{x + width - 100, y + height - 40, 84, 24};
    canvas.fill(fpr_popup_, Color{18, 26, 34, 255});
    canvas.frame(fpr_popup_, kAccent, 2);
    canvas.text(x + 16, y + 14,
                "Edit F" + std::to_string(editing_fpr_) +
                    (fpr_float_view_ ? "  (float)" : "  (double)"),
                kAccent, FontRole::UiBold);
    canvas.text(x + 16, y + 28,
                "Formats: 3.14152   100.00   100   1E2",
                kDim, FontRole::MonoSmall);
    canvas.fill(fpr_input_rect_, kPanelAlt);
    canvas.frame(fpr_input_rect_, kAccent);
    canvas.text(fpr_input_rect_.x + 6, fpr_input_rect_.y + 4, fpr_edit_ + "_", kText,
                FontRole::MonoSmall);
    const auto preview = [&](bool scientific) {
        char* end = nullptr;
        if (fpr_float_view_) {
            const float value = std::strtof(fpr_edit_.c_str(), &end);
            if (end == nullptr || *end != '\0') return std::string("invalid");
            return format_fpr_float(std::bit_cast<std::uint32_t>(value), scientific);
        }
        const double value = std::strtod(fpr_edit_.c_str(), &end);
        if (end == nullptr || *end != '\0') return std::string("invalid");
        return format_fpr(std::bit_cast<std::uint64_t>(value), scientific);
    };
    canvas.text(x + 16, y + 76, "Decimal: " + preview(false), kDim, FontRole::MonoSmall);
    canvas.text(x + 16, y + 92, "Scientific: " + preview(true), kDim, FontRole::MonoSmall);
    canvas.fill(fpr_ok_rect_, kPanelAlt);
    canvas.frame(fpr_ok_rect_, kBorder);
    canvas.text(fpr_ok_rect_.x + 26, fpr_ok_rect_.y + 4, "OK", kText, FontRole::MonoSmall);
    canvas.fill(fpr_cancel_rect_, kPanelAlt);
    canvas.frame(fpr_cancel_rect_, kBorder);
    canvas.text(fpr_cancel_rect_.x + 10, fpr_cancel_rect_.y + 4, "CANCEL", kText,
                FontRole::MonoSmall);
}

void DebuggerUI::draw_fpr_tooltip(Canvas& canvas) {
    if (hover_fpr_ < 0 || hover_fpr_ > 15 || editing_fpr_ >= 0 || menu_.open) return;
    const std::uint64_t bits =
        session_.core(selected_core_).fpr(static_cast<std::size_t>(hover_fpr_));
    const std::string decimal_text =
        fpr_float_view_ ? format_fpr_float(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU), false)
                        : format_fpr(bits, false);
    const std::string scientific_text =
        fpr_float_view_ ? format_fpr_float(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU), true)
                        : format_fpr(bits, true);
    std::vector<std::string> lines{
        "F" + std::to_string(hover_fpr_) + (fpr_float_view_ ? "  (float)" : "  (double)"),
        "decimal: " + decimal_text,
        "scientific: " + scientific_text,
    };
    if ((bits >> 32) == 0) {
        lines.push_back(
            "float: " + format_fpr_float(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU), false));
    }
    lines.push_back("hex: 0x" + hex(static_cast<std::uint32_t>(bits >> 32), 8) +
                    hex(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU), 8));
    int width = 0;
    for (const std::string& line : lines) {
        width = std::max(width, Canvas::text_width(line, FontRole::MonoSmall));
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

void DebuggerUI::draw_register_tooltip(Canvas& canvas) {
    if (hover_register_ < 0 || hover_register_ > 17 || editing_register_ >= 0 ||
        menu_.open) {
        return;
    }
    const core::Machine& machine = session_.core(selected_core_);
    std::uint32_t value = 0;
    std::string name;
    if (hover_register_ == 16) {
        name = "PC";
        value = machine.pc();
    } else if (hover_register_ == 17) {
        name = "SP";
        value = machine.sp();
    } else {
        name = "R" + std::to_string(hover_register_);
        value = machine.reg(static_cast<std::size_t>(hover_register_));
    }
    const std::int32_t signed_value = static_cast<std::int32_t>(value);
    std::vector<std::string> lines{name, "0x" + hex(value, 8),
                                   "unsigned: " + std::to_string(value)};
    if (signed_value < 0) {
        lines.push_back("signed: " + std::to_string(signed_value));
    }
    if (value >= 32 && value < 127) {
        lines.push_back(std::string("char: '") + static_cast<char>(value) + "'");
    }

    int width = 0;
    for (const std::string& line : lines) {
        width = std::max(width, Canvas::text_width(line, FontRole::MonoSmall));
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

void DebuggerUI::draw_flag_tooltip(Canvas& canvas) {
    if (hover_flag_ < 0 || hover_flag_ > 5 || menu_.open) {
        return;
    }
    static const char* names[] = {"Zero (Z)", "Negative (N)", "Carry (C)",
                                  "Overflow (V)", "Interrupt (I)", "Kernel (K)"};
    static const char* descriptions[] = {
        "result is zero", "result sign bit is set", "unsigned carry/borrow",
        "signed overflow", "interrupts enabled", "kernel mode"};
    static const std::uint32_t bits[] = {core::kFlagZ, core::kFlagN,
                                         core::kFlagC, core::kFlagV,
                                         core::kFlagI, core::kFlagK};
    const bool set = (session_.core(selected_core_).flags() & bits[hover_flag_]) != 0;
    std::vector<std::string> lines{names[hover_flag_], set ? "set" : "clear",
                                   descriptions[hover_flag_]};
    int width = 0;
    for (const std::string& line : lines) {
        width = std::max(width, Canvas::text_width(line, FontRole::MonoSmall));
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

void DebuggerUI::draw_memory_tooltip(Canvas& canvas) {
    if (hover_memory_byte_ < 0 ||
        hover_memory_byte_ >= static_cast<int>(memory_byte_cells_.size()) || menu_.open) {
        return;
    }
    const std::uint32_t address =
        memory_byte_cells_[static_cast<std::size_t>(hover_memory_byte_)].address;
    std::uint32_t value = 0;
    if (!session_.read_memory(address, 1, value)) {
        return;
    }
    std::string ascii;
    if (value >= 32 && value < 127) {
        ascii = std::string("  '") + static_cast<char>(value) + "'";
    }
    std::vector<std::string> lines{
        "Byte 0x" + hex(address, 8),
        "0x" + hex(value, 2) + ascii,
        "unsigned: " + std::to_string(value),
        "signed: " + std::to_string(static_cast<std::int8_t>(value)),
    };
    int width = 0;
    for (const std::string& line : lines) {
        width = std::max(width, Canvas::text_width(line, FontRole::MonoSmall));
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

void DebuggerUI::draw_variable_tooltip(Canvas& canvas) {
    if (hover_var_ < 0 || hover_var_ >= static_cast<int>(var_hover_rows_.size()) ||
        menu_.open) {
        return;
    }
    const VarHoverRow& row = var_hover_rows_[static_cast<std::size_t>(hover_var_)];
    std::vector<std::string> lines;
    lines.push_back(row.name + (row.local ? "  (local)" : "  (global)"));
    lines.push_back("type: " + (row.type.empty() ? "?" : row.type));
    lines.push_back("size: " + std::to_string(row.size) + " bytes");
    lines.push_back("address: 0x" + hex(row.address, 8));

    auto read_byte = [&](std::uint32_t address, std::uint32_t& value) {
        return session_.read_memory(address, 1, value);
    };
    auto read_string = [&](std::uint32_t address) {
        std::string text;
        bool terminated = false;
        for (int index = 0; index < 24; ++index) {
            std::uint32_t value = 0;
            if (!read_byte(address + static_cast<std::uint32_t>(index), value)) {
                break;
            }
            if (value == 0) {
                terminated = true;
                break;
            }
            text.push_back(value >= 32 && value < 127 ? static_cast<char>(value) : '.');
        }
        if (!terminated) {
            text += "...";
        }
        return text;
    };
    const bool pointer = !row.type.empty() && row.type.back() == '*';
    const bool array = row.type.find('[') != std::string::npos;
    const bool character = row.type.find("char") != std::string::npos;
    if (!row.ok) {
        lines.push_back("unavailable");
    } else if (pointer) {
        lines.push_back("value: 0x" + hex(row.value, 8));
        if (row.value == 0) {
            lines.push_back("<null>");
        } else if (character) {
            lines.push_back("\"" + read_string(row.value) + "\"");
        } else {
            std::uint32_t dereference = 0;
            if (session_.read_memory(row.value, 4, dereference)) {
                lines.push_back("*ptr: 0x" + hex(dereference, 8));
            } else {
                lines.push_back("*ptr: unavailable");
            }
        }
    } else if (array || row.size > 4) {
        if (character && array) {
            lines.push_back("\"" + read_string(row.address) + "\"");
        } else {
            lines.push_back("aggregate: " + std::to_string(row.size) + " bytes");
            const auto bracket = row.type.find('[');
            int count = 0;
            if (bracket != std::string::npos) {
                count = std::atoi(row.type.c_str() + bracket + 1);
            }
            if (count > 0) {
                lines.push_back("array[" + std::to_string(count) + "]");
                for (int index = 0; index < std::min(4, count); ++index) {
                    std::uint32_t element = 0;
                    if (session_.read_memory(row.address + static_cast<std::uint32_t>(index * 4),
                                             4, element)) {
                        lines.push_back("[" + std::to_string(index) + "] = 0x" +
                                        hex(element, 8));
                    }
                }
            }
        }
    } else {
        std::uint32_t value = row.value;
        if (row.size == 2) {
            value &= 0xFFFFU;
        }
        lines.push_back("value: 0x" +
                        hex(value, row.size == 1 ? 2 : (row.size == 2 ? 4 : 8)));
        const std::int32_t signed_value =
            row.size == 1 ? static_cast<std::int8_t>(value)
                          : (row.size == 2 ? static_cast<std::int16_t>(value)
                                           : static_cast<std::int32_t>(value));
        lines.push_back("decimal: " + std::to_string(value));
        if (signed_value < 0) {
            lines.push_back("signed: " + std::to_string(signed_value));
        }
        if (row.size == 1 && value >= 32 && value < 127) {
            lines.push_back(std::string("char: '") + static_cast<char>(value) + "'");
        }
    }

    int width = 0;
    for (const std::string& line : lines) {
        width = std::max(width, Canvas::text_width(line, FontRole::MonoSmall));
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

void DebuggerUI::draw_call_stack(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "THREADS / CALL STACK");
    session_.refresh_threads();
    const std::vector<debug::ThreadInfo>& threads = session_.threads();
    int y = rect.y + 24;
    const int max_thread_rows = 3;
    const int thread_rows =
        std::min(static_cast<int>(threads.size()), max_thread_rows);
    for (int index = 0; index < thread_rows; ++index) {
        const debug::ThreadInfo& thread = threads[static_cast<std::size_t>(index)];
        canvas.text(rect.x + 6, y,
                    "T" + std::to_string(thread.id) + " " +
                        trim_to(thread.name + " " + hex(thread.pc, 8),
                                static_cast<std::size_t>((rect.width - 16) / 7)),
                    thread.id == session_.selected_thread() ? kAccent : kDim,
                    FontRole::MonoSmall);
        y += 14;
    }
    const int frames_top = y + (thread_rows > 0 ? 4 : 0);
    const std::vector<debug::Frame> frames = session_.call_stack();
    const int row_height = 14;
    const int visible =
        std::max(1, (rect.y + rect.height - frames_top - 4) / row_height);
    stack_scroll_ =
        std::clamp(stack_scroll_, 0, std::max(0, static_cast<int>(frames.size()) - visible));
    int visible_row = 0;
    for (int index = stack_scroll_;
         index < static_cast<int>(frames.size()) && visible_row < visible;
         ++index, ++visible_row) {
        const debug::Frame& frame = frames[static_cast<std::size_t>(index)];
        const int row_y = frames_top + visible_row * row_height;
        canvas.text(rect.x + 6, row_y,
                    "#" + std::to_string(index) + " " +
                        trim_to(frame.function + " " + hex(frame.pc, 8),
                                static_cast<std::size_t>((rect.width - 20) / 7)),
                    index == 0 ? kAccent : kText, FontRole::MonoSmall);
    }
    if (frames.empty()) {
        canvas.text(rect.x + 6, frames_top, "(no frames)", kDim, FontRole::MonoSmall);
    }
    draw_scrollbar(canvas, rect, stack_scroll_, static_cast<int>(frames.size()), visible,
                   stack_scroll_track_, stack_scroll_thumb_);
}

void DebuggerUI::draw_memory(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "MEMORY");
    canvas.text(rect.x + 80, rect.y + 7, "ADDR", kDim, FontRole::MonoSmall);
    memory_address_rect_ =
        Rect{rect.x + 118, rect.y + 3, std::max(120, rect.width - 234), 18};
    memory_pointer_rect_ = Rect{rect.x + rect.width - 96, rect.y + 3, 30, 18};
    memory_up_rect_ = Rect{rect.x + rect.width - 62, rect.y + 3, 20, 18};
    memory_down_rect_ = Rect{rect.x + rect.width - 38, rect.y + 3, 20, 18};
    memory_header_rect_ = Rect{rect.x + 72, rect.y + 3,
                               memory_pointer_rect_.x - (rect.x + 72), 18};
    canvas.fill(memory_address_rect_, kPanelAlt);
    canvas.frame(memory_address_rect_, memory_address_editing_ ? kAccent : kBorder);
    canvas.text(memory_address_rect_.x + 6, memory_address_rect_.y + 3,
                "0x" + (memory_address_editing_ ? memory_edit_ + "_" : hex(memory_address_, 8)),
                memory_address_editing_ ? kAccent : kText, FontRole::MonoSmall);
    for (const Rect& button : {memory_pointer_rect_, memory_up_rect_, memory_down_rect_}) {
        canvas.fill(button, kPanelAlt);
        canvas.frame(button, kBorder);
    }
    canvas.text(memory_pointer_rect_.x + 5, memory_pointer_rect_.y + 3, "PTR", kText,
                FontRole::MonoSmall);
    canvas.text(memory_up_rect_.x + 6, memory_up_rect_.y + 3, "^", kText,
                FontRole::MonoSmall);
    canvas.text(memory_down_rect_.x + 6, memory_down_rect_.y + 3, "v", kText,
                FontRole::MonoSmall);

    const int bytes_per_row = rect.width >= 600 ? 16 : 8;
    constexpr int rows = 4;
    memory_byte_cells_.clear();
    const int text_x = rect.x + 6;
    const int char_width = std::max(1, Canvas::text_width("0", FontRole::MonoSmall));
    for (int row = 0; row < rows; ++row) {
        const std::uint32_t address =
            memory_address_ + static_cast<std::uint32_t>(row * bytes_per_row);
        const int text_y = rect.y + 28 + row * 18;
        std::string hex_part = "0x" + hex(address, 8) + "  ";
        std::string ascii_part;
        for (int column = 0; column < bytes_per_row; ++column) {
            std::uint8_t value = 0;
            const std::uint32_t byte_address = address + static_cast<std::uint32_t>(column);
            const bool ok = session_.machine().read8(byte_address, value) ||
                            session_.machine().read_physical8(byte_address, value);
            hex_part += ok ? hex(value, 2) + " " : "?? ";
            ascii_part.push_back(ok && value >= 32 && value <= 126
                                     ? static_cast<char>(value)
                                     : '.');
            const Rect hex_cell{text_x + (12 + column * 3) * char_width, text_y - 2,
                                char_width * 2, 16};
            memory_byte_cells_.push_back({hex_cell, byte_address});
            const int ascii_column = 12 + bytes_per_row * 3 + 2 + column;
            const Rect ascii_cell{text_x + ascii_column * char_width, text_y - 2,
                                  char_width, 16};
            memory_byte_cells_.push_back({ascii_cell, byte_address});
        }
        for (const MemoryByteCell& cell : memory_byte_cells_) {
            if (cell.address < address ||
                cell.address >= address + static_cast<std::uint32_t>(bytes_per_row)) {
                continue;
            }
            const int index = static_cast<int>(&cell - memory_byte_cells_.data());
            if (index == hover_memory_byte_) {
                canvas.fill(cell.rect, Color{42, 58, 74, 255});
                canvas.frame(cell.rect, kAccent);
            }
        }
        canvas.text(text_x, text_y, hex_part + "  " + ascii_part,
                    kText, FontRole::MonoSmall);
    }
}

void DebuggerUI::begin_memory_address_edit() {
    memory_address_editing_ = true;
    memory_edit_ = hex(memory_address_, 8);
    memory_edit_fresh_ = true;
    status_ = "edit memory address";
}

void DebuggerUI::finish_memory_address_edit(bool commit) {
    if (!memory_address_editing_) return;
    if (commit) {
        unsigned value = 0;
        bool valid = !memory_edit_.empty();
        for (char ch : memory_edit_) {
            const int digit = std::isdigit(static_cast<unsigned char>(ch)) != 0
                                  ? ch - '0'
                                  : (std::isxdigit(static_cast<unsigned char>(ch)) != 0
                                         ? std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10
                                         : -1);
            if (digit < 0) {
                valid = false;
                break;
            }
            value = (value << 4) | static_cast<unsigned>(digit);
        }
        if (valid) {
            memory_address_ = value & ~0xFU;
            hover_memory_byte_ = -1;
            status_ = "memory address 0x" + hex(memory_address_, 8);
        }
    }
    memory_address_editing_ = false;
    memory_edit_.clear();
    memory_edit_fresh_ = false;
}

void DebuggerUI::draw_console(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "CONSOLE");
    std::vector<std::string> lines;
    std::istringstream stream(session_.console());
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    if (lines.empty()) {
        canvas.text(rect.x + 6, rect.y + 24, "(no console output)", kDim, FontRole::MonoSmall);
        draw_scrollbar(canvas, rect, 0, 0, 1, console_scroll_track_, console_scroll_thumb_);
        return;
    }
    const int row_height = 16;
    const int visible = std::max(1, (rect.height - 26) / row_height);
    console_scroll_ =
        std::clamp(console_scroll_, 0, std::max(0, static_cast<int>(lines.size()) - visible));
    for (int index = console_scroll_;
         index < static_cast<int>(lines.size()) && index < console_scroll_ + visible;
         ++index) {
        const int y = rect.y + 24 + (index - console_scroll_) * row_height;
        canvas.text(rect.x + 6, y,
                    trim_to(lines[static_cast<std::size_t>(index)],
                            static_cast<std::size_t>((rect.width - 20) / 8)),
                    kText, FontRole::MonoSmall);
    }
    draw_scrollbar(canvas, rect, console_scroll_, static_cast<int>(lines.size()), visible,
                   console_scroll_track_, console_scroll_thumb_);
}

DebuggerUI::Layout DebuggerUI::layout(int width, int height) const {
    const int right_width = std::clamp(width * 28 / 100, 270, 420);
    int source_width = std::clamp(width * 42 / 100, 240, width - right_width - 300);
    int disassembly_width = width - source_width - right_width;
    if (disassembly_width < 300) {
        disassembly_width = 300;
        source_width = width - disassembly_width - right_width;
    }
    const int bottom_height = std::clamp(height * 24 / 100, 190, 230);
    const int bottom_top = height - bottom_height;
    const int top_height = bottom_top - kToolbarHeight;
    constexpr int memory_height = 112;
    const int source_height = std::max(120, top_height - memory_height);
    const int variables_width = width * 42 / 100;
    const int stack_width = width * 28 / 100;
    const int console_width = width - variables_width - stack_width;

    Layout result;
    result.source = Rect{0, kToolbarHeight, source_width, source_height};
    result.disassembly = Rect{source_width, kToolbarHeight, disassembly_width, top_height};
    result.cores = Rect{source_width + disassembly_width, kToolbarHeight, right_width,
                        top_height};
    result.memory = Rect{0, kToolbarHeight + source_height, source_width, memory_height};
    result.variables = Rect{0, bottom_top, variables_width, bottom_height};
    result.call_stack = Rect{variables_width, bottom_top, stack_width, bottom_height};
    result.console = Rect{variables_width + stack_width, bottom_top, console_width,
                          bottom_height};
    return result;
}

void DebuggerUI::draw_scrollbar(Canvas& canvas, Rect panel, int offset, int total,
                                int visible, Rect& track, Rect& thumb) {
    const int max_offset = std::max(0, total - visible);
    track = Rect{panel.x + panel.width - 10, panel.y + 24, 7,
                 std::max(1, panel.height - 26)};
    thumb = Rect{};
    if (max_offset > 0) {
        const int thumb_height =
            std::clamp(track.height * std::max(1, visible) / std::max(1, total), 18,
                       std::max(18, track.height));
        const int thumb_y =
            track.y + (track.height - thumb_height) * std::clamp(offset, 0, max_offset) /
                          max_offset;
        thumb = Rect{track.x, thumb_y, track.width, thumb_height};
    } else {
        thumb = track;
    }
    canvas.fill(track, kPanelAlt);
    canvas.fill(thumb, max_offset > 0 ? Color{74, 98, 118, 255} : kBorder);
}

void DebuggerUI::draw_error(Canvas& canvas) {
    if (error_.empty()) {
        return;
    }
    const int panel_width = std::min(820, std::max(420, canvas.width() - 80));
    const int text_width = panel_width - 24;
    const int max_chars = std::max(24, text_width / 7);
    std::vector<std::string> lines;
    for (std::size_t offset = 0; offset < error_.size();
         offset += static_cast<std::size_t>(max_chars)) {
        lines.push_back(error_.substr(offset, static_cast<std::size_t>(max_chars)));
        if (lines.size() == 6) {
            if (offset + static_cast<std::size_t>(max_chars) < error_.size()) {
                lines.back() += "...";
            }
            break;
        }
    }
    const int panel_height = 26 + static_cast<int>(lines.size()) * 16 + 34;
    const int x = (canvas.width() - panel_width) / 2;
    const int y = (canvas.height() - panel_height) / 2;
    const Rect dialog{x, y, panel_width, panel_height};
    canvas.fill(Rect{0, 0, canvas.width(), canvas.height()}, Color{0, 0, 0, 150});
    canvas.fill(dialog, Color{28, 8, 12, 255});
    canvas.frame(dialog, kBad, 2);
    canvas.text(dialog.x + 12, dialog.y + 10, "ERROR", Color{255, 130, 130, 255},
                FontRole::UiBold);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        canvas.text(dialog.x + 12, dialog.y + 34 + static_cast<int>(index) * 16,
                    lines[index], Color{255, 255, 255, 255}, FontRole::MonoSmall);
    }
    canvas.text(dialog.x + 12, dialog.y + panel_height - 22, "click to dismiss",
                Color{230, 220, 220, 255}, FontRole::MonoSmall);
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
    if (scroll_dragging_) {
        const int rows = (y - scroll_drag_last_y_) / 14;
        if (rows != 0) {
            scroll_drag_last_y_ = y;
            if (scroll_drag_panel_ == 0) {
                source_follow_ = false;
                source_scroll_ = std::max(0, source_scroll_ + rows);
            } else if (scroll_drag_panel_ == 1) {
                asm_scroll_ += rows;
            } else if (scroll_drag_panel_ == 2) {
                cores_scroll_ = std::max(0, cores_scroll_ + rows * 14);
            } else if (scroll_drag_panel_ == 3) {
                variables_scroll_ = std::max(0, variables_scroll_ + rows);
            } else if (scroll_drag_panel_ == 4) {
                stack_scroll_ = std::max(0, stack_scroll_ + rows);
            } else if (scroll_drag_panel_ == 5) {
                console_scroll_ = std::max(0, console_scroll_ + rows);
            }
        }
        return;
    }
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
    hover_var_ = -1;
    hover_flag_ = -1;
    hover_fpr_ = -1;
    hover_register_ = -1;
    hover_memory_byte_ = -1;
    for (std::size_t index = 0; index < fpr_cells_.size(); ++index) {
        if (fpr_cells_[index].contains(x, y)) {
            hover_fpr_ = static_cast<int>(index);
            break;
        }
    }
    for (std::size_t index = 0; index < register_cells_.size(); ++index) {
        if (register_cells_[index].contains(x, y)) {
            hover_register_ = static_cast<int>(index);
            break;
        }
    }
    if (pc_cell_.contains(x, y)) {
        hover_register_ = 16;
    } else if (sp_cell_.contains(x, y)) {
        hover_register_ = 17;
    }
    for (std::size_t index = 0; index < memory_byte_cells_.size(); ++index) {
        if (memory_byte_cells_[index].rect.contains(x, y)) {
            hover_memory_byte_ = static_cast<int>(index);
            break;
        }
    }
    for (std::size_t index = 0; index < flag_cells_.size(); ++index) {
        if (flag_cells_[index].contains(x, y)) {
            hover_flag_ = static_cast<int>(index);
            break;
        }
    }
    for (std::size_t index = 0; index < var_hover_rows_.size(); ++index) {
        if (var_hover_rows_[index].row.contains(x, y)) {
            hover_var_ = static_cast<int>(index);
            break;
        }
    }
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

void DebuggerUI::mouse_up() {
    scroll_dragging_ = false;
    scroll_drag_panel_ = -1;
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
    draw_cores(canvas, areas.cores);
    draw_memory(canvas, areas.memory);
    draw_variables(canvas, areas.variables);
    draw_call_stack(canvas, areas.call_stack);
    draw_console(canvas, areas.console);
    draw_error(canvas);
    draw_memory_tooltip(canvas);
    draw_register_tooltip(canvas);
    draw_flag_tooltip(canvas);
    draw_variable_tooltip(canvas);
    draw_fpr_tooltip(canvas);
    draw_asm_tooltip(canvas);
    draw_context_menu(canvas);
    draw_fpr_popup(canvas);
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
    if (editing_fpr_ >= 0) {
        if (fpr_ok_rect_.contains(x, y)) {
            finish_fpr_edit(true);
        } else if (fpr_cancel_rect_.contains(x, y)) {
            finish_fpr_edit(false);
        }
        return true;
    }
    if (editing_register_ >= 0) {
        bool inside = pc_cell_.contains(x, y) || sp_cell_.contains(x, y) ||
                      std::any_of(register_cells_.begin(), register_cells_.end(),
                                  [&](const Rect& cell) { return cell.contains(x, y); });
        if (!inside) finish_register_edit(true);
    }
    for (std::size_t index = 0; index < flag_cells_.size(); ++index) {
        if (!flag_cells_[index].contains(x, y)) continue;
        if (index < 5) {
            static const std::uint32_t bits[] = {core::kFlagZ, core::kFlagN, core::kFlagC,
                                                 core::kFlagV, core::kFlagI};
            core::Machine& machine = session_.core(selected_core_);
            machine.set_flags(machine.flags() ^ bits[index]);
            status_ = "flag toggled";
        }
        return true;
    }
    for (std::size_t index = 0; index < register_cells_.size(); ++index) {
        if (!register_cells_[index].contains(x, y)) continue;
        finish_memory_address_edit(true);
        begin_register_edit(static_cast<int>(index));
        return true;
    }
    if (fpr_view_rect_.contains(x, y)) {
        fpr_float_view_ = !fpr_float_view_;
        return true;
    }
    for (std::size_t index = 0; index < fpr_cells_.size(); ++index) {
        if (!fpr_cells_[index].contains(x, y)) continue;
        finish_memory_address_edit(true);
        finish_register_edit(true);
        begin_fpr_edit(static_cast<int>(index));
        return true;
    }
    if (pc_cell_.contains(x, y)) {
        finish_memory_address_edit(true);
        begin_register_edit(16);
        return true;
    }
    if (sp_cell_.contains(x, y)) {
        finish_memory_address_edit(true);
        begin_register_edit(17);
        return true;
    }
    if (source_scroll_track_.contains(x, y)) source_follow_ = false;
    const auto scroll_bar_click = [&](const Rect& track, const Rect& thumb, int& offset,
                                      int page, int panel_id) {
        if (!track.contains(x, y)) return false;
        if (thumb.contains(x, y)) {
            scroll_dragging_ = true;
            scroll_drag_panel_ = panel_id;
            scroll_drag_last_y_ = y;
        } else {
            offset += y < thumb.y ? -page : page;
        }
        return true;
    };
    if (scroll_bar_click(source_scroll_track_, source_scroll_thumb_, source_scroll_, 10, 0) ||
        scroll_bar_click(asm_scroll_track_, asm_scroll_thumb_, asm_scroll_, 5, 1) ||
        scroll_bar_click(cores_scroll_track_, cores_scroll_thumb_, cores_scroll_, 70, 2) ||
        scroll_bar_click(variables_scroll_track_, variables_scroll_thumb_, variables_scroll_,
                         5, 3) ||
        scroll_bar_click(stack_scroll_track_, stack_scroll_thumb_, stack_scroll_, 5, 4) ||
        scroll_bar_click(console_scroll_track_, console_scroll_thumb_, console_scroll_, 5, 5)) {
        return true;
    }
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
            std::string selected;
            if (platform::open_file_dialog(selected)) {
                std::string error;
                if (load(selected, error)) {
                    status_ = "loaded " + selected;
                } else {
                    error_ = error;
                }
            } else {
                status_ = "load canceled";
            }
        } else if (x >= 396 && x < 444) {
            restart();
        }
        return true;
    }
    if (memory_address_editing_ && !memory_address_rect_.contains(x, y)) {
        finish_memory_address_edit(true);
    }
    if (memory_address_rect_.contains(x, y) || memory_header_rect_.contains(x, y)) {
        finish_register_edit(true);
        begin_memory_address_edit();
        return true;
    }
    if (memory_up_rect_.contains(x, y)) {
        memory_address_ = memory_address_ >= 16 ? (memory_address_ - 16) & ~0xFU : 0;
        hover_memory_byte_ = -1;
        return true;
    }
    if (memory_down_rect_.contains(x, y)) {
        memory_address_ = std::min(memory_address_ + 16U, 0xFFFF'FFF0U) & ~0xFU;
        hover_memory_byte_ = -1;
        return true;
    }
    if (memory_pointer_rect_.contains(x, y)) {
        memory_address_ = session_.machine().sp() & ~0xFU;
        hover_memory_byte_ = -1;
        return true;
    }
    if (y >= kToolbarHeight) {
        for (const CoreRow& row : core_rows_) {
            if (!row.row.contains(x, y)) continue;
            selected_core_ = row.index;
            if (expanded_core_ == row.index) {
                expanded_core_ = core_rows_.size();
            } else {
                expanded_core_ = row.index;
            }
            return true;
        }
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

bool DebuggerUI::wheel(int x, int y, int width, int height, int delta) {
    const Layout areas = layout(width, height);
    const int direction = delta > 0 ? -1 : 1;
    if (areas.source.contains(x, y)) {
        source_follow_ = false;
        source_scroll_ =
            std::clamp(source_scroll_ + direction * 3, 0,
                       std::max(0, static_cast<int>(source_lines_.size()) - 1));
        return true;
    }
    if (areas.disassembly.contains(x, y)) {
        asm_scroll_ += direction * 3;
        return true;
    }
    if (areas.cores.contains(x, y)) {
        cores_scroll_ = std::max(0, cores_scroll_ + direction * 14);
        return true;
    }
    if (areas.variables.contains(x, y)) {
        variables_scroll_ = std::max(0, variables_scroll_ + direction * 1);
        return true;
    }
    if (areas.call_stack.contains(x, y)) {
        stack_scroll_ = std::max(0, stack_scroll_ + direction * 1);
        return true;
    }
    if (areas.console.contains(x, y)) {
        console_scroll_ = std::max(0, console_scroll_ + direction * 1);
        return true;
    }
    if (areas.memory.contains(x, y)) {
        const std::int64_t next = static_cast<std::int64_t>(memory_address_) +
                                  static_cast<std::int64_t>(direction) * 16;
        memory_address_ = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(next, 0, 0xFFFF'FFF0));
        memory_address_ &= ~0xFU;
        hover_memory_byte_ = -1;
        return true;
    }
    return false;
}

bool DebuggerUI::key(int key_value, bool shift) {
    if (editing_fpr_ >= 0) {
        if (key_value == kr32::platform::key::Escape) {
            finish_fpr_edit(false);
            return true;
        }
        if (key_value == kr32::platform::key::Enter) {
            finish_fpr_edit(true);
            return true;
        }
        if (key_value == kr32::platform::key::Backspace) {
            if (fpr_edit_fresh_) {
                fpr_edit_.clear();
                fpr_edit_fresh_ = false;
            } else if (!fpr_edit_.empty()) {
                fpr_edit_.pop_back();
            }
            return true;
        }
        const bool allowed =
            (key_value >= '0' && key_value <= '9') || key_value == '.' ||
            key_value == '+' || key_value == '-' || key_value == 'e' ||
            key_value == 'E';
        if (allowed && fpr_edit_.size() < 40) {
            if (fpr_edit_fresh_) {
                fpr_edit_.clear();
                fpr_edit_fresh_ = false;
            }
            fpr_edit_.push_back(static_cast<char>(key_value));
        }
        return true;
    }
    if (editing_register_ >= 0) {
        if (key_value == kr32::platform::key::Escape) {
            finish_register_edit(false);
            return true;
        }
        if (key_value == kr32::platform::key::Enter) {
            finish_register_edit(true);
            return true;
        }
        if (key_value == kr32::platform::key::Backspace) {
            if (register_edit_fresh_) {
                register_edit_.clear();
                register_edit_fresh_ = false;
            } else if (!register_edit_.empty()) {
                register_edit_.pop_back();
            }
            return true;
        }
        const bool digit =
            (key_value >= '0' && key_value <= '9') ||
            (key_value >= 'a' && key_value <= 'f') ||
            (key_value >= 'A' && key_value <= 'F');
        if (digit && register_edit_.size() < 8) {
            if (register_edit_fresh_) {
                register_edit_.clear();
                register_edit_fresh_ = false;
            }
            register_edit_.push_back(static_cast<char>(key_value));
        }
        return true;
    }
    if (memory_address_editing_) {
        if (key_value == kr32::platform::key::Escape) {
            finish_memory_address_edit(false);
            return true;
        }
        if (key_value == kr32::platform::key::Enter) {
            finish_memory_address_edit(true);
            return true;
        }
        if (key_value == kr32::platform::key::Backspace) {
            if (memory_edit_fresh_) {
                memory_edit_.clear();
                memory_edit_fresh_ = false;
            } else if (!memory_edit_.empty()) {
                memory_edit_.pop_back();
            }
            return true;
        }
        const bool digit =
            (key_value >= '0' && key_value <= '9') ||
            (key_value >= 'a' && key_value <= 'f') ||
            (key_value >= 'A' && key_value <= 'F');
        if (digit && memory_edit_.size() < 8) {
            if (memory_edit_fresh_) {
                memory_edit_.clear();
                memory_edit_fresh_ = false;
            }
            memory_edit_.push_back(static_cast<char>(key_value));
        }
        return true;
    }
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
    return false;
}

bool DebuggerUI::headless(const std::string& ppm_path) {
    Canvas canvas(1440, 900);
    render(canvas);
    const std::uint64_t hash = canvas.hash();
    if (!ppm_path.empty() && !canvas.write_ppm(ppm_path)) {
        error_ = "cannot write " + ppm_path;
        return false;
    }
    std::printf("ui-render width=%d height=%d hash=%016llX\n", canvas.width(),
                canvas.height(), static_cast<unsigned long long>(hash));
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
