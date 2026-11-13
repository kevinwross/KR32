#include "doctest.h"

#include "kr32/core/devices.h"

namespace {

using namespace kr32::core;

std::uint32_t read_pixel(FramebufferDevice& framebuffer, int x, int y) {
    const std::uint32_t stride = framebuffer.stride();
    const std::size_t offset = framebuffer.back_offset() +
                               static_cast<std::size_t>(y) * stride +
                               static_cast<std::size_t>(x) * 4U;
    const std::vector<std::uint8_t>& vram = framebuffer.vram();
    if (offset + 4U > vram.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(vram[offset]) |
           (static_cast<std::uint32_t>(vram[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(vram[offset + 2]) << 16);
}

void set_mode(DeviceBus& bus, std::uint32_t width, std::uint32_t height) {
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Width, width);
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Height, height);
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Format, 1);
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Front, 0);
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Back, 0);
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Control, 1);
    (void)bus.write32(kFramebufferControlBase + FramebufferDevice::Flip, 1);
    (void)bus.write32(kTerminalBase + TerminalDevice::Command,
                      TerminalDevice::CommandReset);
}

} // namespace

TEST_CASE("terminal reports its id and an 80x30 grid at 800x600") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    std::uint32_t id = 0;
    REQUIRE(bus.read32(kTerminalBase + TerminalDevice::Id, id));
    CHECK(id == TerminalDevice::IdValue);
    std::uint32_t status = 0;
    REQUIRE(bus.read32(kTerminalBase + TerminalDevice::Status, status));
    CHECK((status & TerminalDevice::StatusReady) != 0);
    CHECK(((status >> 8) & 0xFFU) == 80U);
    CHECK(((status >> 16) & 0xFFU) == 30U);
    CHECK(bus.terminal().columns() == 80);
    CHECK(bus.terminal().rows() == 30);
}

TEST_CASE("terminal writes characters, wraps, and moves the cursor") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    for (char character : std::string("A\nB")) {
        (void)bus.write32(kTerminalBase + TerminalDevice::Data,
                          static_cast<std::uint32_t>(character));
    }
    CHECK(bus.terminal().cell(0, 0) == 'A');
    CHECK(bus.terminal().cell(0, 1) == 'B');
    CHECK(bus.terminal().cursor_x() == 1);
    CHECK(bus.terminal().cursor_y() == 1);

    // Fill the rest of the row: the cursor wraps to the next row.
    for (int index = 1; index < 80; ++index) {
        (void)bus.write32(kTerminalBase + TerminalDevice::Data, 'x');
    }
    CHECK(bus.terminal().cursor_x() == 0);
    CHECK(bus.terminal().cursor_y() == 2);
}

TEST_CASE("terminal scrolls at the bottom and clears on command") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    for (int row = 0; row < 30; ++row) {
        (void)bus.write32(kTerminalBase + TerminalDevice::Data, '0' + row % 10);
        (void)bus.write32(kTerminalBase + TerminalDevice::Data, '\n');
    }
    // 30 writes plus the trailing newline scroll once: the first line is gone.
    CHECK(bus.terminal().cell(0, 0) == '1');
    CHECK(bus.terminal().cell(0, 28) == '9');
    CHECK(bus.terminal().cell(0, 29) == ' ');

    (void)bus.write32(kTerminalBase + TerminalDevice::Command,
                      TerminalDevice::CommandClear);
    for (int row = 0; row < 30; ++row) {
        CHECK(bus.terminal().cell(0, row) == ' ');
    }
    CHECK(bus.terminal().cursor_x() == 0);
    CHECK(bus.terminal().cursor_y() == 0);
}

TEST_CASE("terminal handles carriage return, backspace, tab, and form feed") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    for (char character : std::string("abc")) {
        (void)bus.write32(kTerminalBase + TerminalDevice::Data, character);
    }
    (void)bus.write32(kTerminalBase + TerminalDevice::Data, '\r');
    CHECK(bus.terminal().cursor_x() == 0);
    (void)bus.write32(kTerminalBase + TerminalDevice::Data, '\t');
    CHECK(bus.terminal().cursor_x() == 8);
    (void)bus.write32(kTerminalBase + TerminalDevice::Data, '\b');
    CHECK(bus.terminal().cursor_x() == 7);
    (void)bus.write32(kTerminalBase + TerminalDevice::Data, '\f');
    CHECK(bus.terminal().cell(0, 0) == ' ');
    CHECK(bus.terminal().cursor_x() == 0);
}

TEST_CASE("terminal captures the pen colour per cell and paints the glyph") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    // Foreground 2, background 0.
    (void)bus.write32(kTerminalBase + TerminalDevice::Color, 0x02);
    (void)bus.write32(kTerminalBase + TerminalDevice::Data, 'A');
    CHECK(bus.terminal().foreground() == 2);
    CHECK(bus.terminal().background() == 0);
    CHECK(bus.terminal().attribute(0, 0) == 0x02);

    bool saw_foreground = false;
    bool saw_background = false;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 10; ++x) {
            const std::uint32_t pixel = read_pixel(bus.framebuffer(), x, y);
            saw_foreground = saw_foreground || pixel == 0x0030A030U;
            saw_background = saw_background || pixel == 0x00101820U;
        }
    }
    CHECK(saw_foreground);
    CHECK(saw_background);
}

TEST_CASE("terminal hides without disturbing the grid and redraws on show") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    (void)bus.write32(kTerminalBase + TerminalDevice::Control,
                      TerminalDevice::ControlHidden);
    (void)bus.write32(kTerminalBase + TerminalDevice::Data, 'Z');
    CHECK(bus.terminal().cell(0, 0) == 'Z');
    bool saw_text = false;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 10; ++x) {
            saw_text = saw_text || read_pixel(bus.framebuffer(), x, y) == 0x00E6EDF3U;
        }
    }
    CHECK_FALSE(saw_text);

    (void)bus.write32(kTerminalBase + TerminalDevice::Control,
                      TerminalDevice::ControlCursor);
    CHECK(bus.terminal().hidden() == false);
}
