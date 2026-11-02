#include "doctest.h"

#include "kr32/core/cluster.h"
#include "kr32/core/machine.h"
#include "kr32/isa/encoding.h"

#include <cstdint>

using kr32::core::Machine;
using kr32::isa::Encoded;
using kr32::isa::Instruction;
using kr32::isa::encode;

namespace {

constexpr std::uint32_t kBase = 0x0001'0000;
constexpr std::uint32_t kStack = 0x0001'8000;
constexpr std::uint32_t kKernelStack = 0x0001'F000;
constexpr std::uint32_t kDirectory = 0x0002'0000;
constexpr std::uint32_t kLeaf = 0x0002'1000;
constexpr std::uint32_t kVectorTable = 0x0001'2000;
constexpr std::uint32_t kHandler = 0x0001'0200;

Machine make_exceptions_machine() {
    return Machine(Machine::Options{1U * 1024U * 1024U, 0, true});
}

Machine make_fault_machine() {
    return Machine(Machine::Options{1U * 1024U * 1024U, 0, false});
}

Instruction make(std::uint8_t opcode,
                 std::uint8_t rd = 0,
                 std::uint8_t rs1 = 0,
                 std::uint8_t field = 0,
                 bool literal = false,
                 std::int16_t immediate = 0,
                 bool extension = false,
                 std::uint32_t extension_word = 0) {
    Instruction instruction{};
    instruction.opcode = opcode;
    instruction.rd = rd;
    instruction.rs1 = rs1;
    instruction.field = field;
    instruction.literal = literal;
    instruction.immediate = immediate;
    instruction.extension = extension;
    instruction.extension_word = extension_word;
    return instruction;
}

void put(Machine& machine, std::uint32_t address, const Instruction& instruction) {
    Encoded encoded{};
    REQUIRE(encode(instruction, encoded));
    for (std::uint8_t index = 0; index < encoded.count; ++index) {
        REQUIRE(machine.write_physical32(address + 4U * index, encoded.words[index]));
    }
}

class PagedMachine {
public:
    explicit PagedMachine(bool exceptions = false)
        : machine_(Machine::Options{1U * 1024U * 1024U, 0, exceptions}) {
        machine_.reset(kBase);
    }

    Machine& machine() { return machine_; }

    void map(std::uint32_t virtual_address,
             std::uint32_t physical_address,
             std::uint32_t flags) {
        const std::uint32_t vpn = virtual_address >> 12;
        REQUIRE(vpn < 1024);
        REQUIRE(machine_.write_physical32(kDirectory, kLeaf | kr32::core::kPteValid |
                                                         kr32::core::kPteUser));
        REQUIRE(machine_.write_physical32(kLeaf + (vpn & 0x3FFU) * 4U,
                                          (physical_address & kr32::core::kPageFrameMask) | flags));
    }

    void enable() {
        REQUIRE(machine_.write_control_register(8, kDirectory));
        REQUIRE(machine_.write_control_register(10, kr32::core::kMctlPaging));
    }

    void map_standard() {
        map(kBase, kBase, kr32::core::kPteValid | kr32::core::kPteWritable |
                              kr32::core::kPteExecutable | kr32::core::kPteUser);
        map(kStack, kStack, kr32::core::kPteValid | kr32::core::kPteWritable |
                                kr32::core::kPteUser);
        map(kKernelStack, kKernelStack, kr32::core::kPteValid | kr32::core::kPteWritable |
                                            kr32::core::kPteUser);
    }

private:
    Machine machine_;
};

std::uint32_t far_register(Machine& machine) {
    std::uint32_t value = 0;
    REQUIRE(machine.read_control_register(9, value));
    return value;
}

} // namespace

TEST_CASE("identity paging translates and updates A/D bits") {
    PagedMachine paged;
    paged.map_standard();
    paged.enable();
    Machine& machine = paged.machine();
    machine.set_flags(kr32::core::kFlagK);
    machine.set_pc(kBase);

    std::uint32_t value = 0;
    CHECK(machine.read32(kBase, value));
    CHECK(value == 0);

    std::uint32_t leaf = 0;
    CHECK(machine.read_physical32(kLeaf + 0x10U * 4U, leaf));
    CHECK((leaf & kr32::core::kPteAccessed) != 0);

    CHECK(machine.write32(kBase, 0xCAFE'BABEU));
    CHECK(machine.read_physical32(kLeaf + 0x10U * 4U, leaf));
    CHECK((leaf & kr32::core::kPteDirty) != 0);
}

TEST_CASE("fetch requires the executable bit") {
    PagedMachine paged;
    paged.map(kBase, kBase, kr32::core::kPteValid | kr32::core::kPteWritable |
                                kr32::core::kPteUser);
    paged.enable();
    Machine& machine = paged.machine();
    machine.set_flags(kr32::core::kFlagK);
    machine.set_pc(kBase);

    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::PageFault);
    CHECK(result.fault.vector == kr32::core::kVectorPageFault);
    CHECK((result.fault.code & 8U) != 0);
    CHECK((result.fault.code & 1U) != 0);
    CHECK(far_register(machine) == kBase);
}

TEST_CASE("store requires the writable bit") {
    PagedMachine paged;
    paged.map(kBase, kBase, kr32::core::kPteValid | kr32::core::kPteExecutable |
                                kr32::core::kPteUser);
    put(paged.machine(), kBase, make(0x02, 1, 2, 2, true, 0));
    paged.enable();
    Machine& machine = paged.machine();
    machine.set_flags(kr32::core::kFlagK);
    machine.set_pc(kBase);
    machine.set_reg(1, 42);
    machine.set_reg(2, kBase);

    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::PageFault);
    CHECK((result.fault.code & 2U) != 0);
    CHECK(far_register(machine) == kBase);
}

TEST_CASE("user mode requires the user bit") {
    PagedMachine paged;
    paged.map(kBase, kBase, kr32::core::kPteValid | kr32::core::kPteExecutable |
                                kr32::core::kPteWritable);
    paged.enable();
    Machine& machine = paged.machine();
    machine.set_flags(0);
    machine.set_pc(kBase);

    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::PageFault);
    CHECK((result.fault.code & 4U) != 0);
    CHECK((result.fault.code & 8U) != 0);
}

TEST_CASE("the guard region faults with paging off and on") {
    Machine off = make_fault_machine();
    off.reset(kBase);
    put(off, kBase, make(0x01, 1, 2, 2, true, 0));
    off.set_pc(kBase);
    off.set_reg(2, 0);
    const auto off_result = off.step();
    CHECK(off_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(off_result.fault.kind == kr32::core::FaultKind::GeneralProtection);
    CHECK(off_result.fault.vector == kr32::core::kVectorGeneralProtection);

    PagedMachine paged;
    paged.map(0, 0, kr32::core::kPteValid | kr32::core::kPteWritable |
                        kr32::core::kPteExecutable | kr32::core::kPteUser);
    paged.map(kBase, kBase, kr32::core::kPteValid | kr32::core::kPteWritable |
                                kr32::core::kPteExecutable | kr32::core::kPteUser);
    put(paged.machine(), kBase, make(0x01, 1, 2, 2, true, 0));
    paged.enable();
    Machine& machine = paged.machine();
    machine.set_flags(kr32::core::kFlagK);
    machine.set_pc(kBase);
    machine.set_reg(2, 0);
    const auto on_result = machine.step();
    CHECK(on_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(on_result.fault.kind == kr32::core::FaultKind::PageFault);
    CHECK((on_result.fault.code & 1U) != 0);
    CHECK(far_register(machine) == 0);
}

TEST_CASE("control register writes validate alignment and reserved fields") {
    Machine machine = make_fault_machine();
    CHECK_FALSE(machine.write_control_register(8, 0x1234));
    CHECK_FALSE(machine.write_control_register(10, 4));
    CHECK_FALSE(machine.write_control_register(11, 1));
    CHECK_FALSE(machine.write_control_register(12, 1));

    CHECK(machine.write_control_register(8, kDirectory));
    CHECK(machine.write_control_register(10, kr32::core::kMctlPaging));
    CHECK(machine.paging_enabled());
    CHECK(machine.write_control_register(3, kVectorTable));
    CHECK(machine.write_control_register(6, kKernelStack));
}

TEST_CASE("privileged instructions fault in user mode") {
    Machine machine = make_fault_machine();
    machine.reset(kBase);
    put(machine, kBase, make(0x4A, 1, 0, 3, false, 0));
    machine.set_pc(kBase);
    machine.set_reg(1, kVectorTable);
    machine.set_flags(0);
    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::GeneralProtection);

    Machine halt = make_fault_machine();
    halt.reset(kBase);
    put(halt, kBase, make(0x43));
    halt.set_pc(kBase);
    halt.set_flags(0);
    const auto halt_result = halt.step();
    CHECK(halt_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(halt_result.fault.kind == kr32::core::FaultKind::GeneralProtection);
}

TEST_CASE("BRK delivery builds a frame and IRET resumes after the trap") {
    Machine machine = make_exceptions_machine();
    machine.reset(kBase);
    put(machine, kBase, make(0x44));
    put(machine, kBase + 4, make(0x00, 1, 0, 0, true, 7));
    put(machine, kBase + 8, make(0x43));
    put(machine, kHandler, make(0x42));
    REQUIRE(machine.write_control_register(3, kVectorTable));
    REQUIRE(machine.write_physical32(kVectorTable + 3U * 4U, kHandler));

    const std::uint32_t initial_sp = machine.sp();
    const auto trap = machine.step();
    CHECK(trap.kind == kr32::core::StepResult::Kind::FaultDelivered);
    CHECK(machine.pc() == kHandler);
    CHECK(machine.sp() == initial_sp - 16U);
    CHECK(machine.kernel_mode());
    std::uint32_t esr = 0;
    REQUIRE(machine.read_control_register(5, esr));
    CHECK(esr == 3);

    std::uint32_t saved_pc = 0;
    REQUIRE(machine.read32(machine.sp(), saved_pc));
    CHECK(saved_pc == kBase + 4);

    const auto iret = machine.step();
    CHECK(iret.kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.pc() == kBase + 4);
    CHECK(machine.sp() == initial_sp);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(1) == 7);
}

TEST_CASE("user SYSCALL switches to KSP and IRET restores user mode") {
    Machine machine = make_exceptions_machine();
    machine.reset(kBase);
    put(machine, kBase, make(0x41, 0, 0, 0, true, 32));
    put(machine, kBase + 4, make(0x00, 1, 0, 0, true, 5));
    put(machine, kBase + 8, make(0x45));
    put(machine, kHandler, make(0x42));
    REQUIRE(machine.write_control_register(3, kVectorTable));
    REQUIRE(machine.write_control_register(6, kKernelStack));
    REQUIRE(machine.write_physical32(kVectorTable + 32U * 4U, kHandler));

    machine.set_pc(kBase);
    machine.set_sp(kStack);
    machine.set_flags(0);

    const auto trap = machine.step();
    CHECK(trap.kind == kr32::core::StepResult::Kind::FaultDelivered);
    CHECK(machine.pc() == kHandler);
    CHECK(machine.sp() == kKernelStack - 16U);
    CHECK(machine.kernel_mode());
    std::uint32_t esr = 0;
    REQUIRE(machine.read_control_register(5, esr));
    CHECK(esr == 32);

    const auto iret = machine.step();
    CHECK(iret.kind == kr32::core::StepResult::Kind::Retired);
    CHECK(!machine.kernel_mode());
    CHECK(machine.pc() == kBase + 4);
    CHECK(machine.sp() == kStack);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(1) == 5);
}

TEST_CASE("a failed delivery escalates to a double fault and halts") {
    Machine machine = make_exceptions_machine();
    machine.reset(kBase);
    put(machine, kBase, make(0x25, 1, 2, 3));
    machine.set_pc(kBase);
    machine.set_reg(2, 1);
    machine.set_reg(3, 0);

    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Halted);
    CHECK(machine.halted());
    CHECK(result.fault.kind == kr32::core::FaultKind::DoubleFault);
}

TEST_CASE("a pending interrupt is delivered before the next instruction") {
    Machine machine = make_exceptions_machine();
    machine.reset(kBase);
    put(machine, kBase, make(0x00, 1, 0, 0, true, 1));
    put(machine, kHandler, make(0x42));
    REQUIRE(machine.write_control_register(3, kVectorTable));
    REQUIRE(machine.write_physical32(kVectorTable + 16U * 4U, kHandler));
    REQUIRE(machine.write_control_register(1, 0));
    REQUIRE(machine.write_control_register(2, 1));
    REQUIRE(machine.write_control_register(0, 1));
    machine.set_pc(kBase);

    const auto interrupt = machine.step();
    CHECK(interrupt.kind == kr32::core::StepResult::Kind::FaultDelivered);
    CHECK(machine.pc() == kHandler);
    CHECK(machine.reg(1) == 0);

    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.pc() == kBase);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(1) == 1);
}

TEST_CASE("LL and SC succeed on the same aligned word and clear the reservation") {
    Machine machine = make_fault_machine();
    machine.reset(kBase);
    put(machine, kBase, make(0x46, 1, 2, 2, true, 0));
    put(machine, kBase + 4, make(0x47, 1, 2, 2, true, 0));
    put(machine, kBase + 8, make(0x47, 1, 2, 2, true, 0));
    REQUIRE(machine.write32(kStack, 0x1234'5678U));
    machine.set_pc(kBase);
    machine.set_reg(2, kStack);

    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(1) == 0x1234'5678U);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK((machine.flags() & kr32::core::kFlagZ) != 0);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK((machine.flags() & kr32::core::kFlagZ) == 0);
}

TEST_CASE("an unmapped load reports a not-present page fault") {
    PagedMachine paged;
    paged.map(kBase, kBase, kr32::core::kPteValid | kr32::core::kPteExecutable |
                                kr32::core::kPteUser);
    put(paged.machine(), kBase, make(0x01, 1, 2, 2, true, 0));
    paged.enable();
    Machine& machine = paged.machine();
    machine.set_flags(kr32::core::kFlagK);
    machine.set_pc(kBase);
    machine.set_reg(2, 0x0003'0000);

    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::PageFault);
    CHECK(result.fault.code == 0);
    CHECK(far_register(machine) == 0x0003'0000);
}

TEST_CASE("software vectors below 32 and user IRET raise general protection") {
    Machine interrupt = make_fault_machine();
    interrupt.reset(kBase);
    put(interrupt, kBase, make(0x40, 0, 0, 0, true, 5));
    interrupt.set_pc(kBase);
    const auto interrupt_result = interrupt.step();
    CHECK(interrupt_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(interrupt_result.fault.kind == kr32::core::FaultKind::GeneralProtection);
    CHECK(interrupt_result.fault.vector == kr32::core::kVectorGeneralProtection);

    Machine iret = make_fault_machine();
    iret.reset(kBase);
    put(iret, kBase, make(0x42));
    iret.set_pc(kBase);
    iret.set_flags(0);
    const auto iret_result = iret.step();
    CHECK(iret_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(iret_result.fault.kind == kr32::core::FaultKind::GeneralProtection);
}

TEST_CASE("LL and SC fault on unaligned and non-RAM addresses") {
    Machine unaligned = make_fault_machine();
    unaligned.reset(kBase);
    put(unaligned, kBase, make(0x46, 1, 2, 2, true, 0));
    unaligned.set_pc(kBase);
    unaligned.set_reg(2, kStack + 1);
    const auto align_result = unaligned.step();
    CHECK(align_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(align_result.fault.kind == kr32::core::FaultKind::Alignment);
    CHECK(align_result.fault.vector == kr32::core::kVectorAlignment);

    Machine rom = make_fault_machine();
    rom.reset(kBase);
    put(rom, kBase, make(0x46, 1, 2, 2, true, 0));
    rom.set_pc(kBase);
    rom.set_reg(2, kr32::core::kRomBase);
    const auto rom_result = rom.step();
    CHECK(rom_result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(rom_result.fault.kind == kr32::core::FaultKind::GeneralProtection);
}

TEST_CASE("two cores report distinct COREID values") {
    Machine zero(Machine::Options{1U * 1024U * 1024U, 0, true});
    Machine one(Machine::Options{1U * 1024U * 1024U, 1, true});
    std::uint32_t id0 = 99;
    std::uint32_t id1 = 99;
    REQUIRE(zero.read_control_register(11, id0));
    REQUIRE(one.read_control_register(11, id1));
    CHECK(id0 == 0);
    CHECK(id1 == 1);
}

TEST_CASE("the cluster hook gives each core an ID and raises a target IRQ") {
    kr32::core::Cluster cluster(2, kr32::core::Machine::Options{
                                       1U * 1024U * 1024U, 0, true});
    REQUIRE(cluster.size() == 2);

    std::uint32_t id = 99;
    REQUIRE(cluster.core(1).read_control_register(11, id));
    CHECK(id == 1);

    cluster.raise_interrupt(1, 3);
    std::uint32_t irq = 0;
    REQUIRE(cluster.core(1).read_control_register(2, irq));
    CHECK((irq & (1U << 3)) != 0);
    REQUIRE(cluster.core(0).read_control_register(2, irq));
    CHECK(irq == 0);
}
