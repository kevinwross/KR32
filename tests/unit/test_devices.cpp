#include "doctest.h"

#include "kr32/asm/assembler.h"
#include "kr32/core/cluster.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace kr32;

TEST_CASE("framebuffer aperture stores pixels and exposes control registers") {
    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    core::DeviceBus devices;
    devices.attach_machine(machine);
    machine.attach_devices(&devices);

    constexpr std::uint32_t pixel_offset = 0x1234;
    REQUIRE(machine.write_physical32(core::kFramebufferBase + pixel_offset, 0x11223344U));
    std::uint32_t value = 0;
    REQUIRE(machine.read_physical32(core::kFramebufferBase + pixel_offset, value));
    CHECK(value == 0x11223344U);
    CHECK(devices.framebuffer().vram()[pixel_offset] == 0x44U);

    REQUIRE(machine.write_physical8(core::kFramebufferBase + pixel_offset + 1, 0xAAU));
    std::uint8_t byte = 0;
    REQUIRE(machine.read_physical8(core::kFramebufferBase + pixel_offset + 1, byte));
    CHECK(byte == 0xAAU);
    REQUIRE(machine.read_physical32(core::kFramebufferBase + pixel_offset, value));
    CHECK(value == 0x1122AA44U);

    REQUIRE(machine.write_physical32(core::kFramebufferControlBase + core::FramebufferDevice::Control,
                                     1));
    CHECK(devices.framebuffer().enabled());
    REQUIRE(machine.write_physical32(core::kFramebufferControlBase + core::FramebufferDevice::Width,
                                     800));
    REQUIRE(machine.read_physical32(core::kFramebufferControlBase + core::FramebufferDevice::Width,
                                    value));
    CHECK(value == 800U);
    REQUIRE(machine.read_physical32(core::kFramebufferControlBase + core::FramebufferDevice::Stride,
                                    value));
    CHECK(value == 3200U);
}

TEST_CASE("serial device exposes RX, TX, status, and peek") {
    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    core::DeviceBus devices;
    devices.attach_machine(machine);
    machine.attach_devices(&devices);

    devices.serial().push_input("hi");
    std::uint32_t value = 0;
    REQUIRE(machine.read_physical32(core::kSerialBase + core::SerialDevice::Status, value));
    CHECK((value & core::SerialDevice::RxReady) != 0);
    CHECK((value & core::SerialDevice::Overrun) == 0);
    REQUIRE(machine.read_physical32(core::kSerialBase + core::SerialDevice::Peek, value));
    CHECK(value == 'h');
    REQUIRE(machine.read_physical32(core::kSerialBase + core::SerialDevice::Data, value));
    CHECK(value == 'h');
    REQUIRE(machine.read_physical32(core::kSerialBase + core::SerialDevice::Peek, value));
    CHECK(value == 'i');

    REQUIRE(machine.write_physical32(core::kSerialBase + core::SerialDevice::Data, 'O'));
    REQUIRE(machine.write_physical32(core::kSerialBase + core::SerialDevice::Data, 'K'));
    CHECK(devices.serial().take_output() == "OK");
}

TEST_CASE("timer and console MMIO are accessible through the machine") {
    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    core::DeviceBus devices;
    devices.attach_machine(machine);
    machine.attach_devices(&devices);

    std::uint32_t raised = 0;
    devices.set_irq_handler([&](std::uint32_t line) { raised = line + 1; });

    REQUIRE(machine.write_physical32(core::kTimerBase + core::TimerDevice::Irq, 4));
    REQUIRE(machine.write_physical32(core::kTimerBase + core::TimerDevice::Reload, 3));
    REQUIRE(machine.write_physical32(core::kTimerBase + core::TimerDevice::Control, 1 | 4));

    std::uint32_t value = 0;
    REQUIRE(machine.read_physical32(core::kTimerBase + core::TimerDevice::Count, value));
    CHECK(value == 3);
    for (int tick = 0; tick < 3; ++tick) {
        devices.tick();
    }
    REQUIRE(machine.read_physical32(core::kTimerBase + core::TimerDevice::Status, value));
    CHECK((value & 1U) != 0);
    CHECK(raised == 5);

    REQUIRE(machine.write_physical32(core::kConsoleBase + core::ConsoleDevice::Data, 'O'));
    REQUIRE(machine.write_physical32(core::kConsoleBase + core::ConsoleDevice::Data, 'K'));
    CHECK(devices.console().output() == "OK");

    assembler::Options options;
    options.base = 0x0001'0000;
    assembler::Result code;
    assembler::Error error;
    REQUIRE_MESSAGE(assembler::assemble("NOP\nNOP\nHALT\n", options, code, error),
                    error.message.c_str());
    core::Machine irq_machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    core::DeviceBus irq_devices;
    irq_devices.attach_machine(irq_machine);
    irq_machine.attach_devices(&irq_devices);
    REQUIRE(irq_machine.load_image(options.base, code.bytes));
    irq_machine.set_pc(options.base);
    REQUIRE(irq_machine.write_physical32(core::kTimerBase + core::TimerDevice::Irq, 6));
    REQUIRE(irq_machine.write_physical32(core::kTimerBase + core::TimerDevice::Reload, 1));
    REQUIRE(irq_machine.write_physical32(core::kTimerBase + core::TimerDevice::Control, 1 | 4));
    (void)irq_machine.step();
    std::uint32_t irq = 0;
    REQUIRE(irq_machine.read_control_register(2, irq));
    CHECK((irq & (1U << 6)) != 0);
}

TEST_CASE("block DMA completes and routes its interrupt") {
    core::Cluster cluster(2, core::Machine::Options{1U * 1024U * 1024U});
    (void)cluster.devices().irq_router().write(3 * 4, 2);
    core::DeviceBus& devices = cluster.devices();
    constexpr std::uint32_t target = 0x0002'0000;

    REQUIRE(devices.write32(core::kBlockBase + core::BlockDevice::Address, target));
    REQUIRE(devices.write32(core::kBlockBase + core::BlockDevice::Count, 4));
    REQUIRE(devices.write32(core::kBlockBase + core::BlockDevice::Irq, 3));
    REQUIRE(devices.write32(core::kBlockBase + core::BlockDevice::Command, 1 | 4));

    std::uint32_t value = 0;
    REQUIRE(cluster.core(1).read_control_register(2, value));
    CHECK((value & (1U << 3)) != 0);
    for (std::uint32_t index = 0; index < 4; ++index) {
        std::uint8_t byte = 0;
        REQUIRE(cluster.core(0).read_physical8(target + index, byte));
        CHECK(byte == index);
    }
}

TEST_CASE("clusters share RAM and expose distinct cores") {
    core::Cluster cluster(2, core::Machine::Options{1U * 1024U * 1024U});
    CHECK(cluster.size() == 2);
    CHECK(cluster.core(0).core_id() == 0);
    CHECK(cluster.core(1).core_id() == 1);
    REQUIRE(cluster.core(0).write_physical32(0x0002'0000, 0xDEADBEEFU));
    std::uint32_t value = 0;
    REQUIRE(cluster.core(1).read_physical32(0x0002'0000, value));
    CHECK(value == 0xDEADBEEFU);

    cluster.core(1).raise_interrupt(5);
    std::uint32_t irq = 0;
    REQUIRE(cluster.core(1).read_control_register(2, irq));
    CHECK((irq & (1U << 5)) != 0);
}

TEST_CASE("cluster runs reproduce") {
    const std::string source =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    LL R1, [R0]\n"
        "    ADD R1, R1, #1\n"
        "    SC R1, [R0]\n"
        "    JNZ _start\n"
        "    HALT\n";
    assembler::Options options;
    options.base = 0x0001'0000;
    assembler::Result assembled;
    assembler::Error error;
    REQUIRE_MESSAGE(assembler::assemble(source, options, assembled, error), error.message.c_str());

    auto run_counter = [&]() {
        core::Cluster cluster(2, core::Machine::Options{1U * 1024U * 1024U});
        REQUIRE(cluster.core(0).load_image(options.base, assembled.bytes));
        for (std::size_t index = 0; index < cluster.size(); ++index) {
            cluster.core(index).set_pc(options.base);
            cluster.core(index).set_reg(0, 0x0002'0000);
        }
        for (int step = 0; step < 100; ++step) {
            (void)cluster.step();
            if (cluster.core(0).halted() && cluster.core(1).halted()) {
                break;
            }
        }
        std::uint32_t value = 0;
        REQUIRE(cluster.core(0).read_physical32(0x0002'0000, value));
        return value;
    };

    CHECK(run_counter() == 2);
    CHECK(run_counter() == 2);
}

TEST_CASE("a two-core LL/SC spinlock increments correctly") {
    const std::string source =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    LL R1, [R0]\n"
        "    ADD R1, R1, #1\n"
        "    SC R1, [R0]\n"
        "    JNZ _start\n"
        "    HALT\n";
    assembler::Options options;
    options.base = 0x0001'0000;
    assembler::Result assembled;
    assembler::Error error;
    REQUIRE_MESSAGE(assembler::assemble(source, options, assembled, error), error.message.c_str());

    core::Cluster cluster(2, core::Machine::Options{1U * 1024U * 1024U});
    REQUIRE(cluster.core(0).load_image(options.base, assembled.bytes));
    for (std::size_t index = 0; index < cluster.size(); ++index) {
        cluster.core(index).set_pc(options.base);
        cluster.core(index).set_reg(0, 0x0002'0000);
    }

    for (int step = 0; step < 100; ++step) {
        (void)cluster.step();
        if (cluster.core(0).halted() && cluster.core(1).halted()) {
            break;
        }
    }
    CHECK(cluster.core(0).halted());
    CHECK(cluster.core(1).halted());
    std::uint32_t value = 0;
    REQUIRE(cluster.core(0).read_physical32(0x0002'0000, value));
    CHECK(value == 2);
}

namespace {

struct FramebufferFixture {
    core::Machine machine;
    core::DeviceBus devices;

    FramebufferFixture()
        : machine(core::Machine::Options{1U * 1024U * 1024U, 0, true}) {
        devices.attach_machine(machine);
        machine.attach_devices(&devices);
    }

    void write_reg(std::uint32_t offset, std::uint32_t value) {
        REQUIRE(machine.write_physical32(core::kFramebufferControlBase + offset, value));
    }

    std::uint32_t read_reg(std::uint32_t offset) {
        std::uint32_t value = 0;
        REQUIRE(machine.read_physical32(core::kFramebufferControlBase + offset, value));
        return value;
    }

    core::FramebufferDevice& fb() { return devices.framebuffer(); }

    void set_mode(std::uint32_t width, std::uint32_t height) {
        write_reg(core::FramebufferDevice::Width, width);
        write_reg(core::FramebufferDevice::Height, height);
    }
};

} // namespace

TEST_CASE("framebuffer v2 reset state and ID word") {
    FramebufferFixture f;
    CHECK(f.read_reg(core::FramebufferDevice::Control) == 0U);
    CHECK(f.read_reg(core::FramebufferDevice::Width) == core::kFramebufferWidth);
    CHECK(f.read_reg(core::FramebufferDevice::Height) == core::kFramebufferHeight);
    CHECK(f.read_reg(core::FramebufferDevice::Stride) == core::kFramebufferStride);
    CHECK(f.read_reg(core::FramebufferDevice::Format) == 1U);
    CHECK(f.read_reg(core::FramebufferDevice::Front) == 0U);
    CHECK(f.read_reg(core::FramebufferDevice::Back) == core::kFramebufferSurfaceBytes);
    CHECK(f.read_reg(core::FramebufferDevice::Frame) == 0U);
    CHECK(f.read_reg(core::FramebufferDevice::Id) == core::FramebufferDevice::IdValue);
    CHECK(f.fb().frame().number == 0U);
    CHECK_FALSE(f.fb().frame().enabled);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusModeOk) != 0);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusEnabled) == 0);
}

TEST_CASE("framebuffer width write sets stride and rejects out of range") {
    FramebufferFixture f;
    f.write_reg(core::FramebufferDevice::Width, 800);
    CHECK(f.read_reg(core::FramebufferDevice::Width) == 800U);
    CHECK(f.read_reg(core::FramebufferDevice::Stride) == 3200U);

    f.write_reg(core::FramebufferDevice::Width, 0);
    CHECK(f.read_reg(core::FramebufferDevice::Width) == 800U);
    f.write_reg(core::FramebufferDevice::Width, 1921);
    CHECK(f.read_reg(core::FramebufferDevice::Width) == 800U);
}

TEST_CASE("framebuffer stride accepts padding and rejects invalid values") {
    FramebufferFixture f;
    f.write_reg(core::FramebufferDevice::Width, 800);
    f.write_reg(core::FramebufferDevice::Stride, 3328);
    CHECK(f.read_reg(core::FramebufferDevice::Stride) == 3328U);

    f.write_reg(core::FramebufferDevice::Stride, 3196);
    CHECK(f.read_reg(core::FramebufferDevice::Stride) == 3328U);
    f.write_reg(core::FramebufferDevice::Stride, 3201);
    CHECK(f.read_reg(core::FramebufferDevice::Stride) == 3328U);
    f.write_reg(core::FramebufferDevice::Stride, 7684);
    CHECK(f.read_reg(core::FramebufferDevice::Stride) == 3328U);
}

TEST_CASE("framebuffer format accepts only XRGB8888") {
    FramebufferFixture f;
    f.write_reg(core::FramebufferDevice::Format, 0);
    CHECK(f.read_reg(core::FramebufferDevice::Format) == 1U);
    f.write_reg(core::FramebufferDevice::Format, 2);
    CHECK(f.read_reg(core::FramebufferDevice::Format) == 1U);
}

TEST_CASE("flip commits the surface that was BACK and swaps") {
    FramebufferFixture f;
    f.set_mode(800, 600);
    f.write_reg(core::FramebufferDevice::Front, 0);
    f.write_reg(core::FramebufferDevice::Back, 0x1000);
    f.write_reg(core::FramebufferDevice::Control, 1);
    f.write_reg(core::FramebufferDevice::Flip, 1);

    CHECK(f.fb().frame().number == 1U);
    CHECK(f.fb().frame().offset == 0x1000U);
    CHECK(f.fb().frame().width == 800U);
    CHECK(f.fb().frame().height == 600U);
    CHECK(f.fb().frame().enabled);
    CHECK(f.read_reg(core::FramebufferDevice::Front) == 0x1000U);
    CHECK(f.read_reg(core::FramebufferDevice::Back) == 0U);
    CHECK(f.read_reg(core::FramebufferDevice::Flip) == 0U);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusEnabled) != 0);
}

TEST_CASE("flip bounds accept exactly 32 MiB and reject one byte more") {
    FramebufferFixture f;
    const std::uint32_t edge = core::kFramebufferSize - core::kFramebufferSurfaceBytes;
    f.write_reg(core::FramebufferDevice::Front, edge);
    f.write_reg(core::FramebufferDevice::Back, edge);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    CHECK(f.fb().frame().number == 1U);
    CHECK(f.fb().flip_error() == false);

    f.write_reg(core::FramebufferDevice::Front, edge + 4);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    CHECK(f.fb().frame().number == 1U);
    CHECK(f.fb().flip_error());
}

TEST_CASE("a rejected flip is sticky and W1C clears it") {
    FramebufferFixture f;
    f.write_reg(core::FramebufferDevice::Front, core::kFramebufferSize - 4);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    CHECK(f.fb().frame().number == 0U);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusFlipError) != 0);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusModeOk) == 0);

    f.write_reg(core::FramebufferDevice::Status,
                core::FramebufferDevice::StatusFlipError);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusFlipError) == 0);
    CHECK(f.fb().frame_number() == 0U);
}

TEST_CASE("flipping while disabled commits a disabled frame") {
    FramebufferFixture f;
    f.write_reg(core::FramebufferDevice::Control, 0);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    CHECK(f.fb().frame().number == 1U);
    CHECK_FALSE(f.fb().frame().enabled);
    CHECK((f.fb().status() & core::FramebufferDevice::StatusEnabled) == 0);
}

TEST_CASE("the committed frame is a snapshot of the flip") {
    FramebufferFixture f;
    f.set_mode(800, 600);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    f.set_mode(640, 480);
    CHECK(f.fb().frame().width == 800U);
    CHECK(f.fb().frame().height == 600U);
    CHECK(f.read_reg(core::FramebufferDevice::Width) == 640U);
}

TEST_CASE("the frame counter wraps at 2^32") {
    FramebufferFixture f;
    f.fb().set_frame_number(0xFFFF'FFFFU);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    CHECK(f.fb().frame_number() == 0U);
}

TEST_CASE("unlisted control offsets read zero and ignore writes") {
    FramebufferFixture f;
    CHECK(f.read_reg(0x40) == 0U);
    f.write_reg(0x40, 0xDEAD'BEEFU);
    CHECK(f.read_reg(0x40) == 0U);
    CHECK(f.read_reg(core::FramebufferDevice::Width) == core::kFramebufferWidth);
}

TEST_CASE("unaligned access at a 4 KiB boundary preserves neighbouring bytes") {
    FramebufferFixture f;
    const std::uint32_t boundary = core::kFramebufferBase + 0x1000;
    REQUIRE(f.machine.write_physical32(boundary - 4, 0x1122'3344U));
    REQUIRE(f.machine.write_physical32(boundary, 0x5566'7788U));
    REQUIRE(f.machine.write_physical8(boundary - 1, 0xAAU));

    std::uint32_t value = 0;
    REQUIRE(f.machine.read_physical32(boundary - 4, value));
    CHECK(value == 0xAA22'3344U);
    REQUIRE(f.machine.read_physical32(boundary, value));
    CHECK(value == 0x5566'7788U);

    std::uint16_t half = 0;
    REQUIRE(f.machine.read_physical16(boundary - 2, half));
    CHECK(half == 0xAA22U);

    REQUIRE(f.machine.write_physical16(boundary - 1, 0xBEEFU));
    std::uint8_t byte = 0;
    REQUIRE(f.machine.read_physical8(boundary - 1, byte));
    CHECK(byte == 0xEFU);
    REQUIRE(f.machine.read_physical8(boundary, byte));
    CHECK(byte == 0xBEU);
    REQUIRE(f.machine.read_physical8(boundary + 1, byte));
    CHECK(byte == 0x77U);
}

TEST_CASE("flipping front and back to the same offset still increments") {
    FramebufferFixture f;
    f.write_reg(core::FramebufferDevice::Front, 0x2000);
    f.write_reg(core::FramebufferDevice::Back, 0x2000);
    f.write_reg(core::FramebufferDevice::Flip, 1);
    CHECK(f.fb().frame_number() == 1U);
    CHECK(f.fb().frame().offset == 0x2000U);
    CHECK(f.read_reg(core::FramebufferDevice::Front) == 0x2000U);
    CHECK(f.read_reg(core::FramebufferDevice::Back) == 0x2000U);
}

TEST_CASE("the aperture faults one word past its end") {
    FramebufferFixture f;
    CHECK(f.machine.write_physical32(core::kFramebufferBase + core::kFramebufferSize, 0) == false);
    CHECK(f.machine.write_physical32(core::kFramebufferBase + core::kFramebufferSize - 4, 0));
}

TEST_CASE("keyboard events pack into one word") {
    const std::uint32_t word =
        core::KeyboardDevice::pack_event('a', 0x1, 0x41, 0);
    CHECK((word & 0xFFU) == 'a');
    CHECK(((word >> 8) & 0xFFU) == 0x1U);
    CHECK(((word >> 16) & 0xFFU) == 0x41U);
    CHECK(((word >> 24) & 0xFFU) == 0U);
}

TEST_CASE("keyboard FIFO pops in order and returns zero when empty") {
    core::DeviceBus devices;
    core::KeyboardDevice& keyboard = devices.keyboard();
    keyboard.reset();
    CHECK(keyboard.push_event(1));
    CHECK(keyboard.push_event(2));

    std::uint32_t value = 0;
    REQUIRE(keyboard.read(core::KeyboardDevice::Data, value));
    CHECK(value == 1U);
    REQUIRE(keyboard.read(core::KeyboardDevice::Data, value));
    CHECK(value == 2U);
    REQUIRE(keyboard.read(core::KeyboardDevice::Data, value));
    CHECK(value == 0U);
}

TEST_CASE("keyboard drops the 65th event and sets overrun") {
    core::KeyboardDevice keyboard;
    keyboard.reset();
    for (std::uint32_t index = 1; index <= core::KeyboardDevice::Capacity; ++index) {
        REQUIRE(keyboard.push_event(index));
    }
    CHECK_FALSE(keyboard.push_event(65));
    CHECK(keyboard.overrun());
    CHECK(keyboard.size() == core::KeyboardDevice::Capacity);

    std::uint32_t status = 0;
    REQUIRE(keyboard.read(core::KeyboardDevice::Status, status));
    CHECK((status & core::KeyboardDevice::StatusOverrun) != 0);
    CHECK(((status >> 8) & 0xFFU) == core::KeyboardDevice::Capacity);

    REQUIRE(keyboard.write(core::KeyboardDevice::Status,
                           core::KeyboardDevice::StatusOverrun));
    REQUIRE(keyboard.read(core::KeyboardDevice::Status, status));
    CHECK((status & core::KeyboardDevice::StatusOverrun) == 0);
}

TEST_CASE("keyboard raises its routed line while enabled and non-empty") {
    core::DeviceBus devices;
    std::vector<std::uint32_t> lines;
    devices.set_irq_handler([&](std::uint32_t line) { lines.push_back(line); });
    core::KeyboardDevice& keyboard = devices.keyboard();
    keyboard.reset();

    REQUIRE(keyboard.write(core::KeyboardDevice::Irq, 3));
    REQUIRE(keyboard.push_event(0x41));
    CHECK(lines.empty());

    REQUIRE(keyboard.write(core::KeyboardDevice::Control, 1));
    CHECK(lines.size() == 1U);
    CHECK(lines.back() == 3U);

    REQUIRE(keyboard.push_event(0x42));
    CHECK(lines.size() == 2U);
    CHECK(lines.back() == 3U);
}

TEST_CASE("keyboard reset clears FIFO, overrun, and control") {
    core::KeyboardDevice keyboard;
    keyboard.reset();
    REQUIRE(keyboard.push_event(7));
    REQUIRE(keyboard.write(core::KeyboardDevice::Control, 1));
    keyboard.reset();

    CHECK(keyboard.size() == 0U);
    CHECK_FALSE(keyboard.overrun());
    std::uint32_t value = 0xFF;
    REQUIRE(keyboard.read(core::KeyboardDevice::Control, value));
    CHECK(value == 0U);
    REQUIRE(keyboard.read(core::KeyboardDevice::Data, value));
    CHECK(value == 0U);
}

TEST_CASE("an unclaimed device page still faults") {
    FramebufferFixture f;
    std::uint32_t value = 0;
    CHECK_FALSE(f.machine.read_physical32(core::kDeviceBase + 0x9000, value));
}

struct PagedGuest {
    static constexpr std::uint32_t kDirectory = 0x0003'0000;
    static constexpr std::uint32_t kLeafLow = 0x0003'1000;
    static constexpr std::uint32_t kLeafVram = 0x0003'2000;
    static constexpr std::uint32_t kLeafMmio = 0x0003'3000;

    core::Machine machine;
    core::DeviceBus devices;

    PagedGuest() : machine(core::Machine::Options{1U * 1024U * 1024U, 0, true}) {
        devices.attach_machine(machine);
        machine.attach_devices(&devices);
    }

    static std::uint32_t leaf_for(std::uint32_t directory_index) {
        if (directory_index == 0U) return kLeafLow;
        if (directory_index == 1008U) return kLeafVram; // 0xFC00_0000
        return kLeafMmio;                               // 0xFFC0_0000 and above
    }

    void map(std::uint32_t address, std::uint32_t flags) {
        const std::uint32_t vpn = address >> 12;
        const std::uint32_t directory_index = vpn >> 10;
        const std::uint32_t leaf = leaf_for(directory_index);
        REQUIRE(machine.write_physical32(kDirectory + directory_index * 4U,
                                         leaf | core::kPteValid));
        REQUIRE(machine.write_physical32(leaf + (vpn & 0x3FFU) * 4U,
                                         (address & core::kPageFrameMask) | flags));
    }

    void enable() {
        REQUIRE(machine.write_control_register(8, kDirectory));
        REQUIRE(machine.write_control_register(10, core::kMctlPaging));
        REQUIRE(machine.paging_enabled());
    }
};

TEST_CASE("a guest assembly program sets a mode, fills VRAM, and flips") {
    const std::string source =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    MOV   R0, #0xFFF04000\n"
        "    MOV   R1, #800\n"
        "    STORE R1, [R0 + 4]\n"
        "    MOV   R1, #600\n"
        "    STORE R1, [R0 + 8]\n"
        "    MOV   R1, #0\n"
        "    STORE R1, [R0 + 20]\n"
        "    STORE R1, [R0 + 24]\n"
        "    MOV   R1, #1\n"
        "    STORE R1, [R0 + 0]\n"
        "    MOV   R2, #0xFC000000\n"
        "    MOV   R3, #0x00FF0000\n"
        "    STORE R3, [R2]\n"
        "    STORE R1, [R0 + 32]\n"
        "    HALT\n";

    assembler::Options options;
    options.base = 0x0001'0000;
    assembler::Result assembled;
    assembler::Error error;
    REQUIRE_MESSAGE(assembler::assemble(source, options, assembled, error),
                    error.message.c_str());

    PagedGuest guest;
    REQUIRE(guest.machine.load_image(options.base, assembled.bytes));
    for (std::uint32_t address = core::kRamBase;
         address < core::kRamBase + 1024U * 1024U;
         address += core::kPageSize) {
        guest.map(address, core::kPteValid | core::kPteWritable | core::kPteExecutable);
    }
    guest.map(core::kFramebufferBase, core::kPteValid | core::kPteWritable);
    guest.map(core::kFramebufferControlBase, core::kPteValid | core::kPteWritable);
    guest.enable();
    guest.machine.set_pc(options.base);

    for (int step = 0; step < 200 && !guest.machine.halted(); ++step) {
        (void)guest.machine.step();
    }
    REQUIRE(guest.machine.halted());

    const core::FramebufferDevice& fb = guest.devices.framebuffer();
    CHECK(fb.frame().number == 1U);
    CHECK(fb.frame().width == 800U);
    CHECK(fb.frame().height == 600U);
    CHECK(fb.frame().offset == 0U);
    REQUIRE(fb.frame_pixels().size() >= 4U);
    CHECK(fb.frame_pixels()[0] == 0x00U);
    CHECK(fb.frame_pixels()[1] == 0x00U);
    CHECK(fb.frame_pixels()[2] == 0xFFU);
    CHECK(fb.frame_pixels()[3] == 0x00U);
}
