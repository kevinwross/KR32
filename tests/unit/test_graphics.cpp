#include "doctest.h"

#include "kr32/core/devices.h"

namespace {

using namespace kr32::core;

std::uint32_t pixel(FramebufferDevice& framebuffer, int x, int y) {
    const std::size_t offset = framebuffer.back_offset() +
                               static_cast<std::size_t>(y) * framebuffer.stride() +
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
}

void argument(DeviceBus& bus, std::uint32_t index, std::uint32_t value) {
    (void)bus.write32(kGraphicsBase + GraphicsDevice::ArgumentBase + index * 4U,
                      value);
}

void command(DeviceBus& bus, GraphicsDevice::Op op) {
    (void)bus.write32(kGraphicsBase + GraphicsDevice::CommandRegister,
                      static_cast<std::uint32_t>(op));
    (void)bus.write32(kGraphicsBase + GraphicsDevice::Doorbell, 1);
}

std::uint32_t status(DeviceBus& bus) {
    std::uint32_t value = 0;
    (void)bus.read32(kGraphicsBase + GraphicsDevice::StatusRegister, value);
    return value;
}

} // namespace

TEST_CASE("graphics device reports its id") {
    DeviceBus bus;
    std::uint32_t id = 0;
    REQUIRE(bus.read32(kGraphicsBase + GraphicsDevice::IdRegister, id));
    CHECK(id == GraphicsDevice::IdValue);
}

TEST_CASE("fill rect honours the clip and paints the requested colour") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    argument(bus, 0, 10);  // x
    argument(bus, 1, 10);  // y
    argument(bus, 2, 20);  // width
    argument(bus, 3, 20);  // height
    command(bus, GraphicsDevice::Op::SetClip);
    argument(bus, 0, 0);
    argument(bus, 1, 0);
    argument(bus, 2, 100);
    argument(bus, 3, 100);
    argument(bus, 4, 0x00FF'0000U);
    command(bus, GraphicsDevice::Op::FillRect);
    CHECK(pixel(bus.framebuffer(), 10, 10) == 0x00FF'0000U);
    CHECK(pixel(bus.framebuffer(), 29, 29) == 0x00FF'0000U);
    CHECK(pixel(bus.framebuffer(), 5, 5) == 0);
    CHECK(pixel(bus.framebuffer(), 30, 30) == 0);
}

TEST_CASE("copy rect duplicates a region") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    argument(bus, 0, 0);
    argument(bus, 1, 0);
    argument(bus, 2, 10);
    argument(bus, 3, 10);
    argument(bus, 4, 0x0000'FF00U);
    command(bus, GraphicsDevice::Op::FillRect);

    argument(bus, 0, 0);   // source offset in VRAM
    argument(bus, 1, 40);  // destination x
    argument(bus, 2, 40);  // destination y
    argument(bus, 3, 10);  // width
    argument(bus, 4, 10);  // height
    command(bus, GraphicsDevice::Op::CopyRect);
    CHECK(pixel(bus.framebuffer(), 45, 45) == 0x0000'FF00U);
    CHECK(pixel(bus.framebuffer(), 0, 0) == 0x0000'FF00U);
}

TEST_CASE("draw line includes both endpoints") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    argument(bus, 0, 0);
    argument(bus, 1, 0);
    argument(bus, 2, 10);
    argument(bus, 3, 0);
    argument(bus, 4, 0x0000'00FFU);
    command(bus, GraphicsDevice::Op::DrawLine);
    CHECK(pixel(bus.framebuffer(), 0, 0) == 0x0000'00FFU);
    CHECK(pixel(bus.framebuffer(), 5, 0) == 0x0000'00FFU);
    CHECK(pixel(bus.framebuffer(), 10, 0) == 0x0000'00FFU);
    CHECK(pixel(bus.framebuffer(), 11, 0) == 0);
}

TEST_CASE("draw text reads characters from vram") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    constexpr std::uint32_t kTextAddress = 0x0010'0000U;
    bus.framebuffer().vram()[kTextAddress] = 'A';
    argument(bus, 0, 0);                 // x
    argument(bus, 1, 0);                 // y
    argument(bus, 2, kTextAddress);      // text pointer
    argument(bus, 3, 1);                 // length
    argument(bus, 4, 0x00FF'FFFFU);      // foreground
    argument(bus, 5, 0x0000'0000U);      // background
    command(bus, GraphicsDevice::Op::DrawText);

    bool saw_foreground = false;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 10; ++x) {
            saw_foreground = saw_foreground ||
                             pixel(bus.framebuffer(), x, y) == 0x00FF'FFFFU;
        }
    }
    CHECK(saw_foreground);
}

TEST_CASE("flip commits the frame and advances the counter") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    const std::uint32_t before = bus.framebuffer().frame_number();
    command(bus, GraphicsDevice::Op::Flip);
    CHECK(bus.framebuffer().frame_number() == before + 1);
    CHECK(bus.framebuffer().flip_error() == false);
}

TEST_CASE("an out-of-range text pointer sets the error bit") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    argument(bus, 0, 0);
    argument(bus, 1, 0);
    argument(bus, 2, 0x7FFF'FFF0U);  // beyond VRAM
    argument(bus, 3, 4);
    argument(bus, 4, 0x00FF'FFFFU);
    argument(bus, 5, 0);
    command(bus, GraphicsDevice::Op::DrawText);
    CHECK((status(bus) & GraphicsDevice::StatusError) != 0);

    (void)bus.write32(kGraphicsBase + GraphicsDevice::StatusRegister,
                      GraphicsDevice::StatusError);
    CHECK((status(bus) & GraphicsDevice::StatusError) == 0);
}

TEST_CASE("an unknown command sets the error bit") {
    DeviceBus bus;
    set_mode(bus, 800, 600);
    command(bus, static_cast<GraphicsDevice::Op>(99));
    CHECK((status(bus) & GraphicsDevice::StatusError) != 0);
}
