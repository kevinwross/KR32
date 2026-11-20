#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/link/linker.h"
#include "kr32/os/embedded_os.h"
#include "kr32/platform/window.h"
#include "kr32/ui/canvas.h"
#include "kr32/ui/debugger_ui.h"
#include "kr32/ui/help.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace kr32;

namespace {

std::string build_ui_test_image() {
    const cc::Result compiled = cc::compile("int main(void) { return 0; }\n");
    REQUIRE(compiled.ok);
    container::Image user;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(assembler::assemble_object(compiled.assembly, user, assembly_error),
                    assembly_error.message);
    container::Image runtime;
    REQUIRE_MESSAGE(assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error),
                    assembly_error.message);
    container::Image image;
    linker::Map map;
    linker::Error link_error;
    linker::Options options;
    options.stack_top = 0x0011'0000;
    options.stack_limit = 0x0001'0000;
    REQUIRE_MESSAGE(linker::link({user, runtime}, options, image, map, link_error),
                    link_error.message);
    std::vector<std::uint8_t> bytes;
    container::Error container_error;
    REQUIRE_MESSAGE(container::write(image, bytes, container_error), container_error.message);
    const std::string path = "kr32-ui-run.k32";
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::string build_ui_two_source_image() {
    const std::string main_source =
        "int helper(void);\n"
        "int main(void) { return helper(); }\n";
    const std::string helper_source =
        "int helper(void) { return 0; }\n";
    {
        std::ofstream main_file("ui-main.c", std::ios::binary);
        main_file << main_source;
        std::ofstream helper_file("ui-helper.c", std::ios::binary);
        helper_file << helper_source;
    }
    const cc::Result main_compiled =
        cc::compile(main_source, "ui-main.c");
    const cc::Result helper_compiled =
        cc::compile(helper_source, "ui-helper.c");
    REQUIRE(main_compiled.ok);
    REQUIRE(helper_compiled.ok);
    container::Image main_image;
    container::Image helper_image;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(assembler::assemble_object(main_compiled.assembly,
                                               main_image, assembly_error),
                    assembly_error.message);
    REQUIRE_MESSAGE(assembler::assemble_object(helper_compiled.assembly,
                                               helper_image, assembly_error),
                    assembly_error.message);
    container::Image runtime;
    REQUIRE_MESSAGE(
        assembler::assemble_object(cc::runtime_assembly(), runtime,
                                   assembly_error),
        assembly_error.message);
    container::Image image;
    linker::Map map;
    linker::Error link_error;
    linker::Options options;
    options.stack_top = 0x0011'0000;
    options.stack_limit = 0x0001'0000;
    REQUIRE_MESSAGE(linker::link({main_image, helper_image, runtime},
                                 options, image, map, link_error),
                    link_error.message);
    std::vector<std::uint8_t> bytes;
    container::Error container_error;
    REQUIRE_MESSAGE(container::write(image, bytes, container_error),
                    container_error.message);
    const std::string path = "kr32-ui-two-sources.k32";
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("the portable canvas renders deterministic frames") {
    ui::Canvas first(320, 200);
    first.clear(ui::Color{10, 20, 30, 255});
    first.fill(ui::Rect{4, 4, 100, 20}, ui::Color{40, 60, 80, 255});
    first.frame(ui::Rect{4, 4, 100, 20}, ui::Color{80, 120, 160, 255});
    first.text(10, 10, "KR32", ui::Color{220, 230, 240, 255});
    first.line(0, 0, 319, 199, ui::Color{90, 100, 110, 255});

    ui::Canvas second(320, 200);
    second.clear(ui::Color{10, 20, 30, 255});
    second.fill(ui::Rect{4, 4, 100, 20}, ui::Color{40, 60, 80, 255});
    second.frame(ui::Rect{4, 4, 100, 20}, ui::Color{80, 120, 160, 255});
    second.text(10, 10, "KR32", ui::Color{220, 230, 240, 255});
    second.line(0, 0, 319, 199, ui::Color{90, 100, 110, 255});

    CHECK(first.hash() == second.hash());
    CHECK(first.hash() != 0);
    CHECK(first.width() == 320);
    CHECK(first.height() == 200);
    CHECK(ui::Canvas::text_width("KR32") > 0);
    CHECK(ui::Canvas::text_height(ui::FontRole::Mono) > 0);
}

TEST_CASE("the portable canvas clips drawing to its bounds") {
    ui::Canvas canvas(16, 16);
    canvas.clear(ui::Color{0, 0, 0, 255});
    canvas.fill(ui::Rect{-10, -10, 100, 100}, ui::Color{255, 255, 255, 255});
    CHECK(canvas.pixel_at(0, 0) == 0xFFFFFFFFU);
    canvas.pixel(-1, -1, ui::Color{255, 0, 0, 255});
    canvas.pixel(16, 16, ui::Color{255, 0, 0, 255});
    CHECK(canvas.pixel_at(0, 0) == 0xFFFFFFFFU);
}

TEST_CASE("error popup is opaque and white-on-dark") {
    ui::DebuggerUI debugger;
    debugger.show_error(
        "C:\\Users\\kevin\\source\\repos\\kr16\\tests\\kevin\\C-for-U.c:20:1: "
        "expected an expression");
    ui::Canvas canvas(800, 600);
    debugger.render(canvas);
    CHECK(canvas.pixel_at(400, 300) == 0xFF1C080CU);
    int white = 0;
    for (int y = 270; y < 330; ++y) {
        for (int x = 50; x < 750; ++x) {
            const std::uint32_t value = canvas.pixel_at(x, y);
            if (((value >> 16) & 0xFFU) >= 220 && ((value >> 8) & 0xFFU) >= 220 &&
                (value & 0xFFU) >= 220) {
                ++white;
            }
        }
    }
    CHECK(white > 0);
}

TEST_CASE("the settings dialog renders and closes without a loaded image") {
    ui::DebuggerUI debugger;
    ui::Canvas baseline(800, 600);
    debugger.render(baseline);
    const std::uint64_t baseline_hash = baseline.hash();

    CHECK(debugger.click(800 - 19, 15, 800, 600));
    ui::Canvas settings(800, 600);
    debugger.render(settings);
    CHECK(settings.hash() != baseline_hash);

    // The modal is centered in 800x600 with a 560x440 body; click its X.
    CHECK(debugger.click(120 + 560 - 20, 80 + 18, 800, 600));
    ui::Canvas closed(800, 600);
    debugger.render(closed);
    CHECK(closed.hash() == baseline_hash);
}

TEST_CASE("breakpoint panel disables and survives reset") {
    const std::string path = build_ui_test_image();
    ui::DebuggerUI debugger;
    std::string error;
    REQUIRE_MESSAGE(debugger.load(path, error), error.c_str());
    debugger.toggle_breakpoint_at_current_line();
    REQUIRE(debugger.breakpoint_count() == 1);

    ui::Canvas enabled(1000, 700);
    debugger.render(enabled);
    CHECK(enabled.pixel_at(350, 520) == 0xFFFF6868U);

    CHECK(debugger.click(350, 520, 1000, 700));
    ui::Canvas disabled(1000, 700);
    debugger.render(disabled);
    CHECK(disabled.pixel_at(350, 520) == 0xFFE2BE40U);
    CHECK(debugger.breakpoint_count() == 1);

    CHECK(debugger.click(320, 15, 1000, 700));
    ui::Canvas restarted(1000, 700);
    debugger.render(restarted);
    CHECK(debugger.breakpoint_count() == 1);
    CHECK(restarted.pixel_at(350, 520) == 0xFFE2BE40U);

    CHECK(debugger.context_menu(400, 520, 1000, 700));
    CHECK(debugger.click(450, 530, 1000, 700));
    CHECK(debugger.breakpoint_count() == 0);

    debugger.toggle_breakpoint_at_current_line();
    CHECK(debugger.breakpoint_count() == 1);
    ui::Canvas reenabled(1000, 700);
    debugger.render(reenabled);
    CHECK(reenabled.pixel_at(350, 520) == 0xFFFF6868U);
    std::filesystem::remove(path);
}

TEST_CASE("a C source compiles through the window and steps") {
    const std::string source_path = "kr32-ui-compiled.c";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "int main(void) {\n"
                  "    volatile int total = 42;\n"
                  "    return total;\n"
                  "}\n";
    }
    ui::DebuggerUI debugger;
    std::string error;
    REQUIRE_MESSAGE(debugger.load(source_path, error), error.c_str());
    const debug::Session& session = debugger.session();
    const debug::Line* line = session.line_at(session.machine().pc());
    REQUIRE(line != nullptr);
    CHECK(session.call_stack().size() >= 1);
#if defined(KR32_TOOLCHAIN_KRC)
    // 3.3's acceptance: the krc-built image stops at main's first statement and
    // the local it computes reads back. The built-in optimizer has its own
    // register model and folds this slot away, so only the krc branch asserts it.
    CHECK(line->line == 2);
    debugger.step_line_over();
    std::uint32_t total = 0;
    REQUIRE_MESSAGE(session.read_variable("total", total, error), error.c_str());
    CHECK(total == 42);
#else
    const std::uint32_t before = session.machine().pc();
    debugger.step_line_over();
    CHECK(session.machine().pc() != before);
#endif
    std::filesystem::remove(source_path);
}

TEST_CASE("a C++ source compiles through the window and keeps a local") {
    const std::string source_path = "kr32-ui-compiled.cpp";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "class Point {\n"
                  "public:\n"
                  "    int x;\n"
                  "    int y;\n"
                  "    int sum() { return x + y; }\n"
                  "};\n"
                  "\n"
                  "int main() {\n"
                  "    Point point;\n"
                  "    point.x = 20;\n"
                  "    point.y = 22;\n"
                  "    int total = point.sum();\n"
                  "    return total;\n"
                  "}\n";
    }
    ui::DebuggerUI debugger;
    std::string error;
    REQUIRE_MESSAGE(debugger.load(source_path, error), error.c_str());
    const debug::Session& session = debugger.session();
    std::uint32_t total = 0;
    bool reached = false;
    for (int step = 0; step < 20 && !reached; ++step) {
        debugger.step_line_over();
        reached = session.read_variable("total", total, error) && total == 42;
    }
    CHECK_MESSAGE(reached, "total never read 42; error: " << error);
    std::filesystem::remove(source_path);
}

TEST_CASE("all five guest headers compile through the window") {
    const std::string source_path = "kr32-ui-guest-headers.c";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "#include <kr32/fb.h>\n"
                  "#include <kr32/display.h>\n"
                  "#include <kr32/gfx.h>\n"
                  "#include <kr32/gfx.h>\n"
                  "#include <kr32/con.h>\n"
                  "#include <kr32/input.h>\n"
                  "#include <kr32/sys.h>\n"
                  "int main(void) {\n"
                  "    fb_info info = {0};\n"
                  "    display_surface display = {0};\n"
                  "    gfx_surface surface = {0};\n"
                  "    kr32_event event = {0};\n"
                  "    return (int)(info.width + display.offset + surface.offset +\n"
                  "                 event.ascii + KR32_STDOUT);\n"
                  "}\n";
    }
    ui::DebuggerUI debugger;
    std::string error;
    CHECK_MESSAGE(debugger.load(source_path, error), error);
    std::filesystem::remove(source_path);
}

TEST_CASE("a rejected source reports its file, line and column") {
    const std::string source_path = "kr32-ui-rejected.c";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "int main(void) {\n"
                  "    return missing;\n"
                  "}\n";
    }
    ui::DebuggerUI debugger;
    std::string error;
    CHECK_FALSE(debugger.load(source_path, error));
    CHECK(error.rfind(source_path + ":", 0) == 0);
    CHECK(error.find(":2:") != std::string::npos);
    std::filesystem::remove(source_path);
}

TEST_CASE("the window writes an assembler listing when the setting asks") {
    const std::filesystem::path config =
        std::filesystem::absolute("kr32-ui-config");
    std::filesystem::create_directories(config / "KR32");
    {
        std::ofstream settings(config / "KR32" / "settings.ini", std::ios::binary);
        settings << "listing=1\n";
    }
    const std::string previous_value = [] {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&value, &size, "APPDATA") != 0 || value == nullptr) {
            return std::string();
        }
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv("APPDATA");
        return value != nullptr ? std::string(value) : std::string();
#endif
    }();
#if defined(_WIN32)
    REQUIRE(_putenv_s("APPDATA", config.string().c_str()) == 0);
#else
    REQUIRE(setenv("APPDATA", config.string().c_str(), 1) == 0);
#endif
    const std::string source_path = "kr32-ui-listing.c";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "int main(void) { return 42; }\n";
    }
    ui::DebuggerUI debugger;
    std::string error;
    const bool loaded = debugger.load(source_path, error);
#if defined(_WIN32)
    _putenv_s("APPDATA", previous_value.c_str());
#else
    if (previous_value.empty()) {
        unsetenv("APPDATA");
    } else {
        setenv("APPDATA", previous_value.c_str(), 1);
    }
#endif
    REQUIRE_MESSAGE(loaded, error.c_str());
    const std::string listing_path = source_path + ".lst";
    REQUIRE(std::filesystem::exists(listing_path));
    std::ifstream listing(listing_path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(listing)),
                               std::istreambuf_iterator<char>());
    CHECK(contents.find("main:") != std::string::npos);
#if defined(KR32_TOOLCHAIN_KRC)
    // The krc branch renders the assembler-listing columns from the facade rows.
    CHECK(contents.find(": ") != std::string::npos);
#endif
    listing.close();
    std::filesystem::remove(listing_path);
    std::filesystem::remove(source_path);
    std::filesystem::remove_all(config);
}

TEST_CASE("source filename opens a source picker and selection follows steps") {
    const std::string path = build_ui_two_source_image();
    ui::DebuggerUI debugger;
    std::string error;
    REQUIRE_MESSAGE(debugger.load(path, error), error.c_str());
    ui::Canvas baseline(1000, 700);
    debugger.render(baseline);
    CHECK(!debugger.source_path().empty());

    const int filename_x =
        6 + ui::Canvas::text_width("SOURCE  ", ui::FontRole::UiBold) + 10;
    // The tab strip occupies the first 26 pixels of the old source region.
    CHECK(debugger.click(filename_x, 64, 1000, 700));
    ui::Canvas menu(1000, 700);
    debugger.render(menu);
    CHECK(menu.hash() != baseline.hash());

    // The first menu row starts below the source header.
    CHECK(debugger.click(filename_x, 88, 1000, 700));
    CHECK(debugger.status().rfind("source:", 0) == 0);
    CHECK(!debugger.source_path().empty());

    debugger.step_line_over();
    CHECK(debugger.source_path() == "ui-main.c");
    std::filesystem::remove(path);
    std::filesystem::remove("ui-main.c");
    std::filesystem::remove("ui-helper.c");
}

TEST_CASE("debugger variable values format floats and scalar types") {
    CHECK(ui::format_scalar_value("float", 0x40000000U, true) == "+2.0");
    CHECK(ui::format_scalar_value("float", 0xC0000000U, true) == "-2.0");
    CHECK(ui::format_scalar_value("float", 0x3FC00000U, true) == "+1.5");
    CHECK(ui::format_scalar_value("float", 0x7FC00000U, true) == "nan");
    CHECK(ui::format_scalar_value("float", 0, false) == "?");
    CHECK(ui::format_scalar_value("bool", 0, true) == "false");
    CHECK(ui::format_scalar_value("bool", 1, true) == "true");
    CHECK(ui::format_scalar_value("char", 'A', true) == "'A'");
    CHECK(ui::format_scalar_value("int", 42, true) == "0x0000002A");
}

TEST_CASE("scrollbar dragging follows the mouse one-to-one") {
    CHECK(ui::scroll_offset_for_drag(10, 100, 100, 100, 50) == 10);
    CHECK(ui::scroll_offset_for_drag(10, 100, 120, 100, 50) == 50);
    CHECK(ui::scroll_offset_for_drag(10, 100, 80, 100, 50) == 0);
    CHECK(ui::scroll_offset_for_drag(10, 100, 200, 100, 50) == 100);
    CHECK(ui::scroll_offset_for_drag(10, 100, 120, 24, 0) == 10);
}

TEST_CASE("console output follows only when the view is at the bottom") {
    CHECK(ui::console_scroll_after_output(90, 100, 105, 10) == 95);
    CHECK(ui::console_scroll_after_output(50, 100, 105, 10) == 50);
    CHECK(ui::console_scroll_after_output(0, 5, 6, 10) == 0);
    CHECK(ui::console_scroll_after_output(2, 3, 5, 1) == 4);
    CHECK(ui::console_scroll_after_output(90, 100, 5, 10) == 0);
    CHECK(ui::console_scroll_after_output(50, 100, 5, 10) == 0);
}

TEST_CASE("array types display dimensions in declaration order") {
    // The front ends record the innermost dimension first.
    CHECK(ui::array_dimensions_display("int[3][2]") == "[2][3]");
    CHECK(ui::array_dimensions_display("char[37]") == "[37]");
    CHECK(ui::array_dimensions_display("int[7][100][37]") == "[37][100][7]");
    CHECK(ui::array_dimensions_display("int") == "");
}

TEST_CASE("instruction and addressing help describe KR32 operands") {
    CHECK(ui::instruction_help("PUSH").find("SP") != std::string::npos);
    CHECK(ui::instruction_help("LOAD8S").find("sign") != std::string::npos);
    CHECK(ui::instruction_help("FADD.S").find("Floating") != std::string::npos);
    CHECK(ui::instruction_help(".word").find("data word") != std::string::npos);
    CHECK(ui::addressing_help("LOAD R4, [R5 + 8]", "LOAD").find("base register") !=
          std::string::npos);
    CHECK(ui::addressing_help("MOV R1, #42", "MOV").find("immediate") !=
          std::string::npos);
    CHECK(ui::addressing_help("JMP 0x00010000", "JMP").find("branch target") !=
          std::string::npos);
}

TEST_CASE("running uses the responsive sliced path and clears the RUN highlight") {
    const std::string path = build_ui_test_image();
    ui::DebuggerUI debugger;
    std::string error;
    REQUIRE_MESSAGE(debugger.load(path, error), error.c_str());

    ui::Canvas probe(1, 1);
    probe.fill(ui::Rect{0, 0, 1, 1}, ui::Color{32, 122, 64, 255});
    const std::uint32_t green = probe.pixel_at(0, 0);
    ui::Canvas title_probe(1, 1);
    title_probe.fill(ui::Rect{0, 0, 1, 1}, ui::Color{86, 210, 126, 255});
    const std::uint32_t title_green = title_probe.pixel_at(0, 0);

    ui::Canvas before(800, 600);
    debugger.render(before);
    CHECK(before.pixel_at(84, 6) != green);

    debugger.hover(700, 430, 800, 600);
    ui::Canvas hovered(800, 600);
    debugger.render(hovered);
    int green_title_pixels = 0;
    for (int y = 389; y < 409; ++y) {
        for (int x = 630; x < 795; ++x) {
            if (hovered.pixel_at(x, y) == title_green) {
                ++green_title_pixels;
            }
        }
    }
    CHECK(green_title_pixels > 0);

    debugger.run();
    ui::Canvas running(800, 600);
    debugger.render(running);
    CHECK(running.pixel_at(84, 6) == green);

    CHECK(debugger.key(kr32::platform::key::F6, false, false));
    ui::Canvas broken(800, 600);
    debugger.render(broken);
    CHECK(broken.pixel_at(84, 6) != green);

    debugger.run();
    for (int index = 0; index < 1000; ++index) {
        debugger.update();
    }
    ui::Canvas stopped(800, 600);
    debugger.render(stopped);
    CHECK(stopped.pixel_at(84, 6) != green);
    std::filesystem::remove(path);
}

#if defined(KR32_HAS_EMBEDDED_OS) && KR32_HAS_EMBEDDED_OS
namespace {

// Loads the embedded OS so the debugger is in OS mode with the SCREEN tab
// showing; the 1440x900 layout puts that pane in x 0..604, y 52..579.
void load_embedded_os(ui::DebuggerUI& debugger) {
    const os::EmbeddedOs& images = os::embedded_os();
    std::vector<std::uint8_t> kernel(images.kernel.begin(), images.kernel.end());
    std::vector<std::uint8_t> bios(images.bios.begin(), images.bios.end());
    std::string error;
    REQUIRE_MESSAGE(debugger.load_os_memory(kernel, bios, "<embedded>", "<embedded>",
                                            error),
                    error.c_str());
}

} // namespace

TEST_CASE("typing over the screen sends characters to the guest console") {
    ui::DebuggerUI debugger;
    load_embedded_os(debugger);

    debugger.hover(100, 200, 1440, 900);
    CHECK(debugger.key('r', false, false));
    CHECK(debugger.session().machine().devices()->serial().available() == 1);
    CHECK(debugger.status().rfind("restarted", 0) != 0);
}

TEST_CASE("function keys still reach the debugger while the pointer is over the screen") {
    ui::DebuggerUI debugger;
    load_embedded_os(debugger);

    debugger.hover(100, 200, 1440, 900);
    CHECK(debugger.key(kr32::platform::key::F5, false, false));
    CHECK(debugger.status().find("running") != std::string::npos);
    CHECK(debugger.session().machine().devices()->serial().available() == 0);
}

TEST_CASE("typing away from the screen keeps the debugger shortcuts") {
    ui::DebuggerUI debugger;
    load_embedded_os(debugger);

    // x 604 and beyond is the disassembly pane, which is not a typing surface.
    debugger.hover(700, 200, 1440, 900);
    CHECK(debugger.key('r', false, false));
    CHECK(debugger.status().rfind("restarted at", 0) == 0);
    CHECK(debugger.session().machine().devices()->serial().available() == 0);
}
#endif

TEST_CASE("platform key events pack into the keyboard word") {
    kr32::platform::Event event;
    event.kind = kr32::platform::Event::Kind::KeyDown;
    event.ascii = 'a';
    event.modifiers = 0x1;
    event.scancode = 0x41;
    CHECK(kr32::platform::event_word(event) == 0x0041'0161U);

    event.kind = kr32::platform::Event::Kind::KeyUp;
    CHECK(kr32::platform::event_word(event) == 0x0141'0161U);

    kr32::platform::Event special;
    special.kind = kr32::platform::Event::Kind::KeyDown;
    special.scancode = 0x8A; // Up
    CHECK(kr32::platform::event_word(special) == 0x008A'0000U);
}
