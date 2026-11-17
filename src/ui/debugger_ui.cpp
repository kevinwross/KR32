#include "kr32/ui/debugger_ui.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/core/devices.h"
#include "kr32/cxx/cxx.h"
#include "kr32/dis/disassembler.h"
#include "kr32/isa/encoding.h"
#include "kr32/link/linker.h"
#include "kr32/os/bootinfo.h"
#include "kr32/os/embedded_os.h"
#include "kr32/os/kernel_meta.h"
#include "kr32/os/symbols.h"
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
constexpr Color kStats{163, 173, 182, 255};
constexpr Color kAccent{76, 170, 255, 255};
constexpr Color kBad{255, 104, 104, 255};

// 24x24 monochrome mask derived from the KR16 settings gear icon.
constexpr std::uint32_t kGearIcon[24] = {
    0x003C00, 0x006600, 0x0E4660, 0x0FC7F0, 0x310098, 0x30000C,
    0x300018, 0x187E10, 0x10C308, 0x71818E, 0xC10083, 0x810081,
    0x810081, 0xF1018F, 0x71830E, 0x10C718, 0x187C10, 0x300008,
    0x30000C, 0x1B0198, 0x0EC770, 0x044620, 0x006600, 0x003C00,
};

constexpr int kToolbarHeight = 30;
constexpr int kStatusBarHeight = 22;
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

std::string format_stats_time(double seconds) {
    char buffer[32] = {};
    if (seconds < 60.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fs", seconds);
    } else {
        const int minutes = static_cast<int>(seconds) / 60;
        std::snprintf(buffer, sizeof(buffer), "%d:%04.1fs", minutes,
                      seconds - static_cast<double>(minutes * 60));
    }
    return buffer;
}

std::string format_stats_rate(double rate) {
    char buffer[32] = {};
    if (rate >= 1'000'000'000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fG/s", rate / 1'000'000'000.0);
    } else if (rate >= 1'000'000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fM/s", rate / 1'000'000.0);
    } else if (rate >= 1'000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1fk/s", rate / 1'000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f/s", rate);
    }
    return buffer;
}

std::string trim_to(std::string value, std::size_t limit) {
    if (value.size() > limit) {
        value.resize(limit);
    }
    return value;
}

struct ParsedArguments {
    bool ok = true;
    std::string error;
    std::vector<std::string> arguments;
};

bool argument_space(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

ParsedArguments parse_arguments(std::string_view text) {
    ParsedArguments result;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && argument_space(text[index])) {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        std::string argument;
        bool quoted = false;
        bool started = false;
        while (index < text.size()) {
            const char ch = text[index];
            if (!quoted && argument_space(ch)) {
                break;
            }
            if (ch == '\\') {
                std::size_t count = 0;
                while (index + count < text.size() && text[index + count] == '\\') {
                    ++count;
                }
                const bool before_quote =
                    index + count < text.size() && text[index + count] == '"';
                if (!before_quote) {
                    argument.append(count, '\\');
                    index += count;
                    started = true;
                    continue;
                }
                argument.append(count / 2, '\\');
                if (count % 2 != 0) {
                    argument.push_back('"');
                    index += count + 1;
                    started = true;
                } else {
                    index += count;
                }
                continue;
            }
            if (ch == '"') {
                if (quoted && index + 1 < text.size() && text[index + 1] == '"') {
                    argument.push_back('"');
                    index += 2;
                    started = true;
                    continue;
                }
                quoted = !quoted;
                started = true;
                ++index;
                continue;
            }
            argument.push_back(ch);
            started = true;
            ++index;
        }
        if (quoted) {
            result.ok = false;
            result.error = "unterminated double quote";
            result.arguments.clear();
            return result;
        }
        if (started) {
            result.arguments.push_back(std::move(argument));
        }
    }
    return result;
}

std::filesystem::path settings_path() {
#ifdef _MSC_VER
    char* appdata = nullptr;
    std::size_t appdata_size = 0;
    if (_dupenv_s(&appdata, &appdata_size, "APPDATA") == 0 && appdata != nullptr) {
        const std::filesystem::path path =
            std::filesystem::path(appdata) / "KR32" / "settings.ini";
        std::free(appdata);
        return path;
    }
    char* xdg = nullptr;
    std::size_t xdg_size = 0;
    if (_dupenv_s(&xdg, &xdg_size, "XDG_CONFIG_HOME") == 0 && xdg != nullptr) {
        const std::filesystem::path path =
            std::filesystem::path(xdg) / "kr32" / "settings.ini";
        std::free(xdg);
        return path;
    }
#else
    const char* appdata = std::getenv("APPDATA");
    if (appdata != nullptr && *appdata != 0) {
        return std::filesystem::path(appdata) / "KR32" / "settings.ini";
    }
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != 0) {
        return std::filesystem::path(xdg) / "kr32" / "settings.ini";
    }
#endif
    return std::filesystem::current_path() / "kr32-settings.ini";
}

bool write_listing(const std::string& path, const std::string& assembly,
                   std::string& error) {
    const std::string listing_path = path + ".lst";
    std::ofstream output(listing_path, std::ios::binary);
    if (!output) {
        error = "cannot write listing " + listing_path;
        return false;
    }
    output << assembly;
    if (!output) {
        error = "cannot write listing " + listing_path;
        return false;
    }
    return true;
}

bool read_binary(const std::string& path, std::vector<std::uint8_t>& bytes,
                 std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return true;
}

bool compile_c_source(const std::string& path, container::Image& image,
                      std::string& error, bool listing) {
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
    if (listing && !write_listing(path, compiled.assembly, error)) {
        return false;
    }
    return true;
}

bool compile_cxx_source(const std::string& path, container::Image& image,
                        std::string& error, bool listing) {
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
    if (listing && !write_listing(path, compiled.assembly, error)) {
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

bool parse_run_limit(const std::string& text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

std::string format_float_text(float number, bool scientific) {
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
    return format_float_text(std::bit_cast<float>(value), scientific);
}

std::string format_scalar_value(const std::string& type,
                                std::uint32_t value,
                                bool available) {
    if (!available) {
        return "?";
    }
    if (type == "float") {
        std::string text = format_float_text(std::bit_cast<float>(value), false);
        if (text.find('.') == std::string::npos &&
            text.find('e') == std::string::npos &&
            text.find('E') == std::string::npos &&
            text.find("inf") == std::string::npos &&
            text.find("nan") == std::string::npos) {
            text += ".0";
        }
        return text;
    }
    if (type == "bool") {
        return value == 0 ? "false" : "true";
    }
    if (type == "char") {
        if (value >= 32 && value < 127) {
            return "'" + std::string(1, static_cast<char>(value)) + "'";
        }
        return "0x" + hex(value, 2);
    }
    return "0x" + hex(value, 8);
}

int scroll_offset_for_drag(int origin_offset, int origin_y, int y,
                           int max_offset, int span) {
    if (max_offset <= 0) {
        return 0;
    }
    if (span <= 0) {
        return std::clamp(origin_offset, 0, max_offset);
    }
    const int next = origin_offset + (y - origin_y) * max_offset / span;
    return std::clamp(next, 0, max_offset);
}

int console_scroll_after_output(int scroll, int previous_line_count,
                                int line_count, int visible) {
    visible = std::max(1, visible);
    const int previous_max = std::max(0, previous_line_count - visible);
    const int max_scroll = std::max(0, line_count - visible);
    if (scroll >= previous_max) {
        return max_scroll;
    }
    return std::clamp(scroll, 0, max_scroll);
}

DebuggerUI::DebuggerUI() {
    session_.set_file_dialog([](const FileDialogOptions& options) {
        return platform::show_file_dialog(options);
    });
    load_settings();
}

void DebuggerUI::load_settings() {
    std::ifstream input(settings_path());
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "arguments") {
            settings_arguments_ = value;
        } else if (key == "stop_at_main") {
            settings_stop_at_main_ = value != "0";
        } else if (key == "listing") {
            settings_write_listing_ = value != "0";
        } else if (key == "run_limit") {
            std::uint64_t limit = 0;
            if (parse_run_limit(value, limit)) {
                settings_run_limit_ = limit;
            }
        }
    }
}

void DebuggerUI::save_settings() {
    const std::filesystem::path path = settings_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return;
    }
    output << "arguments=" << settings_arguments_ << '\n';
    output << "stop_at_main=" << (settings_stop_at_main_ ? 1 : 0) << '\n';
    output << "listing=" << (settings_write_listing_ ? 1 : 0) << '\n';
    output << "run_limit=" << settings_run_limit_ << '\n';
}

void DebuggerUI::open_settings() {
    if (continuous_run_) {
        continuous_run_ = false;
        if (continuous_owned_breakpoint_) {
            session_.clear_breakpoint(continuous_target_);
        }
        continuous_has_target_ = false;
        continuous_owned_breakpoint_ = false;
    }
    settings_open_ = true;
    settings_args_focused_ = false;
    settings_run_focused_ = false;
    settings_run_limit_text_ = std::to_string(settings_run_limit_);
    settings_error_.clear();
    hover_settings_control_ = -1;
}

void DebuggerUI::close_settings(bool save) {
    if (save) {
        save_settings();
    }
    settings_open_ = false;
    settings_args_focused_ = false;
    settings_run_focused_ = false;
    settings_error_.clear();
    hover_settings_control_ = -1;
}

void DebuggerUI::apply_settings() {
    const ParsedArguments parsed = parse_arguments(settings_arguments_);
    if (!parsed.ok) {
        settings_error_ = parsed.error;
        return;
    }
    std::uint64_t run_limit = 0;
    if (!parse_run_limit(settings_run_limit_text_, run_limit)) {
        settings_error_ = "run limit must be a decimal instruction count";
        return;
    }
    settings_run_limit_ = run_limit;
    settings_error_.clear();
    save_settings();
    if (!loaded_ || path_.empty()) {
        status_ = "settings saved";
        settings_open_ = false;
        return;
    }
    std::string error;
    const bool reloaded = os_mode_ ? reload_os(error) : load(path_, error);
    if (!reloaded) {
        settings_error_ = error;
        error_ = error;
        return;
    }
    status_ = "settings applied; rebuilt " + path_;
    settings_open_ = false;
}

void DebuggerUI::draw_settings(Canvas& canvas) {
    const int width = 560;
    const int height = 440;
    const int x = (canvas.width() - width) / 2;
    const int y = (canvas.height() - height) / 2;
    settings_modal_ = Rect{x, y, width, height};
    settings_close_ = Rect{x + width - 32, y + 8, 24, 22};
    settings_args_rect_ = Rect{x + 18, y + 92, width - 36, 26};
    settings_run_rect_ = Rect{x + 18, y + 152, width - 36, 26};
    settings_stop_box_ = Rect{x + 18, y + 208, 16, 16};
    settings_listing_box_ = Rect{x + 18, y + 240, 16, 16};
    settings_apply_rect_ = Rect{x + width - 200, y + height - 42, 112, 26};
    settings_cancel_rect_ = Rect{x + width - 80, y + height - 42, 62, 26};

    canvas.fill(Rect{0, 0, canvas.width(), canvas.height()}, Color{0, 0, 0, 120});
    canvas.fill(settings_modal_, Color{18, 26, 34, 255});
    canvas.frame(settings_modal_, kAccent, 2);
    canvas.text(x + 18, y + 12, "Settings", kAccent, FontRole::UiBold);

    canvas.fill(settings_close_, hover_settings_control_ == 5 ? kPanelAlt : kPanel);
    canvas.frame(settings_close_, hover_settings_control_ == 5 ? kAccent : kBorder);
    canvas.text(settings_close_.x + 8, settings_close_.y + 2, "x", kText, FontRole::UiBold);

    canvas.text(x + 18, y + 46, "Command line arguments", kText, FontRole::MonoSmall);
    canvas.text(x + 18, y + 64,
                "Passed to main(argc, argv) and sys_argc/sys_arg; quote spaces.",
                kDim, FontRole::MonoSmall);
    canvas.fill(settings_args_rect_,
                settings_args_focused_ ? Color{34, 46, 58, 255} : kPanelAlt);
    canvas.frame(settings_args_rect_, settings_args_focused_ ? kAccent : kBorder);
    const std::string arguments_text =
        trim_to(settings_arguments_ + (settings_args_focused_ ? "_" : ""),
                static_cast<std::size_t>((width - 48) / 7));
    canvas.text(settings_args_rect_.x + 6, settings_args_rect_.y + 5,
                arguments_text, kText, FontRole::MonoSmall);
    canvas.text(x + 18, y + 122,
                "Arguments are stored as --arg values for the next Load/Restart.",
                kDim, FontRole::MonoSmall);

    canvas.text(x + 18, y + 136, "Run instruction limit (0 = unlimited)",
                kText, FontRole::MonoSmall);
    canvas.fill(settings_run_rect_,
                settings_run_focused_ ? Color{34, 46, 58, 255}
                                      : (hover_settings_control_ == 6
                                             ? Color{42, 58, 74, 255}
                                             : kPanelAlt));
    canvas.frame(settings_run_rect_,
                 settings_run_focused_ || hover_settings_control_ == 6 ? kAccent
                                                                       : kBorder);
    canvas.text(settings_run_rect_.x + 6, settings_run_rect_.y + 5,
                settings_run_limit_text_ + (settings_run_focused_ ? "_" : ""),
                kText, FontRole::MonoSmall);
    canvas.text(x + 18, y + 184,
                "Applied to Run/Run To; zero removes the instruction cap.",
                kDim, FontRole::MonoSmall);

    const auto checkbox = [&](Rect box, bool enabled, int id, const char* label,
                              int label_y) {
        canvas.fill(box, enabled ? kAccent : kPanelAlt);
        canvas.frame(box, hover_settings_control_ == id ? kAccent : kBorder);
        if (enabled) {
            canvas.fill(Rect{box.x + 4, box.y + 4, box.width - 8, box.height - 8},
                        kPanel);
        }
        canvas.text(box.x + 24, label_y, label, kText, FontRole::MonoSmall);
    };
    checkbox(settings_stop_box_, settings_stop_at_main_, 1,
             "Stop at main after load", y + 206);
    checkbox(settings_listing_box_, settings_write_listing_, 2,
             "Write assembler listing (.lst) beside the source", y + 238);

    if (!settings_error_.empty()) {
        canvas.text(x + 18, y + 276, settings_error_, kBad, FontRole::MonoSmall);
    } else {
        canvas.text(x + 18, y + 276,
                    "Settings are saved in the user configuration directory.",
                    kDim, FontRole::MonoSmall);
    }
    canvas.text(x + 18, y + 300,
                "Apply rebuilds the current source and applies arguments.",
                kDim, FontRole::MonoSmall);
    canvas.text(x + 18, y + 324,
                "Other useful future controls: guest filesystem root/quota,",
                kDim, FontRole::MonoSmall);
    canvas.text(x + 18, y + 348,
                "read-only mode, and console echo.", kDim,
                FontRole::MonoSmall);

    canvas.fill(settings_apply_rect_,
                hover_settings_control_ == 3 ? Color{52, 70, 88, 255} : kPanelAlt);
    canvas.frame(settings_apply_rect_, hover_settings_control_ == 3 ? kAccent : kBorder);
    canvas.text(settings_apply_rect_.x + 8, settings_apply_rect_.y + 5,
                "APPLY & RESTART", kText, FontRole::MonoSmall);
    canvas.fill(settings_cancel_rect_,
                hover_settings_control_ == 4 ? Color{52, 70, 88, 255} : kPanelAlt);
    canvas.frame(settings_cancel_rect_, hover_settings_control_ == 4 ? kAccent : kBorder);
    canvas.text(settings_cancel_rect_.x + 6, settings_cancel_rect_.y + 5,
                "CANCEL", kText, FontRole::MonoSmall);
}

bool DebuggerUI::load(const std::string& path, std::string& error) {
    const ParsedArguments parsed = parse_arguments(settings_arguments_);
    if (!parsed.ok) {
        error = "command line arguments: " + parsed.error;
        return false;
    }
    session_.set_arguments(parsed.arguments);
    session_.set_program_name(
        std::filesystem::path(path).filename().string());
    std::string source_path;
    if (has_c_extension(path)) {
        if (!compile_c_source(path, image_, error, settings_write_listing_)) {
            return false;
        }
        source_path = path;
    } else if (has_cxx_extension(path)) {
        if (!compile_cxx_source(path, image_, error, settings_write_listing_)) {
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
    console_scroll_ = 0;
    console_line_count_ = 0;
    path_ = path;
    source_lines_.clear();
    source_cache_.clear();
    source_view_id_ = -1;
    source_view_path_.clear();
    source_pinned_ = false;
    source_menu_open_ = false;
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
    os_mode_ = false;
    bios_path_.clear();
    source_scroll_ = 0;
    asm_scroll_ = 0;
    array_expanded_.clear();
    aggregate_expanded_.clear();
    array_pages_.clear();
    continuous_run_ = false;
    continuous_has_target_ = false;
    continuous_owned_breakpoint_ = false;
    continuous_remaining_ = 0;
    reset_stats();
    error_.clear();
    if (settings_stop_at_main_) {
        prepare_initial_stop();
    }
    memory_address_ = session_.machine().sp() & ~0xFU;
    hover_memory_byte_ = -1;
    status_ = "paused at " + hex(session_.machine().pc(), 8);
    return true;
}

bool DebuggerUI::load_os(const std::string& kernel_path,
                         const std::string& bios_path,
                         std::string& error) {
    std::vector<std::uint8_t> kernel;
    std::vector<std::uint8_t> bios;
    if (!read_binary(kernel_path, kernel, error) ||
        !read_binary(bios_path, bios, error)) {
        return false;
    }
    return load_os_memory(kernel, bios, kernel_path, bios_path, error);
}

bool DebuggerUI::load_os_memory(const std::vector<std::uint8_t>& kernel,
                                const std::vector<std::uint8_t>& bios,
                                const std::string& kernel_name,
                                const std::string& bios_name,
                                std::string& error) {
    const ParsedArguments parsed = parse_arguments(settings_arguments_);
    if (!parsed.ok) {
        error = "command line arguments: " + parsed.error;
        return false;
    }
    session_.set_arguments(parsed.arguments);
    session_.set_program_name(
        std::filesystem::path(kernel_name).filename().string());
    const auto is_container = [](const std::vector<std::uint8_t>& bytes) {
        return bytes.size() >= 4 && bytes[0] == 'K' && bytes[1] == '3' &&
               bytes[2] == '2' && bytes[3] == 0;
    };
    const bool kernel_container = is_container(kernel);
    const bool bios_container = is_container(bios);
    std::uint32_t kernel_size = static_cast<std::uint32_t>(kernel.size());
    if (kernel_container || bios_container) {
        if (!kernel_container || !bios_container) {
            error = "kernel and BIOS must both be .k32 or both be raw .bin";
            return false;
        }
        container::Image kernel_image;
        container::Image bios_image;
        container::Error container_error;
        if (!container::read(kernel, kernel_image, container_error) ||
            !container::read(bios, bios_image, container_error)) {
            error = container_error.message;
            return false;
        }
        if (!session_.load_os(kernel_image, bios_image, error)) {
            return false;
        }
        kernel_size = 0;
        for (const container::Section& section : kernel_image.sections) {
            if ((section.flags & container::Alloc) != 0) {
                kernel_size = std::max(kernel_size, section.address + section.size -
                                                      0x0002'0000U);
            }
        }
    } else if (!session_.load_raw(kernel, 0x0002'0000, bios, error)) {
        return false;
    } else {
        container::Image debug_image;
        std::string sym_error;
        if (os::load_symbol_file(kernel_name, kernel, debug_image, sym_error)) {
            if (!session_.set_debug_info(debug_image, error)) {
                return false;
            }
        } else if (!sym_error.empty()) {
            error = sym_error;
            return false;
        }
        container::Image bios_debug_image;
        sym_error.clear();
        if (os::load_symbol_file(bios_name, bios, bios_debug_image, sym_error)) {
            if (!session_.set_debug_info(bios_debug_image, error)) {
                return false;
            }
        } else if (!sym_error.empty()) {
            error = sym_error;
            return false;
        }
    }
    os::BootInfo boot{};
    boot.magic = os::kBootInfoMagic;
    boot.version = os::kBootInfoVersion;
    boot.size = sizeof(os::BootInfo);
    boot.ram_base = session_.machine().ram_base();
    boot.ram_size = session_.machine().ram_size();
    boot.kernel_base = 0x0002'0000;
    boot.kernel_size = kernel_size;
    boot.kernel_entry = 0x0002'0000;
    boot.framebuffer_base = core::kFramebufferBase;
    boot.framebuffer_size = core::kFramebufferSize;
    boot.framebuffer_stride = core::kFramebufferStride;
    boot.framebuffer_format = 1;
    os::KernelMeta meta{};
    if (!kernel_container && os::load_kernel_meta(kernel_name, meta, error)) {
        boot.initrd_base = 0x0002'0000U + meta.initrd_offset;
        boot.initrd_size = meta.initrd_size;
    } else if (!kernel_container && !error.empty()) {
        return false;
    }
    if (!os::write_boot_info(session_.machine(), boot, error)) {
        return false;
    }
    devices_.reset();
    devices_.attach_machine(session_.machine());
    session_.machine().attach_devices(&devices_);
    serial_text_.clear();
    console_scroll_ = 0;
    console_line_count_ = 0;
    path_ = kernel_name;
    bios_path_ = bios_name;
    os_mode_ = true;
    screen_tab_active_ = true;
    os_embedded_ = kernel_name == "<embedded>";
    source_lines_.clear();
    source_cache_.clear();
    source_view_id_ = -1;
    source_view_path_.clear();
    source_pinned_ = false;
    source_menu_open_ = false;
    load_source_files();
    if (!session_.info().sources.empty()) {
        const int initial_source = session_.info().sources.front().id;
        const auto cached = source_cache_.find(initial_source);
        if (cached != source_cache_.end()) {
            source_lines_ = cached->second;
            source_view_id_ = initial_source;
            source_view_path_ = source_path_for(initial_source);
        }
    }
    ensure_source_for_current();
    loaded_ = true;
    source_scroll_ = 0;
    asm_scroll_ = 0;
    array_expanded_.clear();
    aggregate_expanded_.clear();
    array_pages_.clear();
    continuous_run_ = false;
    continuous_has_target_ = false;
    continuous_owned_breakpoint_ = false;
    continuous_remaining_ = 0;
    reset_stats();
    error_.clear();
    memory_address_ = session_.machine().sp() & ~0xFU;
    hover_memory_byte_ = -1;
    status_ = "OS loaded: " + kernel_name;
    return true;
}

bool DebuggerUI::reload_os(std::string& error) {
    if (!os_embedded_) {
        return load_os(path_, bios_path_, error);
    }
    const os::EmbeddedOs& images = os::embedded_os();
    std::vector<std::uint8_t> kernel(images.kernel.begin(), images.kernel.end());
    std::vector<std::uint8_t> bios(images.bios.begin(), images.bios.end());
    return load_os_memory(kernel, bios, "<embedded>", "<embedded>", error);
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
    std::vector<std::pair<std::uint32_t, bool>> saved_breakpoints;
    for (const std::uint32_t address : session_.breakpoints()) {
        saved_breakpoints.push_back(
            {address, session_.breakpoint_enabled(address)});
    }
    std::string error;
    const bool reloaded = os_mode_ ? reload_os(error) : load(path_, error);
    if (!reloaded) {
        error_ = error;
        return;
    }
    for (const auto& [address, enabled] : saved_breakpoints) {
        session_.set_breakpoint(address, enabled);
    }
    status_ = "restarted at " + hex(session_.machine().pc(), 8);
    error_.clear();
}

void DebuggerUI::run() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) {
        stop_continuous(false, core::StepResult{});
        status_ = "paused at " + hex(session_.machine().pc(), 8);
        return;
    }
    continuous_run_ = true;
    continuous_has_target_ = false;
    continuous_owned_breakpoint_ = false;
    continuous_target_ = 0;
    continuous_remaining_ = settings_run_limit_;
    status_ = continuous_remaining_ == 0
                  ? "running without an instruction limit; press Run to pause"
                  : "running; press Run to pause";
}

void DebuggerUI::update() {
    if (!continuous_run_) {
        return;
    }
    constexpr std::uint64_t kSlice = 100'000;
    std::uint64_t slice = kSlice;
    if (continuous_remaining_ != 0) {
        slice = std::min(slice, continuous_remaining_);
    }
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.run(slice);
    stats_seconds_ += std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - started)
                          .count();
    const bool reached_target =
        continuous_has_target_ && session_.machine().pc() == continuous_target_;
    if (session_.at_breakpoint() || reached_target ||
        result.kind == core::StepResult::Kind::Halted ||
        result.kind == core::StepResult::Kind::Raised) {
        stop_continuous(reached_target, result);
        return;
    }
    if (continuous_remaining_ != 0) {
        continuous_remaining_ -= slice;
        if (continuous_remaining_ == 0) {
            stop_continuous(false, result, true);
        }
    }
}

void DebuggerUI::stop_continuous(bool reached_target,
                                 const core::StepResult& result,
                                 bool budget_exhausted) {
    const bool had_target = continuous_has_target_;
    continuous_run_ = false;
    if (continuous_owned_breakpoint_) {
        session_.clear_breakpoint(continuous_target_);
    }
    continuous_has_target_ = false;
    continuous_owned_breakpoint_ = false;
    continuous_remaining_ = 0;
    if (reached_target) {
        status_ = "stopped at " + hex(continuous_target_, 8);
    } else if (budget_exhausted) {
        status_ = had_target
                      ? "run step limit before target " + hex(continuous_target_, 8)
                      : "run step limit reached; press Run to continue";
    } else {
        set_status_from_result(result);
    }
}

void DebuggerUI::reset_stats() {
    stats_base_ticks_ = session_.machine().ticks();
    stats_seconds_ = 0.0;
}

void DebuggerUI::record_stats_time(std::chrono::steady_clock::time_point start) {
    stats_seconds_ +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void DebuggerUI::step() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) run();
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.step();
    record_stats_time(started);
    set_status_from_result(result);
}

void DebuggerUI::step_over() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) run();
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.step_over();
    record_stats_time(started);
    set_status_from_result(result);
}

void DebuggerUI::step_out() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) run();
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.step_out();
    record_stats_time(started);
    set_status_from_result(result);
}

void DebuggerUI::step_line_in() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) run();
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.step_line_in();
    record_stats_time(started);
    set_status_from_result(result);
}

void DebuggerUI::step_line_over() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) run();
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.step_line_over();
    record_stats_time(started);
    set_status_from_result(result);
}

void DebuggerUI::step_line_out() {
    if (!loaded_) {
        return;
    }
    if (continuous_run_) run();
    const auto started = std::chrono::steady_clock::now();
    const core::StepResult result = session_.step_line_out();
    record_stats_time(started);
    set_status_from_result(result);
}

void DebuggerUI::set_status_from_result(const core::StepResult& result) {
    source_pinned_ = false;
    source_menu_open_ = false;
    ensure_source_for_current();
    source_follow_ = true;
    // Keep the instruction view centered on the new PC after every step/run.
    asm_scroll_ = 0;
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
    if (source_pinned_) {
        return;
    }
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
    asm_scroll_ = 0;
    source_follow_ = true;
}

void DebuggerUI::select_source(int source_id) {
    auto cached = source_cache_.find(source_id);
    if (cached == source_cache_.end()) {
        load_source_files();
        cached = source_cache_.find(source_id);
    }
    source_view_id_ = source_id;
    source_view_path_ = source_path_for(source_id);
    source_lines_ = cached == source_cache_.end()
                        ? std::vector<std::string>{}
                        : cached->second;
    source_pinned_ = true;
    source_menu_open_ = false;
    source_menu_scroll_ = 0;
    source_scroll_ = 0;
    source_follow_ = true;
    status_ = source_view_path_.empty() ? "source selected"
                                        : "source: " + source_view_path_;
}

void DebuggerUI::open_source_menu() {
    source_menu_open_ = !source_menu_open_;
    source_menu_scroll_ = 0;
    source_menu_items_.clear();
}

int DebuggerUI::current_source_line() const {
    if (!loaded_) {
        return 0;
    }
    const debug::Line* line = session_.line_at(session_.machine().pc());
    if (line == nullptr || line->source != source_view_id_) {
        return 0;
    }
    return line->line;
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
    toggle_instruction_breakpoint(address);
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
        const bool running =
            continuous_run_ && std::strcmp(button_entry.label, "RUN") == 0;
        const Color fill = running ? Color{32, 122, 64, 255} : button;
        const Color frame = running ? Color{86, 210, 126, 255} : kBorder;
        canvas.fill(Rect{button_entry.x, 4, button_entry.width, 22}, fill);
        canvas.frame(Rect{button_entry.x, 4, button_entry.width, 22}, frame);
        canvas.text(button_entry.x + 6, 7, button_entry.label,
                    running ? Color{235, 255, 240, 255} : kText, FontRole::Ui);
    }

    const std::uint64_t ticks = session_.machine().ticks();
    const std::uint64_t instructions =
        ticks >= stats_base_ticks_ ? ticks - stats_base_ticks_ : 0;
    const double rate = stats_seconds_ > 0.0
                            ? static_cast<double>(instructions) / stats_seconds_
                            : 0.0;
    const std::string stats_text =
        "instr " + decimal(instructions) + "  time " + format_stats_time(stats_seconds_) +
        "  ips " + format_stats_rate(rate);
    stats_reset_rect_ = Rect{canvas.width() - 86, 4, 44, 22};
    const int stats_right = stats_reset_rect_.x - 8;
    const int stats_x = std::max(452, stats_right - 360);
    if (stats_x > 520) {
        canvas.text(452, 7,
                    trim_to(status_, static_cast<std::size_t>((stats_x - 460) / 7)),
                    kDim, FontRole::MonoSmall);
    }
    canvas.text(stats_x, 7,
                trim_to(stats_text,
                        static_cast<std::size_t>(std::max(0, stats_right - stats_x) / 7)),
                kStats, FontRole::MonoSmall);

    canvas.fill(stats_reset_rect_,
                hover_settings_control_ == 101 ? Color{52, 70, 88, 255} : button);
    canvas.frame(stats_reset_rect_,
                 hover_settings_control_ == 101 ? kAccent : kBorder);
    canvas.text(stats_reset_rect_.x + 6, 7, "CLR", kText, FontRole::MonoSmall);

    settings_rect_ = Rect{canvas.width() - 32, 4, 26, 22};
    draw_gear(canvas, settings_rect_, hover_settings_control_ == 100);
}

void DebuggerUI::draw_gear(Canvas& canvas, Rect rect, bool hover) {
    canvas.fill(rect, hover ? Color{52, 70, 88, 255} : Color{42, 54, 66, 255});
    canvas.frame(rect, hover ? kAccent : kBorder);
    const int left = rect.x + (rect.width - 24) / 2;
    const int top = rect.y + (rect.height - 24) / 2;
    const Color color = hover ? kAccent : kText;
    for (int row = 0; row < 24; ++row) {
        for (int column = 0; column < 24; ++column) {
            if ((kGearIcon[row] & (1u << (23 - column))) != 0) {
                canvas.fill(Rect{left + column, top + row, 1, 1}, color);
            }
        }
    }
}

void DebuggerUI::draw_status_bar(Canvas& canvas) {
    const int y = canvas.height() - kStatusBarHeight;
    canvas.fill(Rect{0, y, canvas.width(), kStatusBarHeight}, kPanelAlt);
    canvas.fill(Rect{0, y, canvas.width(), 1}, kBorder);

    struct Hint {
        const char* key;
        const char* label;
        bool active = false;
    };
    std::vector<Hint> hints;
    if (settings_open_) {
        hints = {{"ENTER", "APPLY", true}, {"ESC", "CANCEL", false}};
    } else if (continuous_run_) {
        hints = {{"F5", "PAUSE", true}, {"F6", "BREAK", false}};
    } else {
        hints = {{"F5", "RUN", false}, {"F6", "BREAK", false},
                 {"F9", "BKPT", false}, {"F10", "OVER", false},
                 {"F11", "IN", false},  {"R", "RST", false}};
    }

    int right = canvas.width() - 8;
    for (const Hint& hint : hints) {
        const int key_width = Canvas::text_width(hint.key, FontRole::MonoSmall) + 8;
        const int label_width = Canvas::text_width(hint.label, FontRole::MonoSmall);
        const int total = key_width + 4 + label_width + 8;
        if (right - total < 160) {
            break;
        }
        right -= total;
        const Rect badge{right, y + 3, key_width, 16};
        canvas.fill(badge, hint.active ? Color{32, 122, 64, 255} : kPanel);
        canvas.frame(badge, hint.active ? Color{86, 210, 126, 255} : kBorder);
        canvas.text(badge.x + 4, y + 4, hint.key,
                    hint.active ? Color{235, 255, 240, 255} : kStats,
                    FontRole::MonoSmall);
        canvas.text(badge.x + key_width + 4, y + 4, hint.label, kDim,
                    FontRole::MonoSmall);
    }
    const int status_width = std::max(0, right - 16);
    canvas.text(8, y + 4,
                trim_to(status_, static_cast<std::size_t>(status_width / 7)),
                kStats, FontRole::MonoSmall);
}

void DebuggerUI::guest_key(std::uint32_t word) {
    (void)devices_.keyboard().push_event(word);
}

void DebuggerUI::draw_tabs(Canvas& canvas, Rect rect) {
    canvas.fill(rect, kPanel);
    canvas.fill(Rect{rect.x, rect.y + rect.height - 1, rect.width, 1}, kBorder);
    const int button_height = rect.height - 4;
    screen_tab_source_ = Rect{rect.x + 4, rect.y + 2, 78, button_height};
    screen_tab_screen_ = Rect{screen_tab_source_.x + screen_tab_source_.width + 4,
                              screen_tab_source_.y, 78, button_height};
    canvas.fill(screen_tab_source_, screen_tab_active_ ? kPanel : kPanelAlt);
    canvas.fill(screen_tab_screen_, screen_tab_active_ ? kPanelAlt : kPanel);
    canvas.text(screen_tab_source_.x + 10, screen_tab_source_.y + 5, "SOURCE",
                screen_tab_active_ ? kDim : kText, FontRole::UiBold);
    canvas.text(screen_tab_screen_.x + 10, screen_tab_screen_.y + 5, "SCREEN",
                screen_tab_active_ ? kText : kDim, FontRole::UiBold);
    const Rect active = screen_tab_active_ ? screen_tab_screen_ : screen_tab_source_;
    canvas.fill(Rect{active.x, active.y + active.height - 2, active.width, 2}, kAccent);
}

void DebuggerUI::refresh_screen_cache() {
    const core::FramebufferDevice& framebuffer = devices_.framebuffer();
    const core::FramebufferDevice::CommittedFrame& frame = framebuffer.frame();
    if (frame.number == screen_frame_.number && !screen_pixels_.empty()) {
        return;
    }
    const auto pixels = framebuffer.frame_pixels();
    screen_pixels_.assign(pixels.begin(), pixels.end());
    screen_frame_ = frame;
    screen_has_frame_ = frame.number != 0 && !pixels.empty();
}

void DebuggerUI::draw_screen(Canvas& canvas, Rect rect) {
    refresh_screen_cache();
    std::string title = "SCREEN";
    if (screen_has_frame_) {
        title += " " + std::to_string(screen_frame_.width) + "x" +
                 std::to_string(screen_frame_.height) + "  FRAME " +
                 std::to_string(screen_frame_.number);
    } else {
        title += "  no committed frame";
    }
    draw_panel(canvas, rect, title);

    screen_view_ = Rect{};
    screen_scale_ = 1;
    screen_downscale_ = false;
    if (!screen_has_frame_) {
        return;
    }
    const int inner_x = rect.x + 4;
    const int inner_y = rect.y + 24;
    const int inner_w = std::max(1, rect.width - 8);
    const int inner_h = std::max(1, rect.height - 28);
    const int frame_w = static_cast<int>(screen_frame_.width);
    const int frame_h = static_cast<int>(screen_frame_.height);
    if (frame_w <= 0 || frame_h <= 0) {
        return;
    }

    int draw_w = frame_w;
    int draw_h = frame_h;
    int scale = std::min(inner_w / frame_w, inner_h / frame_h);
    if (scale >= 1) {
        scale = std::clamp(scale, 1, 8);
        draw_w = frame_w * scale;
        draw_h = frame_h * scale;
        screen_scale_ = scale;
    } else {
        screen_downscale_ = true;
        const int by_width = std::max(1, inner_w * frame_h / frame_w);
        if (by_width <= inner_h) {
            draw_w = inner_w;
            draw_h = by_width;
        } else {
            draw_h = inner_h;
            draw_w = std::max(1, inner_h * frame_w / frame_h);
        }
    }

    const int origin_x = inner_x + (inner_w - draw_w) / 2;
    const int origin_y = inner_y + (inner_h - draw_h) / 2;
    screen_view_ = Rect{origin_x, origin_y, draw_w, draw_h};
    for (int y = 0; y < draw_h; ++y) {
        const int source_y = screen_downscale_ ? y * frame_h / draw_h : y / scale;
        for (int x = 0; x < draw_w; ++x) {
            const int source_x = screen_downscale_ ? x * frame_w / draw_w : x / scale;
            const std::size_t offset =
                static_cast<std::size_t>(source_y) * screen_frame_.stride +
                static_cast<std::size_t>(source_x) * 4U;
            if (offset + 2U >= screen_pixels_.size()) {
                continue;
            }
            canvas.pixel(origin_x + x, origin_y + y,
                         Color{screen_pixels_[offset + 2], screen_pixels_[offset + 1],
                               screen_pixels_[offset], 255});
        }
    }
    if (screen_focus_) {
        canvas.fill(Rect{rect.x + rect.width - 16, rect.y + 8, 7, 7},
                    Color{80, 220, 120, 255});
    }
}

void DebuggerUI::draw_screen_tooltip(Canvas& canvas) {
    if (!screen_tab_active_ || screen_hover_x_ < 0 || screen_view_.width == 0) {
        return;
    }
    char text[128];
    std::snprintf(text, sizeof(text), "(%d, %d)  0x%08X  #%06X", screen_hover_x_,
                  screen_hover_y_, screen_hover_address_, screen_hover_color_);
    const int width = Canvas::text_width(text, FontRole::MonoSmall) + 12;
    const int x = std::min(canvas.width() - width - 4, mouse_x_ + 12);
    const int y = std::min(canvas.height() - 24, mouse_y_ + 18);
    canvas.fill(Rect{x, y, width, 20}, kPanelAlt);
    canvas.frame(Rect{x, y, width, 20}, kBorder);
    canvas.text(x + 6, y + 4, text, kText, FontRole::MonoSmall);
}

void DebuggerUI::draw_source(Canvas& canvas, Rect rect) {
    ensure_source_for_current();
    draw_panel(canvas, rect, "SOURCE");
    const int button_width = 40;
    const int button_start = rect.x + rect.width - 3 * (button_width + 2) - 4;
    canvas.fill(Rect{button_start - 1, rect.y + 1, rect.width - (button_start - rect.x) + 1, 20},
                kPanelAlt);
    std::string source_path = source_view_path_;
    if (source_path.empty() && !session_.info().sources.empty()) {
        source_path = session_.info().sources.front().path;
    }
    source_file_rect_ = Rect{};
    if (!source_path.empty()) {
        const int path_x = rect.x + 6 +
            Canvas::text_width("SOURCE  ", FontRole::UiBold);
        const int path_width = button_start - path_x - 8;
        if (path_width > 16) {
            const std::string shown =
                trim_to(source_path, static_cast<std::size_t>(path_width / 7));
            const int text_width = Canvas::text_width(shown, FontRole::UiBold);
            const Rect area{path_x - 2, rect.y + 2,
                            std::min(path_width, text_width + 16), 18};
            const bool hovered =
                mouse_x_ >= area.x && mouse_x_ < area.x + area.width &&
                mouse_y_ >= area.y && mouse_y_ < area.y + area.height;
            if (hovered || source_menu_open_) {
                canvas.fill(area, Color{42, 58, 74, 255});
            }
            canvas.text(path_x, rect.y + 3, shown, kAccent, FontRole::UiBold);
            canvas.text(area.x + area.width - 10, rect.y + 4, "v", kDim,
                        FontRole::MonoSmall);
            source_file_rect_ = area;
        }
    }
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
            canvas.fill(Rect{rect.x + 7, y + 5, 7, 8},
                        session_.breakpoint_enabled(address)
                            ? kBad
                            : Color{226, 190, 64, 255});
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
                   visible, source_scroll_track_, source_scroll_thumb_, 0);
}

void DebuggerUI::draw_source_menu(Canvas& canvas) {
    if (!source_menu_open_) {
        return;
    }
    std::vector<const debug::Source*> sources;
    for (const debug::Source& source : session_.info().sources) {
        if (!source.path.empty()) {
            sources.push_back(&source);
        }
    }
    source_menu_items_.clear();
    if (sources.empty() || source_file_rect_.width <= 0) {
        return;
    }
    const int visible = std::min<int>(14, static_cast<int>(sources.size()));
    const int item_height = 18;
    const int menu_width = std::min(720, canvas.width() - source_file_rect_.x - 8);
    int x = source_file_rect_.x;
    if (menu_width < 320) {
        x = std::max(4, canvas.width() - 328);
    }
    const int width = std::max(320, menu_width);
    const int height = visible * item_height + 8;
    int y = source_file_rect_.y + source_file_rect_.height + 2;
    if (y + height > canvas.height() - kStatusBarHeight) {
        y = source_file_rect_.y - height - 2;
    }
    source_menu_rect_ = Rect{x, y, width, height};
    canvas.fill(Rect{x + 3, y + 3, width, height}, Color{0, 0, 0, 90});
    canvas.fill(source_menu_rect_, Color{26, 34, 42, 255});
    canvas.frame(source_menu_rect_, kAccent, 1);
    const int max_scroll = std::max(0, static_cast<int>(sources.size()) - visible);
    source_menu_scroll_ = std::clamp(source_menu_scroll_, 0, max_scroll);
    for (int row = 0; row < visible; ++row) {
        const int index = source_menu_scroll_ + row;
        if (index >= static_cast<int>(sources.size())) {
            break;
        }
        const debug::Source* source = sources[static_cast<std::size_t>(index)];
        const Rect item{x + 2, y + 4 + row * item_height, width - 4, item_height};
        const bool current = source->id == source_view_id_;
        const bool hovered =
            mouse_x_ >= item.x && mouse_x_ < item.x + item.width &&
            mouse_y_ >= item.y && mouse_y_ < item.y + item.height;
        if (current || hovered) {
            canvas.fill(item, current ? Color{38, 66, 92, 255}
                                      : Color{34, 48, 60, 255});
        }
        canvas.text(item.x + 4, item.y + 2,
                    trim_to(source->path,
                            static_cast<std::size_t>((item.width - 22) / 7)),
                    current ? kText : Color{190, 204, 216, 255},
                    FontRole::MonoSmall);
        if (current) {
            canvas.text(item.x + item.width - 10, item.y + 2, "*", kAccent,
                        FontRole::MonoSmall);
        }
        source_menu_items_.push_back({item, source->id});
    }
    if (max_scroll > 0) {
        canvas.text(x + width - 12, y + 4, "^", kDim, FontRole::MonoSmall);
        canvas.text(x + width - 12, y + height - 14, "v", kDim,
                    FontRole::MonoSmall);
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
    std::uint32_t before = static_cast<std::uint32_t>((rows / 2) * 4);
    std::uint32_t start = pc >= before ? pc - before : 0;
    start &= ~3U;
    const std::int64_t scrolled =
        static_cast<std::int64_t>(start / 4U) + asm_scroll_;
    constexpr std::int64_t kAddressRows = 0x1'0000'0000LL / 4;
    const std::int64_t start_row =
        std::clamp<std::int64_t>(scrolled, 0, kAddressRows - 1);
    start = static_cast<std::uint32_t>(start_row * 4);
    const int address_width = Canvas::text_width("00000000", FontRole::Mono);
    const int bytes_width = std::clamp(rect.width - kAsmGutter - 160, 100, 125);
    const int bytes_x = rect.x + kAsmGutter + 6;
    const int instruction_x = bytes_x + bytes_width + 4;
    asm_rows_.clear();
    canvas.fill(Rect{rect.x + 1, rect.y + 24, kAsmGutter - 1, rect.height - 26}, kPanelAlt);
    canvas.fill(Rect{rect.x + kAsmGutter, rect.y + 24, 1, rect.height - 26}, kBorder);
    canvas.fill(Rect{instruction_x - 4, rect.y + 24, 1, rect.height - 26}, kBorder);
    canvas.text(bytes_x, rect.y + 7, "HEX", kDim, FontRole::MonoSmall);
    std::uint32_t address = start;
    for (int row = 0; row < rows; ++row) {
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
        bool extended = false;
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
            extended = (bytes[1] & 0x10U) != 0;
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
            canvas.fill(Rect{rect.x + 8, y + 5, 7, 8},
                        session_.breakpoint_enabled(address)
                            ? kBad
                            : Color{226, 190, 64, 255});
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
        address += extended ? 8U : 4U;
    }
    draw_scrollbar(canvas, rect,
                   static_cast<int>(start_row),
                   static_cast<int>(kAddressRows), rows, asm_scroll_track_,
                   asm_scroll_thumb_, 1);
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
                   cores_scroll_track_, cores_scroll_thumb_, 2);
}

int DebuggerUI::draw_core_state(Canvas& canvas, Rect rect, std::size_t core_index) {
    const core::Machine& machine = session_.core(core_index);
    const int start_y = rect.y;
    int y = rect.y;
    register_cells_.clear();
    fpr_cells_.clear();
    tile_cells_.clear();
    flag_cells_.clear();
    control_hover_rows_.clear();
    register_cells_.resize(16);
    fpr_cells_.resize(16);
    tile_cells_.resize(16);
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
            register_cells_[static_cast<std::size_t>(index)] = cell;
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
            fpr_cells_[static_cast<std::size_t>(index)] = cell;
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
    y += 9;
    const Rect tsp_row{rect.x - 2, y - 2, rect.width - 4, 18};
    control_hover_rows_.push_back(
        ControlHoverRow{tsp_row, "TSP 0x" + hex(machine.tsp(), 8),
                        "Transform stack pointer (TSP). RDSP/WRSP access it; "
                        "the 4x4 matrix below is tile T0."});
    if (hover_control_ == static_cast<int>(control_hover_rows_.size()) - 1) {
        canvas.fill(tsp_row, Color{42, 58, 74, 255});
    }
    canvas.text(rect.x, y, "TSP " + hex(machine.tsp(), 8), kAccent, FontRole::MonoSmall);
    canvas.text(rect.x + rect.width - 54, y, "T0 4x4", kDim, FontRole::MonoSmall);
    y += 21;
    const int tile_column_width = std::max(46, (rect.width - 8) / 4);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const int index = row * 4 + column;
            const int x = rect.x + column * tile_column_width;
            const Rect cell{x, y - 2, tile_column_width - 4, 17};
            tile_cells_[static_cast<std::size_t>(index)] = cell;
            if (hover_tile_ == index) {
                canvas.fill(cell, Color{42, 58, 74, 255});
                canvas.frame(cell, kAccent);
            } else if (index == editing_tile_) {
                canvas.fill(cell, Color{44, 64, 84, 255});
                canvas.frame(cell, kAccent);
            } else if ((row + column) % 2 == 0) {
                canvas.fill(cell, Color{24, 34, 44, 255});
            }
            char buffer[24] = {};
            std::snprintf(buffer, sizeof(buffer), "%.2f", machine.tile_value(
                0, static_cast<std::size_t>(index)));
            canvas.text(x + 3, y,
                        editing_tile_ == index ? tile_edit_ + "_" : buffer,
                        editing_tile_ == index ? kAccent : kText,
                        FontRole::MonoSmall);
        }
        y += 19;
    }

    canvas.fill(Rect{rect.x, y + 2, rect.width, 1}, kBorder);
    y += 9;
    for (std::uint32_t number : {3U, 6U, 10U}) {
        std::uint32_t value = 0;
        if (machine.read_control_register(number, value)) {
            const std::string title =
                "CR" + std::to_string(number) + " 0x" + hex(value, 8);
            std::string detail;
            if (number == 3) {
                detail = "Virtual base of the 256-entry interrupt vector table (VTBR).";
            } else if (number == 6) {
                detail = "Kernel stack pointer (KSP).";
            } else {
                detail = "Memory control (MCTL): bit 0 PGE, bit 1 WP.";
            }
            const Rect row{rect.x - 2, y - 2, rect.width - 4, 18};
            control_hover_rows_.push_back(ControlHoverRow{row, title, detail});
            if (hover_control_ == static_cast<int>(control_hover_rows_.size()) - 1) {
                canvas.fill(row, Color{42, 58, 74, 255});
            }
            canvas.text(rect.x, y, "CR" + std::to_string(number) + " " + hex(value, 8),
                        kDim, FontRole::MonoSmall);
            y += 18;
        }
    }
    std::uint32_t ptbr = 0;
    if (machine.read_control_register(8, ptbr)) {
        const std::string title = "PTBR 0x" + hex(ptbr, 8);
        const Rect row{rect.x - 2, y - 2, rect.width - 4, 18};
        control_hover_rows_.push_back(
            ControlHoverRow{row, title,
                            "Physical base of the page directory. Must be 4 KiB aligned."});
        if (hover_control_ == static_cast<int>(control_hover_rows_.size()) - 1) {
            canvas.fill(row, Color{42, 58, 74, 255});
        }
        canvas.text(rect.x, y, "PTBR " + hex(ptbr, 8), kDim, FontRole::MonoSmall);
        y += 18;
        for (int index = 0; index < 4; ++index) {
            std::uint32_t entry = 0;
            if (machine.read_physical32(ptbr + static_cast<std::uint32_t>(index * 4), entry)) {
                canvas.text(rect.x, y, "PTE" + std::to_string(index) + " " + hex(entry, 8),
                            kDim, FontRole::MonoSmall);
                y += 14;
            }
        }
    }
    return std::min(y - start_y, rect.height);
}

// Both front ends record array types innermost-first ("int[3][2]" for
// `int m[2][3]`). Reverse the bracket groups so panels and tooltips show the
// declaration order; a non-array type returns an empty string.
std::string array_dimensions_display(std::string_view type) {
    std::vector<std::string> parts;
    std::size_t position = 0;
    while ((position = type.find('[', position)) != std::string_view::npos) {
        const std::size_t close = type.find(']', position);
        if (close == std::string_view::npos) {
            break;
        }
        parts.emplace_back(type.substr(position, close - position + 1));
        position = close + 1;
    }
    std::string result;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        result += *it;
    }
    return result;
}

void DebuggerUI::draw_variables(Canvas& canvas, Rect rect) {
    draw_panel(canvas, rect, "VARIABLES");
    struct Entry {
        std::string name;
        std::string type;
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        std::uint32_t size = 0;
        // Set when the optimizer holds the variable in a register over this PC;
        // there is no frame address to show or to read children from.
        std::string reg_name;
        bool local = false;
        bool ok = false;
        bool header = false;
        bool array = false;
        bool pager = false;
        // An aggregate row the operator can open and close. Collapsed by default:
        // an object with many fields otherwise pushes every other local off the
        // panel.
        bool aggregate = false;
        bool expanded = true;
        int page = 0;
        int page_count = 1;
        int array_count = 0;
        int indent = 0;
        std::string key;
    };
    std::vector<Entry> entries;

    const auto class_for = [&](const std::string& type) -> const debug::ClassInfo* {
        for (const debug::ClassInfo& klass : session_.info().classes) {
            if (klass.name == type) {
                return &klass;
            }
        }
        return nullptr;
    };    std::function<void(Entry, int)> append_entry;
    append_entry = [&](Entry entry, int depth) {
        if (entries.size() >= 256) {
            return;
        }

        constexpr int kPageSize = 16;
        std::string type = entry.type;
        const std::size_t bracket = type.rfind('[');
        int array_count = 0;
        unsigned element_size = 0;
        std::string element_type;
        if (bracket != std::string::npos && type.back() == ']') {
            const std::string count_text = type.substr(bracket + 1, type.size() - bracket - 2);
            array_count = std::atoi(count_text.c_str());
            if (array_count > 0) {
                element_size = entry.size / static_cast<unsigned>(array_count);
                element_type = type.substr(0, bracket);
            }
        }
        if (array_count > 0 && element_size > 0) {
            entry.array = true;
            entry.array_count = array_count;
            entry.page_count = (array_count + kPageSize - 1) / kPageSize;
            const auto expanded = array_expanded_.find(entry.key);
            entry.expanded = expanded != array_expanded_.end() && expanded->second;
            const auto page = array_pages_.find(entry.key);
            entry.page = page == array_pages_.end() ? 0 : page->second;
            entry.page = std::clamp(entry.page, 0, std::max(0, entry.page_count - 1));
        }

        // An aggregate row has to know that it is one before it is stored, because
        // the row is copied into the list here. Collapsed unless the operator has
        // opened it: a class with ten fields otherwise buries the rest of the frame.
        const std::string member_type = debug::pointee_type(entry.type);
        const bool is_aggregate =
            !debug::class_members(session_.info(),
                                  member_type.empty() ? entry.type : member_type)
                 .empty();
        const auto member_open = aggregate_expanded_.find(entry.key);
        const bool aggregate_is_open =
            member_open != aggregate_expanded_.end() && member_open->second;
        entry.aggregate = is_aggregate;
        entry.expanded = aggregate_is_open;

        entries.push_back(entry);
        // A register-held value has no address of its own, but a pointer still
        // has a target: expand through its value rather than its (absent) slot.
        const bool register_pointer = !entry.reg_name.empty() &&
                                      !entry.type.empty() &&
                                      entry.type.back() == '*';
        if (!entry.ok || depth >= 2 || entry.size == 0 ||
            (!entry.reg_name.empty() && !register_pointer)) {
            return;
        }
        if (register_pointer) {
            entry.address = entry.value;
        }

        if (entry.array) {
            if (!entry.expanded) {
                return;
            }
            const int start = entry.page * kPageSize;
            const int end = std::min(entry.array_count, start + kPageSize);
            for (int index = start; index < end && entries.size() < 256; ++index) {
                Entry child;
                child.name = "[" + std::to_string(index) + "]";
                child.type = element_type;
                child.address = entry.address +
                                static_cast<std::uint32_t>(index) * element_size;
                child.size = element_size;
                child.local = entry.local;
                child.indent = entry.indent + 1;
                child.key = entry.key + "[" + std::to_string(index) + "]";
                child.ok = session_.read_memory(child.address, child.size, child.value);
                append_entry(std::move(child), depth + 1);
            }
            if (entry.page_count > 1 && entries.size() < 256) {
                Entry pager;
                pager.name = "< page " + std::to_string(entry.page + 1) + "/" +
                             std::to_string(entry.page_count) + " >";
                pager.pager = true;
                pager.key = entry.key;
                pager.page = entry.page;
                pager.page_count = entry.page_count;
                pager.local = entry.local;
                pager.indent = entry.indent + 1;
                entries.push_back(std::move(pager));
            }
            return;
        }

        // The front ends spell a pointer three ways; the model decides which.
        const std::string pointee = debug::pointee_type(type);
        const bool pointer = !pointee.empty();

        const std::string structure = pointer ? pointee : type;
        // Inherited members live in the base class's record; a derived object with
        // no fields of its own shows nothing without them.
        if (!is_aggregate || !aggregate_is_open) {
            return;
        }
        const std::uint32_t storage = pointer ? entry.value : entry.address;
        const std::vector<debug::MemberEntry> members = debug::member_entries(
            session_.info(), type, storage,
            [&](std::uint32_t address, std::uint32_t size, std::uint32_t& value) {
                return session_.read_memory(address, size, value);
            });
        for (const debug::MemberEntry& member : members) {
            if (entries.size() >= 256) break;
            Entry child;
            child.name = "." + member.name;
            child.type = member.type;
            child.address = member.address;
            child.size = member.size;
            child.local = entry.local;
            child.indent = entry.indent + 1;
            child.key = entry.key + "." + member.name;
            child.ok = member.ok;
            child.value = member.value;
            append_entry(std::move(child), depth + 1);
        }
    };

    std::vector<Entry> locals;
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
            entry.key = "L:" + function->name + ":" + variable.name;
            if (const debug::Location* location = session_.location_at(
                    function->name, variable.name, session_.machine().pc())) {
                entry.reg_name = (location->fp ? "F" : "R") +
                                 std::to_string(location->reg);
                entry.address = 0;
            }
            std::string error;
            entry.ok = session_.read_variable(variable.name, entry.value, error);
            locals.push_back(std::move(entry));
        }
    }
    std::vector<Entry> globals;
    for (const debug::Global& global : session_.info().globals) {
        Entry entry;
        entry.name = global.name;
        entry.type = global.type;
        entry.address = global.address;
        entry.size = global.size;
        entry.key = "G:" + global.name;
        std::string error;
        entry.ok = session_.read_global(global.name, entry.value, error);
        globals.push_back(std::move(entry));
    }

    Entry locals_header;
    locals_header.name = "Locals";
    locals_header.header = true;
    locals_header.expanded = locals_expanded_;
    locals_header.size = static_cast<std::uint32_t>(locals.size());
    entries.push_back(locals_header);
    if (locals_expanded_) {
        for (Entry& entry : locals) {
            append_entry(std::move(entry), 0);
        }
    }

    Entry globals_header;
    globals_header.name = "Globals";
    globals_header.header = true;
    globals_header.expanded = globals_expanded_;
    globals_header.size = static_cast<std::uint32_t>(globals.size());
    entries.push_back(globals_header);
    if (globals_expanded_) {
        for (Entry& entry : globals) {
            append_entry(std::move(entry), 0);
        }
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
        std::string text;
        if (entry.header) {
            text = std::string(entry.expanded ? "v " : "> ") + entry.name + " (" +
                   std::to_string(entry.size) + ")";
        } else if (entry.pager) {
            text = std::string(static_cast<std::size_t>(entry.indent * 2), ' ') + entry.name;
        } else if (entry.array) {
            const std::string indent_text(static_cast<std::size_t>(entry.indent * 2), ' ');
            std::string dimensions = array_dimensions_display(entry.type);
            if (dimensions.empty()) {
                dimensions = "[" + std::to_string(entry.array_count) + "]";
            }
            text = indent_text + (entry.expanded ? "[-] " : "[+] ") + entry.name +
                   dimensions +
                   (entry.expanded && entry.page_count > 1
                        ? " (page " + std::to_string(entry.page + 1) + ")"
                        : "");
        } else {
            const std::string indent_text(static_cast<std::size_t>(entry.indent * 2), ' ');
            std::string value_text =
                format_scalar_value(entry.type, entry.value, entry.ok);
            if (entry.ok && class_for(entry.type) != nullptr) {
                value_text = "{...}";
            }
            text = indent_text + entry.name + " = " + value_text;
        }
        canvas.text(rect.x + 6, y, trim_to(text, static_cast<std::size_t>((rect.width - 20) / 7)),
                    entry.header ? kAccent
                                 : (entry.pager ? kDim
                                                : (entry.ok ? (entry.indent > 0 ? kDim : kText)
                                                            : kBad)),
                    FontRole::MonoSmall);
        var_hover_rows_.push_back(
            VarHoverRow{row, entry.name, entry.type, entry.address, entry.value, entry.size,
                        entry.reg_name, entry.local, entry.ok, entry.header, entry.array,
                        entry.pager, entry.aggregate, entry.key, entry.page,
                        entry.page_count});
    }
    if (entries.empty()) {
        canvas.text(rect.x + 6, rect.y + 24, "(no variables)", kDim, FontRole::MonoSmall);
    }
    draw_scrollbar(canvas, rect, variables_scroll_, static_cast<int>(entries.size()), visible,
                   variables_scroll_track_, variables_scroll_thumb_, 3);
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
                source_follow_ = true;
                asm_scroll_ = 0;
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

void DebuggerUI::begin_tile_edit(int index) {
    if (index < 0 || index > 15) return;
    editing_tile_ = index;
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.6g",
                  session_.core(selected_core_).tile_value(
                      0, static_cast<std::size_t>(index)));
    tile_edit_ = buffer;
    tile_edit_fresh_ = true;
}

void DebuggerUI::finish_tile_edit(bool commit) {
    if (editing_tile_ < 0) return;
    if (commit && !tile_edit_.empty()) {
        char* end = nullptr;
        const float value = std::strtof(tile_edit_.c_str(), &end);
        if (end != nullptr && *end == '\0') {
            session_.core(selected_core_).set_tile_value(
                0, static_cast<std::size_t>(editing_tile_), value);
            status_ = "T" + std::to_string(editing_tile_ / 4) + "[" +
                      std::to_string(editing_tile_ % 4) + "] updated";
        } else {
            status_ = "invalid floating-point value";
        }
    }
    editing_tile_ = -1;
    tile_edit_.clear();
    tile_edit_fresh_ = false;
}

void DebuggerUI::draw_tile_popup(Canvas& canvas) {
    if (editing_tile_ < 0) return;
    const int width = 430;
    const int height = 170;
    const int x = (canvas.width() - width) / 2;
    const int y = (canvas.height() - height) / 2;
    tile_popup_ = Rect{x, y, width, height};
    tile_input_rect_ = Rect{x + 16, y + 42, width - 32, 24};
    tile_ok_rect_ = Rect{x + width - 190, y + height - 40, 80, 24};
    tile_cancel_rect_ = Rect{x + width - 100, y + height - 40, 84, 24};
    canvas.fill(tile_popup_, Color{18, 26, 34, 255});
    canvas.frame(tile_popup_, kAccent, 2);
    canvas.text(x + 16, y + 14,
                "Edit T" + std::to_string(editing_tile_ / 4) + "[" +
                    std::to_string(editing_tile_ % 4) + "]",
                kAccent, FontRole::UiBold);
    canvas.text(x + 16, y + 28,
                "Floating point: 3.14159   100   1e2   -0.5",
                kDim, FontRole::MonoSmall);
    canvas.fill(tile_input_rect_, kPanelAlt);
    canvas.frame(tile_input_rect_, kAccent);
    canvas.text(tile_input_rect_.x + 6, tile_input_rect_.y + 4,
                tile_edit_ + "_", kText, FontRole::MonoSmall);
    char expanded[32] = {};
    char* end = nullptr;
    const float value = std::strtof(tile_edit_.c_str(), &end);
    if (end != nullptr && *end == '\0') {
        std::snprintf(expanded, sizeof(expanded), "%.6f", value);
    } else {
        std::snprintf(expanded, sizeof(expanded), "invalid");
    }
    canvas.text(x + 16, y + 76, "Value: " + std::string(expanded), kDim,
                FontRole::MonoSmall);
    canvas.text(x + 16, y + 94, "Stored as 32-bit float in tile T0.", kDim,
                FontRole::MonoSmall);
    canvas.fill(tile_ok_rect_, kPanelAlt);
    canvas.frame(tile_ok_rect_, kBorder);
    canvas.text(tile_ok_rect_.x + 26, tile_ok_rect_.y + 4, "OK", kText,
                FontRole::MonoSmall);
    canvas.fill(tile_cancel_rect_, kPanelAlt);
    canvas.frame(tile_cancel_rect_, kBorder);
    canvas.text(tile_cancel_rect_.x + 10, tile_cancel_rect_.y + 4, "CANCEL", kText,
                FontRole::MonoSmall);
}

void DebuggerUI::draw_tile_tooltip(Canvas& canvas) {
    if (hover_tile_ < 0 || hover_tile_ > 15 || editing_tile_ >= 0 || menu_.open) return;
    const float value = session_.core(selected_core_).tile_value(
        0, static_cast<std::size_t>(hover_tile_));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char expanded[32] = {};
    std::snprintf(expanded, sizeof(expanded), "%.6f", value);
    std::vector<std::string> lines{
        "T" + std::to_string(hover_tile_ / 4) + "[" +
            std::to_string(hover_tile_ % 4) + "]",
        "value: " + std::string(expanded),
        "hex: 0x" + hex(bits, 8),
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

void DebuggerUI::draw_control_tooltip(Canvas& canvas) {
    if (hover_control_ < 0 ||
        hover_control_ >= static_cast<int>(control_hover_rows_.size()) ||
        editing_tile_ >= 0 || menu_.open) {
        return;
    }
    const ControlHoverRow& row =
        control_hover_rows_[static_cast<std::size_t>(hover_control_)];
    std::vector<std::string> lines{row.title, row.detail};
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
    if (row.header) {
        return;
    }
    if (row.pager) {
        return;
    }
    std::vector<std::string> lines;
    lines.push_back(row.name + (row.local ? "  (local)" : "  (global)"));
    std::string pretty_type = row.type;
    std::replace(pretty_type.begin(), pretty_type.end(), '_', ' ');
    const std::string type_dimensions = array_dimensions_display(pretty_type);
    if (!type_dimensions.empty()) {
        pretty_type = pretty_type.substr(0, pretty_type.find('[')) + type_dimensions;
    }
    lines.push_back("type: " + (pretty_type.empty() ? "?" : pretty_type));
    lines.push_back("size: " + std::to_string(row.size) + " bytes");
    if (row.reg_name.empty()) {
        lines.push_back("address: 0x" + hex(row.address, 8));
    } else {
        lines.push_back("register: " + row.reg_name);
    }

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
                const std::string element_type =
                    bracket == std::string::npos ? std::string{}
                                                 : row.type.substr(0, bracket);
                for (int index = 0; index < std::min(4, count); ++index) {
                    std::uint32_t element = 0;
                    if (session_.read_memory(row.address + static_cast<std::uint32_t>(index * 4),
                                             4, element)) {
                        lines.push_back("[" + std::to_string(index) + "] = " +
                                        format_scalar_value(element_type, element, true));
                    }
                }
            }
        }
    } else {
        std::uint32_t value = row.value;
        if (row.size == 2) {
            value &= 0xFFFFU;
        }
        lines.push_back("value: " + format_scalar_value(row.type, value, row.ok));
        if (row.type == "float") {
            lines.push_back("bits: 0x" + hex(value, 8));
        }
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

void DebuggerUI::draw_breakpoints(Canvas& canvas, Rect rect) {
    const auto& breakpoints = session_.breakpoints();
    draw_panel(canvas, rect,
               "BREAKPOINTS (" + std::to_string(breakpoints.size()) + ")");
    breakpoint_rows_.clear();
    const int line_height = 18;
    const int top = rect.y + 24;
    const int visible = std::max(1, (rect.height - 28) / line_height);
    canvas.fill(Rect{rect.x + 1, top, 22, rect.height - 26}, kPanelAlt);
    canvas.fill(Rect{rect.x + 22, top, 1, rect.height - 26}, kBorder);
    const int total = static_cast<int>(breakpoints.size());
    breakpoints_scroll_ =
        std::clamp(breakpoints_scroll_, 0, std::max(0, total - visible));
    draw_scrollbar(canvas, rect, breakpoints_scroll_, total, visible,
                   breakpoints_scroll_track_, breakpoints_scroll_thumb_, 5);
    if (breakpoints.empty()) {
        canvas.text(rect.x + 8, top + 2, "(no breakpoints)", kDim,
                    FontRole::MonoSmall);
        return;
    }
    int row_index = 0;
    for (const std::uint32_t address : breakpoints) {
        if (row_index++ < breakpoints_scroll_) {
            continue;
        }
        const int row = row_index - 1 - breakpoints_scroll_;
        if (row >= visible) {
            break;
        }
        const int y = top + row * line_height;
        const bool enabled = session_.breakpoint_enabled(address);
        const bool hovered =
            mouse_x_ >= rect.x + 1 && mouse_x_ < rect.x + rect.width - 12 &&
            mouse_y_ >= y - 1 && mouse_y_ < y + line_height - 1;
        if (hovered) {
            canvas.fill(Rect{rect.x + 1, y - 1, rect.width - 14,
                             line_height},
                        Color{34, 46, 58, 255});
        }
        const Color color =
            enabled ? kBad : Color{226, 190, 64, 255};
        canvas.fill(Rect{rect.x + 7, y + 4, 9, 10}, color);
        canvas.text(rect.x + 25, y + 2, "0x" + hex(address, 8),
                    enabled ? kText : Color{190, 180, 120, 255},
                    FontRole::MonoSmall);
        std::string filename;
        int source_line = 0;
        for (const debug::Line& line : session_.info().lines) {
            if (line.address != address) {
                continue;
            }
            source_line = line.line;
            const std::string path = source_path_for(line.source);
            if (!path.empty()) {
                filename = std::filesystem::path(path).filename().string();
            }
            break;
        }
        if (filename.empty()) {
            filename = "<no source>";
        }
        const int filename_width =
            std::max(20, rect.width - 108 - 40);
        canvas.text(rect.x + 108, y + 2,
                    trim_to(filename,
                            static_cast<std::size_t>(filename_width / 7)),
                    kDim, FontRole::MonoSmall);
        if (source_line > 0) {
            const std::string number = std::to_string(source_line);
            const int number_width =
                Canvas::text_width(number, FontRole::MonoSmall);
            canvas.text(rect.x + rect.width - 12 - number_width, y + 2,
                        number, kDim, FontRole::MonoSmall);
        }
        breakpoint_rows_.push_back(
            {address, Rect{rect.x + 1, y - 1, rect.width - 14, line_height},
             Rect{rect.x + 4, y + 2, 14, 14}});
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
                   stack_scroll_track_, stack_scroll_thumb_, 4);
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
    if (console_hover_) {
        canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2, 20}, kPanelAlt);
        canvas.text(rect.x + 6, rect.y + 3, "CONSOLE", Color{86, 210, 126, 255},
                    FontRole::UiBold);
    }
    std::vector<std::string> lines;
    std::istringstream stream(os_mode_ ? serial_text_ : session_.console());
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    if (lines.empty()) {
        console_scroll_ = 0;
        console_line_count_ = 0;
        canvas.text(rect.x + 6, rect.y + 24, "(no console output)", kDim, FontRole::MonoSmall);
        draw_scrollbar(canvas, rect, 0, 0, 1, console_scroll_track_,
                       console_scroll_thumb_, 6);
        return;
    }
    const int row_height = 16;
    const int visible = std::max(1, (rect.height - 26) / row_height);
    const int line_count = static_cast<int>(lines.size());
    console_scroll_ = console_scroll_after_output(
        console_scroll_, console_line_count_, line_count, visible);
    console_line_count_ = line_count;
    for (int index = console_scroll_;
         index < static_cast<int>(lines.size()) && index < console_scroll_ + visible;
         ++index) {
        const int y = rect.y + 24 + (index - console_scroll_) * row_height;
        canvas.text(rect.x + 6, y,
                    trim_to(lines[static_cast<std::size_t>(index)],
                            static_cast<std::size_t>((rect.width - 20) / 8)),
                    kText, FontRole::MonoSmall);
    }
    draw_scrollbar(canvas, rect, console_scroll_, static_cast<int>(lines.size()),
                   visible, console_scroll_track_, console_scroll_thumb_, 6);
}

DebuggerUI::Layout DebuggerUI::layout(int width, int height) const {
    const int right_width = std::clamp(width * 28 / 100, 270, 420);
    int source_width = std::clamp(width * 42 / 100, 160,
                                  std::max(160, width - right_width - 300));
    int disassembly_width = width - source_width - right_width;
    if (disassembly_width < 300) {
        disassembly_width = 300;
        source_width = width - disassembly_width - right_width;
    }
    const int content_height = height - kStatusBarHeight;
    const int bottom_height = std::clamp(content_height * 24 / 100, 190, 230);
    const int bottom_top = content_height - bottom_height;
    const int top_height = bottom_top - kToolbarHeight;
    constexpr int memory_height = 112;
    const int source_height = std::max(120, top_height - memory_height);
    const int variables_width = width * 34 / 100;
    const int breakpoints_width = width * 22 / 100;
    const int stack_width = width * 22 / 100;
    const int console_width =
        width - variables_width - breakpoints_width - stack_width;

    Layout result;
    constexpr int tab_height = 26;
    result.tabs = Rect{0, kToolbarHeight, source_width, tab_height};
    result.source = Rect{0, kToolbarHeight + tab_height, source_width,
                         std::max(80, source_height - tab_height)};
    result.screen = result.source;
    result.disassembly = Rect{source_width, kToolbarHeight, disassembly_width, top_height};
    result.cores = Rect{source_width + disassembly_width, kToolbarHeight, right_width,
                        top_height};
    result.memory = Rect{0, kToolbarHeight + source_height, source_width, memory_height};
    result.variables = Rect{0, bottom_top, variables_width, bottom_height};
    result.breakpoints =
        Rect{variables_width, bottom_top, breakpoints_width, bottom_height};
    result.call_stack = Rect{variables_width + breakpoints_width, bottom_top,
                             stack_width, bottom_height};
    result.console = Rect{variables_width + breakpoints_width + stack_width,
                          bottom_top, console_width, bottom_height};
    return result;
}

void DebuggerUI::draw_scrollbar(Canvas& canvas, Rect panel, int offset, int total,
                                int visible, Rect& track, Rect& thumb,
                                int panel_id) {
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
    scroll_metrics_[panel_id] =
        ScrollMetrics{max_offset, std::max(0, track.height - thumb.height)};
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
        const bool enabled = session_.breakpoint_enabled(address);
        session_.set_breakpoint_enabled(address, !enabled);
        status_ = enabled ? "breakpoint disabled at " + hex(address, 8)
                          : "breakpoint enabled at " + hex(address, 8);
    } else {
        session_.set_breakpoint(address, true);
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
    const bool exists =
        session_.breakpoints().find(address) != session_.breakpoints().end();
    if (exists && !session_.breakpoint_enabled(address)) {
        status_ = "breakpoint is disabled at " + hex(address, 8);
        return;
    }
    const bool owned = !exists;
    if (owned) {
        session_.set_breakpoint(address);
    }
    if (continuous_run_) {
        continuous_run_ = false;
        if (continuous_owned_breakpoint_) {
            session_.clear_breakpoint(continuous_target_);
        }
    }
    continuous_run_ = true;
    continuous_has_target_ = true;
    continuous_owned_breakpoint_ = owned;
    continuous_target_ = address;
    continuous_remaining_ = settings_run_limit_;
    status_ = "running to " + hex(address, 8) + "; press Run to pause";
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
        for (const AsmRow& row : asm_rows_) {
            if (y >= row.row.y && y < row.row.y + row.row.height) {
                address = row.address;
                break;
            }
        }
        if (address == 0) {
            return;
        }
    } else {
        for (const BreakpointRow& row : breakpoint_rows_) {
            if (row.row.contains(x, y)) {
                address = row.address;
                break;
            }
        }
        if (address == 0) {
            return;
        }
    }
    if (source && address == 0) {
        (void)source_line_to_address(line, address);
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
    menu_.has_breakpoint =
        session_.breakpoints().find(address) != session_.breakpoints().end();
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
    std::string body =
        hover_asm_operand_ ? addressing_help(row.text, mnemonic)
                           : instruction_help(mnemonic);
    if (!hover_asm_operand_ && row.address == core::kRomBase && mnemonic == ".word") {
        body = "BIOS reset-vector word. The CPU reads this word from the ROM base "
               "at reset and starts at the address it contains; it is data, not an "
               "instruction.";
    }

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
    canvas.text(menu_.rect.x + 10, menu_.toggle_rect.y + 4,
                menu_.has_breakpoint ? "Remove breakpoint" : "Set breakpoint",
                kText, FontRole::MonoSmall);
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
        if (menu_.has_breakpoint && address != 0) {
            session_.clear_breakpoint(address);
            status_ = "breakpoint removed at " + hex(address, 8);
        } else if (source) {
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
    if (screen_tab_active_ && !scroll_dragging_) {
        screen_hover_x_ = -1;
        screen_hover_y_ = -1;
        if (screen_view_.contains(x, y) && screen_has_frame_ &&
            screen_view_.width > 0 && screen_view_.height > 0) {
            const int frame_w = static_cast<int>(screen_frame_.width);
            const int frame_h = static_cast<int>(screen_frame_.height);
            const int local_x = x - screen_view_.x;
            const int local_y = y - screen_view_.y;
            const int px = screen_downscale_
                               ? local_x * frame_w / screen_view_.width
                               : local_x / std::max(1, screen_scale_);
            const int py = screen_downscale_
                               ? local_y * frame_h / screen_view_.height
                               : local_y / std::max(1, screen_scale_);
            if (px >= 0 && px < frame_w && py >= 0 && py < frame_h) {
                screen_hover_x_ = px;
                screen_hover_y_ = py;
                screen_hover_address_ =
                    core::kFramebufferBase + screen_frame_.offset +
                    static_cast<std::uint32_t>(py) * screen_frame_.stride +
                    static_cast<std::uint32_t>(px) * 4U;
                const std::size_t offset =
                    static_cast<std::size_t>(py) * screen_frame_.stride +
                    static_cast<std::size_t>(px) * 4U;
                screen_hover_color_ = 0;
                if (offset + 2U < screen_pixels_.size()) {
                    screen_hover_color_ =
                        (static_cast<std::uint32_t>(screen_pixels_[offset + 2]) << 16) |
                        (static_cast<std::uint32_t>(screen_pixels_[offset + 1]) << 8) |
                        static_cast<std::uint32_t>(screen_pixels_[offset]);
                }
            }
        }
        return;
    }
    if (scroll_dragging_) {
        if (scroll_drag_panel_ == 1) {
            // The disassembly scrollbar covers the full address space; use a
            // row-rate drag rather than mapping the thumb across 4 GiB.
            const int rows = (y - scroll_drag_last_y_) / 14;
            if (rows != 0) {
                scroll_drag_last_y_ = y;
                asm_scroll_ += rows;
            }
            return;
        }
        const auto metrics = scroll_metrics_.find(scroll_drag_panel_);
        if (metrics != scroll_metrics_.end() && metrics->second.span > 0) {
            const int next = scroll_offset_for_drag(
                scroll_drag_origin_offset_, scroll_drag_origin_y_, y,
                metrics->second.max_offset, metrics->second.span);
            switch (scroll_drag_panel_) {
            case 0:
                source_follow_ = false;
                source_scroll_ = next;
                break;
            case 2:
                cores_scroll_ = next * 14;
                break;
            case 3:
                variables_scroll_ = next;
                break;
            case 4:
                stack_scroll_ = next;
                break;
            case 5:
                breakpoints_scroll_ = next;
                break;
            case 6:
                console_scroll_ = next;
                break;
            default:
                break;
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
    if (settings_open_) {
        hover_settings_control_ = -1;
        if (settings_close_.contains(x, y)) hover_settings_control_ = 5;
        else if (settings_args_rect_.contains(x, y)) hover_settings_control_ = 0;
        else if (settings_run_rect_.contains(x, y)) hover_settings_control_ = 6;
        else if (settings_stop_box_.contains(x, y)) hover_settings_control_ = 1;
        else if (settings_listing_box_.contains(x, y)) hover_settings_control_ = 2;
        else if (settings_apply_rect_.contains(x, y)) hover_settings_control_ = 3;
        else if (settings_cancel_rect_.contains(x, y)) hover_settings_control_ = 4;
        return;
    }
    const Layout areas = layout(width, height);
    hover_settings_control_ = -1;
    console_hover_ = areas.console.contains(x, y);
    if (settings_rect_.contains(x, y)) {
        hover_settings_control_ = 100;
    } else if (stats_reset_rect_.contains(x, y)) {
        hover_settings_control_ = 101;
    }
    hover_row_ = -1;
    hover_source_ = false;
    hover_asm_index_ = -1;
    hover_asm_operand_ = false;
    hover_var_ = -1;
    hover_flag_ = -1;
    hover_fpr_ = -1;
    hover_tile_ = -1;
    hover_control_ = -1;
    hover_register_ = -1;
    hover_memory_byte_ = -1;
    for (std::size_t index = 0; index < fpr_cells_.size(); ++index) {
        if (fpr_cells_[index].contains(x, y)) {
            hover_fpr_ = static_cast<int>(index);
            break;
        }
    }
    for (std::size_t index = 0; index < tile_cells_.size(); ++index) {
        if (tile_cells_[index].contains(x, y)) {
            hover_tile_ = static_cast<int>(index);
            break;
        }
    }
    for (std::size_t index = 0; index < control_hover_rows_.size(); ++index) {
        if (control_hover_rows_[index].row.contains(x, y)) {
            hover_control_ = static_cast<int>(index);
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
        if (settings_open_) {
            draw_settings(canvas);
        }
        return;
    }
    const Layout areas = layout(canvas.width(), canvas.height());
    draw_tabs(canvas, areas.tabs);
    if (screen_tab_active_) {
        draw_screen(canvas, areas.source);
    } else {
        draw_source(canvas, areas.source);
    }
    draw_disassembly(canvas, areas.disassembly);
    draw_cores(canvas, areas.cores);
    draw_memory(canvas, areas.memory);
    draw_variables(canvas, areas.variables);
    draw_breakpoints(canvas, areas.breakpoints);
    draw_call_stack(canvas, areas.call_stack);
    if (os_mode_) {
        std::string output = devices_.serial().take_output();
        for (char character : output) {
            if (character == '\f') {
                serial_text_.clear();
            } else {
                serial_text_.push_back(character);
            }
        }
    }
    draw_console(canvas, areas.console);
    draw_status_bar(canvas);
    draw_error(canvas);
    draw_memory_tooltip(canvas);
    draw_register_tooltip(canvas);
    draw_flag_tooltip(canvas);
    draw_variable_tooltip(canvas);
    draw_fpr_tooltip(canvas);
    draw_tile_tooltip(canvas);
    draw_control_tooltip(canvas);
    draw_asm_tooltip(canvas);
    draw_screen_tooltip(canvas);
    draw_context_menu(canvas);
    draw_source_menu(canvas);
    draw_fpr_popup(canvas);
    draw_tile_popup(canvas);
    if (settings_open_) {
        draw_settings(canvas);
    }
}

bool DebuggerUI::click(int x, int y, int width, int height) {
    if (!settings_open_ && loaded_) {
        const Layout areas = layout(width, height);
        if (areas.tabs.contains(x, y)) {
            if (screen_tab_source_.contains(x, y)) {
                screen_tab_active_ = false;
                screen_focus_ = false;
                return true;
            }
            if (screen_tab_screen_.contains(x, y)) {
                screen_tab_active_ = true;
                return true;
            }
            return false;
        }
        if (screen_tab_active_ && areas.source.contains(x, y)) {
            screen_focus_ = true;
            const int frame_w = static_cast<int>(screen_frame_.width);
            const int frame_h = static_cast<int>(screen_frame_.height);
            if (screen_has_frame_ && screen_view_.contains(x, y) && frame_w > 0 &&
                frame_h > 0 && screen_view_.width > 0 && screen_view_.height > 0) {
                const int local_x = x - screen_view_.x;
                const int local_y = y - screen_view_.y;
                const int px = screen_downscale_
                                   ? local_x * frame_w / screen_view_.width
                                   : local_x / std::max(1, screen_scale_);
                const int py = screen_downscale_
                                   ? local_y * frame_h / screen_view_.height
                                   : local_y / std::max(1, screen_scale_);
                if (px >= 0 && px < frame_w && py >= 0 && py < frame_h) {
                    memory_address_ =
                        core::kFramebufferBase + screen_frame_.offset +
                        static_cast<std::uint32_t>(py) * screen_frame_.stride +
                        static_cast<std::uint32_t>(px) * 4U;
                }
            }
            return true;
        }
    }
    if (settings_open_) {
        if (settings_close_.contains(x, y) || settings_cancel_rect_.contains(x, y) ||
            !settings_modal_.contains(x, y)) {
            close_settings(false);
            return true;
        }
        if (settings_args_rect_.contains(x, y)) {
            settings_args_focused_ = true;
            settings_run_focused_ = false;
            return true;
        }
        if (settings_run_rect_.contains(x, y)) {
            settings_args_focused_ = false;
            settings_run_focused_ = true;
            return true;
        }
        settings_args_focused_ = false;
        settings_run_focused_ = false;
        if (settings_stop_box_.contains(x, y)) {
            settings_stop_at_main_ = !settings_stop_at_main_;
            save_settings();
            return true;
        }
        if (settings_listing_box_.contains(x, y)) {
            settings_write_listing_ = !settings_write_listing_;
            save_settings();
            return true;
        }
        if (settings_apply_rect_.contains(x, y)) {
            apply_settings();
            return true;
        }
        return true;
    }
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
    if (source_menu_open_) {
        if (source_file_rect_.contains(x, y)) {
            source_menu_open_ = false;
            return true;
        }
        for (const auto& item : source_menu_items_) {
            if (item.first.contains(x, y)) {
                select_source(item.second);
                return true;
            }
        }
        source_menu_open_ = false;
        return true;
    }
    const Layout areas = layout(width, height);
    if (source_file_rect_.contains(x, y)) {
        open_source_menu();
        return true;
    }
    if (editing_tile_ >= 0) {
        if (tile_ok_rect_.contains(x, y)) {
            finish_tile_edit(true);
        } else if (tile_cancel_rect_.contains(x, y)) {
            finish_tile_edit(false);
        }
        return true;
    }
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
        const auto now = std::chrono::steady_clock::now();
        const bool double_click =
            last_register_click_ == static_cast<int>(index) &&
            now - last_register_click_time_ < std::chrono::milliseconds(400);
        last_register_click_ = static_cast<int>(index);
        last_register_click_time_ = now;
        if (double_click) {
            finish_register_edit(false);
            memory_address_ = session_.machine().reg(index) & ~0xFU;
            hover_memory_byte_ = -1;
            status_ = "memory: 0x" + hex(memory_address_, 8);
            last_register_click_ = -1;
            return true;
        }
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
    for (std::size_t index = 0; index < tile_cells_.size(); ++index) {
        if (!tile_cells_[index].contains(x, y)) continue;
        finish_memory_address_edit(true);
        finish_register_edit(true);
        begin_tile_edit(static_cast<int>(index));
        return true;
    }
    if (pc_cell_.contains(x, y)) {
        const auto now = std::chrono::steady_clock::now();
        const bool double_click =
            last_register_click_ == 16 &&
            now - last_register_click_time_ < std::chrono::milliseconds(400);
        last_register_click_ = 16;
        last_register_click_time_ = now;
        if (double_click) {
            finish_register_edit(false);
            memory_address_ = session_.machine().pc() & ~0xFU;
            hover_memory_byte_ = -1;
            status_ = "memory: 0x" + hex(memory_address_, 8);
            last_register_click_ = -1;
            return true;
        }
        finish_memory_address_edit(true);
        begin_register_edit(16);
        return true;
    }
    if (sp_cell_.contains(x, y)) {
        const auto now = std::chrono::steady_clock::now();
        const bool double_click =
            last_register_click_ == 17 &&
            now - last_register_click_time_ < std::chrono::milliseconds(400);
        last_register_click_ = 17;
        last_register_click_time_ = now;
        if (double_click) {
            finish_register_edit(false);
            memory_address_ = session_.machine().sp() & ~0xFU;
            hover_memory_byte_ = -1;
            status_ = "memory: 0x" + hex(memory_address_, 8);
            last_register_click_ = -1;
            return true;
        }
        finish_memory_address_edit(true);
        begin_register_edit(17);
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < var_hover_rows_.size(); ++index) {
        const VarHoverRow& row = var_hover_rows_[index];
        if (!row.row.contains(x, y)) continue;
        if (row.header) {
            if (row.name == "Locals") {
                locals_expanded_ = !locals_expanded_;
            } else if (row.name == "Globals") {
                globals_expanded_ = !globals_expanded_;
            }
            last_variable_click_ = -1;
            return true;
        }
        if (row.pager) {
            int page = row.page;
            // The left-hand "<" steps back; everything else on the pager row
            // advances.  This keeps the page text itself clickable.
            if (x < row.row.x + 22) {
                page = std::max(0, page - 1);
            } else {
                page = std::min(row.page_count - 1, page + 1);
            }
            array_pages_[row.key] = page;
            status_ = "array page " + std::to_string(page + 1) + "/" +
                      std::to_string(row.page_count);
            return true;
        }
        if (row.aggregate) {
            const auto open = aggregate_expanded_.find(row.key);
            const bool currently_open = open != aggregate_expanded_.end() && open->second;
            aggregate_expanded_[row.key] = !currently_open;
            last_variable_click_ = -1;
            return true;
        }
        if (row.array) {
            const auto expanded = array_expanded_.find(row.key);
            const bool currently_expanded =
                expanded != array_expanded_.end() && expanded->second;
            array_expanded_[row.key] = !currently_expanded;
            last_variable_click_ = -1;
            return true;
        }
        const bool double_click =
            last_variable_click_ == static_cast<int>(index) &&
            now - last_variable_click_time_ < std::chrono::milliseconds(400);
        last_variable_click_ = static_cast<int>(index);
        last_variable_click_time_ = now;
        if (double_click) {
            const bool pointer = row.type.find('*') != std::string::npos;
            const std::uint32_t target = pointer ? row.value : row.address;
            memory_address_ = target & ~0xFU;
            hover_memory_byte_ = -1;
            status_ = "memory: 0x" + hex(memory_address_, 8);
            last_variable_click_ = -1;
        }
        return true;
    }
    if (source_scroll_track_.contains(x, y)) source_follow_ = false;
    const auto scroll_bar_click = [&](const Rect& track, const Rect& thumb, int& offset,
                                      int page, int panel_id) {
        if (!track.contains(x, y)) return false;
        if (thumb.contains(x, y)) {
            scroll_dragging_ = true;
            scroll_drag_panel_ = panel_id;
            scroll_drag_origin_y_ = y;
            scroll_drag_origin_offset_ = offset;
            scroll_drag_last_y_ = y;
        } else {
            const auto metrics = scroll_metrics_.find(panel_id);
            offset += y < thumb.y ? -page : page;
            if (metrics != scroll_metrics_.end()) {
                offset = std::clamp(offset, 0, metrics->second.max_offset);
            }
        }
        return true;
    };
    if (scroll_bar_click(source_scroll_track_, source_scroll_thumb_, source_scroll_, 10, 0) ||
        scroll_bar_click(asm_scroll_track_, asm_scroll_thumb_, asm_scroll_, 5, 1) ||
        scroll_bar_click(cores_scroll_track_, cores_scroll_thumb_, cores_scroll_, 70, 2) ||
        scroll_bar_click(variables_scroll_track_, variables_scroll_thumb_, variables_scroll_,
                         5, 3) ||
        scroll_bar_click(breakpoints_scroll_track_,
                         breakpoints_scroll_thumb_, breakpoints_scroll_, 5, 5) ||
        scroll_bar_click(stack_scroll_track_, stack_scroll_thumb_, stack_scroll_, 5, 4) ||
        scroll_bar_click(console_scroll_track_, console_scroll_thumb_, console_scroll_, 5, 6)) {
        return true;
    }
    if (y < kToolbarHeight) {
        if (settings_rect_.contains(x, y)) {
            open_settings();
            return true;
        }
        if (stats_reset_rect_.contains(x, y)) {
            reset_stats();
            status_ = "statistics reset";
            return true;
        }
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
    for (const BreakpointRow& row : breakpoint_rows_) {
        if (row.gutter.contains(x, y)) {
            session_.set_breakpoint_enabled(
                row.address,
                !session_.breakpoint_enabled(row.address));
            status_ = session_.breakpoint_enabled(row.address)
                          ? "breakpoint enabled at " + hex(row.address, 8)
                          : "breakpoint disabled at " + hex(row.address, 8);
            return true;
        }
        if (row.row.contains(x, y)) {
            for (const debug::Line& line : session_.info().lines) {
                if (line.address != row.address) {
                    continue;
                }
                select_source(line.source);
                source_scroll_ = std::max(0, line.line - 3);
                source_follow_ = false;
                status_ = "breakpoint source: " + source_view_path_ +
                          ":" + std::to_string(line.line);
                return true;
            }
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
        for (const AsmRow& row : asm_rows_) {
            if (y >= row.row.y && y < row.row.y + row.row.height) {
                toggle_instruction_breakpoint(row.address);
                return true;
            }
        }
        return true;
    }
    return false;
}

bool DebuggerUI::wheel(int x, int y, int width, int height, int delta) {
    if (source_menu_open_ && source_menu_rect_.contains(x, y)) {
        int count = 0;
        for (const debug::Source& source : session_.info().sources) {
            if (!source.path.empty()) {
                ++count;
            }
        }
        const int visible = std::min(14, count);
        source_menu_scroll_ = std::clamp(
            source_menu_scroll_ + (delta > 0 ? -1 : 1), 0,
            std::max(0, count - visible));
        return true;
    }
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
    if (areas.breakpoints.contains(x, y)) {
        breakpoints_scroll_ =
            std::max(0, breakpoints_scroll_ + direction * 1);
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

bool DebuggerUI::key(int key_value, bool shift, bool control) {
    if (screen_focus_ && key_value == kr32::platform::key::Escape) {
        screen_focus_ = false;
        return true;
    }
    if (settings_open_) {
        if (key_value == kr32::platform::key::Escape) {
            close_settings(false);
            return true;
        }
        if (key_value == kr32::platform::key::Enter) {
            apply_settings();
            return true;
        }
        if (settings_args_focused_) {
            if (key_value == kr32::platform::key::Backspace) {
                if (!settings_arguments_.empty()) {
                    settings_arguments_.pop_back();
                }
                settings_error_.clear();
                return true;
            }
            if (key_value >= 32 && key_value <= 126) {
                settings_arguments_.push_back(static_cast<char>(key_value));
                settings_error_.clear();
                return true;
            }
        }
        if (settings_run_focused_) {
            if (key_value == kr32::platform::key::Backspace) {
                if (!settings_run_limit_text_.empty()) {
                    settings_run_limit_text_.pop_back();
                }
                settings_error_.clear();
                return true;
            }
            if (key_value >= '0' && key_value <= '9') {
                settings_run_limit_text_.push_back(static_cast<char>(key_value));
                settings_error_.clear();
                return true;
            }
        }
        return true;
    }
    if (source_menu_open_ &&
        key_value == kr32::platform::key::Escape) {
        source_menu_open_ = false;
        return true;
    }
    if (console_hover_ && os_mode_ && !menu_.open && editing_tile_ < 0 &&
        editing_fpr_ < 0 && editing_register_ < 0 && !memory_address_editing_) {
        bool pushed = false;
        if (control && (key_value == 'v' || key_value == 'V')) {
            std::string text = kr32::platform::clipboard_text();
            std::string normalized;
            for (std::size_t index = 0; index < text.size(); ++index) {
                if (text[index] == '\r') {
                    if (index + 1 < text.size() && text[index + 1] == '\n') {
                        ++index;
                    }
                    normalized.push_back('\n');
                } else {
                    normalized.push_back(text[index]);
                }
            }
            (void)devices_.serial().push_input(normalized);
            pushed = !normalized.empty();
        } else if (control && key_value >= 'a' && key_value <= 'z') {
            (void)devices_.serial().push_input(
                static_cast<std::uint8_t>(key_value - 'a' + 1));
            pushed = true;
        } else if (key_value == kr32::platform::key::Enter) {
            (void)devices_.serial().push_input(static_cast<std::uint8_t>('\n'));
            pushed = true;
        } else if (key_value == kr32::platform::key::Backspace) {
            (void)devices_.serial().push_input(static_cast<std::uint8_t>(0x7F));
            pushed = true;
        } else if (key_value >= 32 && key_value <= 126) {
            char character = static_cast<char>(key_value);
            if (shift && character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
            (void)devices_.serial().push_input(static_cast<std::uint8_t>(character));
            pushed = true;
        }
        if (pushed) {
            if (!continuous_run_) run();
            return true;
        }
    }
    if (editing_tile_ >= 0) {
        if (key_value == kr32::platform::key::Escape) {
            finish_tile_edit(false);
            return true;
        }
        if (key_value == kr32::platform::key::Enter) {
            finish_tile_edit(true);
            return true;
        }
        if (key_value == kr32::platform::key::Backspace) {
            if (tile_edit_fresh_) {
                tile_edit_.clear();
                tile_edit_fresh_ = false;
            } else if (!tile_edit_.empty()) {
                tile_edit_.pop_back();
            }
            return true;
        }
        const bool allowed =
            (key_value >= '0' && key_value <= '9') || key_value == '.' ||
            key_value == '+' || key_value == '-' || key_value == 'e' ||
            key_value == 'E';
        if (allowed && tile_edit_.size() < 40) {
            if (tile_edit_fresh_) {
                tile_edit_.clear();
                tile_edit_fresh_ = false;
            }
            tile_edit_.push_back(static_cast<char>(key_value));
        }
        return true;
    }
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
    if (key_value == kr32::platform::key::F6) {
        if (continuous_run_) {
            stop_continuous(false, core::StepResult{});
            status_ = "paused at " + hex(session_.machine().pc(), 8);
        }
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
