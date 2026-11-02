#include "doctest.h"

#include "kr32/core/machine.h"
#include "kr32/isa/encoding.h"

#include <cstdint>
#include <functional>

using kr32::core::Machine;
using kr32::isa::Encoded;
using kr32::isa::Instruction;
using kr32::isa::encode;

namespace {

Machine make_machine() {
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

void write_instruction(Machine& machine, std::uint32_t address, const Instruction& instruction) {
    Encoded encoded{};
    REQUIRE(encode(instruction, encoded));
    for (std::uint8_t index = 0; index < encoded.count; ++index) {
        REQUIRE(machine.write32(address + 4U * index, encoded.words[index]));
    }
}

void prepare(Machine& machine,
             const Instruction& instruction,
             const std::function<void(Machine&)>& setup = {}) {
    write_instruction(machine, kr32::core::kRamBase, instruction);
    machine.reset(kr32::core::kRamBase);
    if (setup) {
        setup(machine);
    }
}

} // namespace

TEST_CASE("MOV writes a register or an immediate") {
    Machine machine = make_machine();
    Instruction instruction = make(0x00, 1, 0, 0, true, 42);
    prepare(machine, instruction);
    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(1) == 42);
    CHECK(machine.pc() == kr32::core::kRamBase + 4);
    CHECK(machine.ticks() == 1);
}

TEST_CASE("ADD and SUB set the documented flags") {
    Machine machine = make_machine();
    prepare(machine, make(0x20, 3, 1, 2), [](Machine& m) {
        m.set_reg(1, 0x7FFF'FFFFU);
        m.set_reg(2, 1);
    });
    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(3) == 0x8000'0000U);
    CHECK((machine.flags() & kr32::core::kFlagN) != 0);
    CHECK((machine.flags() & kr32::core::kFlagV) != 0);
    CHECK((machine.flags() & kr32::core::kFlagZ) == 0);
    CHECK((machine.flags() & kr32::core::kFlagC) == 0);

    Machine subtract = make_machine();
    prepare(subtract, make(0x21, 2, 1, 0, true, 1), [](Machine& m) {
        m.set_reg(1, 0);
    });
    const auto sub_result = subtract.step();
    CHECK(sub_result.kind == kr32::core::StepResult::Kind::Retired);
    CHECK(subtract.reg(2) == 0xFFFF'FFFFU);    CHECK((subtract.flags() & kr32::core::kFlagC) == 0);
    CHECK((subtract.flags() & kr32::core::kFlagN) != 0);
}

TEST_CASE("memory loads and stores use the effective address") {
    Machine machine = make_machine();
    const std::uint32_t address = kr32::core::kRamBase + 0x100;
    prepare(machine, make(0x02, 1, 2, 2, true, 0), [&](Machine& m) {
        m.set_reg(1, 0x1234'5678U);
        m.set_reg(2, address);
    });
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    std::uint32_t stored = 0;
    REQUIRE(machine.read32(address, stored));
    CHECK(stored == 0x1234'5678U);

    Machine load = make_machine();
    write_instruction(load, kr32::core::kRamBase, make(0x01, 3, 2, 2, true, 0));
    load.reset(kr32::core::kRamBase);
    REQUIRE(load.write32(address, 0xDEAD'BEEFU));
    load.set_reg(2, address);
    CHECK(load.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(load.reg(3) == 0xDEAD'BEEFU);
    CHECK(load.reg(2) == address);
}

TEST_CASE("byte loads sign extend and byte stores keep the low byte") {
    Machine machine = make_machine();
    const std::uint32_t address = kr32::core::kRamBase + 0x200;
    write_instruction(machine, kr32::core::kRamBase, make(0x04, 4, 2, 2, true, 0));
    machine.reset(kr32::core::kRamBase);
    REQUIRE(machine.write8(address, 0xF0));
    machine.set_reg(2, address);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(4) == 0xFFFF'FFF0U);

    Machine store = make_machine();
    write_instruction(store, kr32::core::kRamBase, make(0x07, 1, 2, 2, true, 0));
    store.reset(kr32::core::kRamBase);
    store.set_reg(1, 0xABCD'1234U);
    store.set_reg(2, address);
    CHECK(store.step().kind == kr32::core::StepResult::Kind::Retired);
    std::uint8_t byte = 0;
    REQUIRE(store.read8(address, byte));
    CHECK(byte == 0x34);
}

TEST_CASE("PUSH and POP use the stack pointer") {
    Machine machine = make_machine();
    write_instruction(machine, kr32::core::kRamBase, make(0x3C, 1));
    machine.reset(kr32::core::kRamBase);
    const std::uint32_t initial_sp = machine.sp();
    machine.set_reg(1, 42);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.sp() == initial_sp - 4);
    std::uint32_t value = 0;
    REQUIRE(machine.read32(machine.sp(), value));
    CHECK(value == 42);

    write_instruction(machine, kr32::core::kRamBase + 4, make(0x3D, 2));
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(2) == 42);
    CHECK(machine.sp() == initial_sp);
}

TEST_CASE("CALL and RET return to the following instruction") {
    Machine machine = make_machine();
    const std::uint32_t base = kr32::core::kRamBase;
    write_instruction(machine, base, make(0x3A, 0, 1, 0));
    write_instruction(machine, base + 4, make(0x43));
    write_instruction(machine, base + 8, make(0x00, 2, 0, 0, true, 7));
    write_instruction(machine, base + 12, make(0x3B));
    machine.reset(base);
    const std::uint32_t initial_sp = machine.sp();
    machine.set_reg(1, base + 8);

    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.pc() == base + 8);
    CHECK(machine.sp() == initial_sp - 4);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(2) == 7);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.pc() == base + 4);
    CHECK(machine.sp() == initial_sp);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Halted);
}

TEST_CASE("shifts set carry and arithmetic shifts preserve sign") {
    Machine shift = make_machine();
    write_instruction(shift, kr32::core::kRamBase, make(0x30, 2, 1, 0, true, 4));
    shift.reset(kr32::core::kRamBase);
    shift.set_reg(1, 1);
    CHECK(shift.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(shift.reg(2) == 16);

    Machine arithmetic = make_machine();
    write_instruction(arithmetic, kr32::core::kRamBase, make(0x32, 2, 1, 0, true, 1));
    arithmetic.reset(kr32::core::kRamBase);
    arithmetic.set_reg(1, 0xFFFF'FFF8U);
    CHECK(arithmetic.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(arithmetic.reg(2) == 0xFFFF'FFFCU);
    CHECK((arithmetic.flags() & kr32::core::kFlagN) != 0);
}

TEST_CASE("SLT and SLTU distinguish signed and unsigned comparisons") {
    Machine signed_less = make_machine();
    write_instruction(signed_less, kr32::core::kRamBase, make(0x18, 3, 1, 2));
    signed_less.reset(kr32::core::kRamBase);
    signed_less.set_reg(1, 0xFFFF'FFFFU);
    signed_less.set_reg(2, 1);
    CHECK(signed_less.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(signed_less.reg(3) == 1);

    Machine unsigned_less = make_machine();
    write_instruction(unsigned_less, kr32::core::kRamBase, make(0x19, 3, 1, 2));
    unsigned_less.reset(kr32::core::kRamBase);
    unsigned_less.set_reg(1, 0xFFFF'FFFFU);
    unsigned_less.set_reg(2, 1);
    CHECK(unsigned_less.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(unsigned_less.reg(3) == 0);
}

TEST_CASE("LEA computes the effective address without a load") {
    Machine machine = make_machine();
    write_instruction(machine, kr32::core::kRamBase, make(0x09, 2, 1, 3, true, 8));
    machine.reset(kr32::core::kRamBase);
    machine.set_reg(1, kr32::core::kRamBase + 0x100);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.reg(2) == kr32::core::kRamBase + 0x108);
}

TEST_CASE("divide by zero raises the documented fault") {
    Machine machine = make_machine();
    write_instruction(machine, kr32::core::kRamBase, make(0x25, 3, 1, 2));
    machine.reset(kr32::core::kRamBase);
    machine.set_reg(1, 1);
    machine.set_reg(2, 0);
    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::DivideByZero);
    CHECK(result.fault.vector == kr32::core::kVectorDivideByZero);
}

TEST_CASE("multiply and divide edge cases follow the flag contract") {
    Machine multiply = make_machine();
    write_instruction(multiply, kr32::core::kRamBase, make(0x22, 3, 1, 2));
    multiply.reset(kr32::core::kRamBase);
    multiply.set_reg(1, 0xFFFF'FFFFU);
    multiply.set_reg(2, 2);
    CHECK(multiply.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(multiply.reg(3) == 0xFFFF'FFFEU);
    CHECK((multiply.flags() & kr32::core::kFlagV) != 0);

    Machine mulh = make_machine();
    write_instruction(mulh, kr32::core::kRamBase, make(0x23, 3, 1, 2));
    mulh.reset(kr32::core::kRamBase);
    mulh.set_reg(1, 0xFFFF'FFFEU);
    mulh.set_reg(2, 3);
    CHECK(mulh.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(mulh.reg(3) == 0xFFFF'FFFFU);

    Machine mulhu = make_machine();
    write_instruction(mulhu, kr32::core::kRamBase, make(0x24, 3, 1, 2));
    mulhu.reset(kr32::core::kRamBase);
    mulhu.set_reg(1, 0xFFFF'FFFFU);
    mulhu.set_reg(2, 2);
    CHECK(mulhu.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(mulhu.reg(3) == 1);

    Machine divide = make_machine();
    write_instruction(divide, kr32::core::kRamBase, make(0x26, 3, 1, 2));
    divide.reset(kr32::core::kRamBase);
    divide.set_reg(1, 0x8000'0000U);
    divide.set_reg(2, 0xFFFF'FFFFU);
    CHECK(divide.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(divide.reg(3) == 0x8000'0000U);

    Machine modulo = make_machine();
    write_instruction(modulo, kr32::core::kRamBase, make(0x28, 3, 1, 2));
    modulo.reset(kr32::core::kRamBase);
    modulo.set_reg(1, 0x8000'0000U);
    modulo.set_reg(2, 0xFFFF'FFFFU);
    CHECK(modulo.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(modulo.reg(3) == 0);
}

TEST_CASE("JCOND implements every condition code") {
    const std::uint32_t target = kr32::core::kRamBase + 0x80;

    auto expected = [](std::uint32_t condition, bool z, bool n, bool c, bool v) {
        switch (condition) {
        case 0: return z;
        case 1: return !z;
        case 2: return c;
        case 3: return !c;
        case 4: return n;
        case 5: return !n;
        case 6: return v;
        case 7: return !v;
        case 8: return c && !z;
        case 9: return !c || z;
        case 10: return n == v;
        case 11: return n != v;
        case 12: return !z && (n == v);
        case 13: return z || (n != v);
        case 14: return true;
        default: return false;
        }
    };

    for (std::uint32_t condition = 0; condition < 16; ++condition) {
        for (std::uint32_t mask = 0; mask < 16; ++mask) {
            const bool z = (mask & 1U) != 0;
            const bool n = (mask & 2U) != 0;
            const bool c = (mask & 4U) != 0;
            const bool v = (mask & 8U) != 0;

            Machine machine = make_machine();
            write_instruction(machine, kr32::core::kRamBase,
                              make(0x39, 0, 1, static_cast<std::uint8_t>(condition)));
            machine.reset(kr32::core::kRamBase);
            machine.set_reg(1, target);
            std::uint32_t flags = 0;
            if (z) flags |= kr32::core::kFlagZ;
            if (n) flags |= kr32::core::kFlagN;
            if (c) flags |= kr32::core::kFlagC;
            if (v) flags |= kr32::core::kFlagV;
            machine.set_flags(flags);

            CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
            const std::uint32_t expected_pc =
                expected(condition, z, n, c, v) ? target : kr32::core::kRamBase + 4;
            CHECK(machine.pc() == expected_pc);
        }
    }
}

TEST_CASE("JCOND branches to a register target when the condition holds") {
    Machine machine = make_machine();
    const std::uint32_t target = kr32::core::kRamBase + 0x40;
    write_instruction(machine, kr32::core::kRamBase, make(0x39, 0, 1, 0));
    machine.reset(kr32::core::kRamBase);
    machine.set_reg(1, target);
    machine.set_flags(kr32::core::kFlagZ);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.pc() == target);
}

TEST_CASE("the same program produces the same state twice") {
    auto run = []() {
        Machine machine = make_machine();
        write_instruction(machine, kr32::core::kRamBase, make(0x00, 1, 0, 0, true, 5));
        write_instruction(machine, kr32::core::kRamBase + 4, make(0x20, 2, 1, 1));
        write_instruction(machine, kr32::core::kRamBase + 8, make(0x43));
        machine.reset(kr32::core::kRamBase);
        while (machine.step().kind == kr32::core::StepResult::Kind::Retired) {
        }
        return machine;
    };

    const Machine first = run();
    const Machine second = run();
    CHECK(first.reg(1) == second.reg(1));
    CHECK(first.reg(2) == second.reg(2));
    CHECK(first.pc() == second.pc());
    CHECK(first.sp() == second.sp());
    CHECK(first.ticks() == second.ticks());
}
