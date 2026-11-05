#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/link/linker.h"
#include "kr32/platform/window.h"
#include "kr32/ui/canvas.h"
#include "kr32/ui/debugger_ui.h"
#include "kr32/ui/help.h"

#include <filesystem>
#include <fstream>
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

TEST_CASE("console output follows only when the view is at the bottom") {
    CHECK(ui::console_scroll_after_output(90, 100, 105, 10) == 95);
    CHECK(ui::console_scroll_after_output(50, 100, 105, 10) == 50);
    CHECK(ui::console_scroll_after_output(0, 5, 6, 10) == 0);
    CHECK(ui::console_scroll_after_output(2, 3, 5, 1) == 4);
    CHECK(ui::console_scroll_after_output(90, 100, 5, 10) == 0);
    CHECK(ui::console_scroll_after_output(50, 100, 5, 10) == 0);
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

    debugger.hover(600, 430, 800, 600);
    ui::Canvas hovered(800, 600);
    debugger.render(hovered);
    int green_title_pixels = 0;
    for (int y = 389; y < 409; ++y) {
        for (int x = 561; x < 680; ++x) {
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
