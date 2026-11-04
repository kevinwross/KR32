#include "doctest.h"

#include "kr32/asm/assembler.h"
#include "kr32/core/cluster.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"

#include <cstdint>
#include <string>

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
    CHECK(devices.framebuffer().pixels()[pixel_offset] == 0x44U);

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
