#include "doctest.h"

#include "kr32/core/machine.h"
#include "kr32/isa/encoding.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

using kr32::core::Machine;
using kr32::isa::Encoded;
using kr32::isa::Instruction;
using kr32::isa::encode;

namespace {

constexpr std::uint32_t kBase = 0x0001'0000;
constexpr std::uint32_t kData = 0x0001'8000;

Machine make_machine() {
    return Machine(Machine::Options{1U * 1024U * 1024U, 0, false});
}

Instruction fp(std::uint32_t function,
               std::uint8_t fd = 0,
               std::uint8_t fs1 = 1,
               std::uint8_t fs2 = 2) {
    Instruction instruction{};
    instruction.opcode = 0x60;
    instruction.rd = fd;
    instruction.rs1 = fs1;
    instruction.field = fs2;
    instruction.immediate = static_cast<std::int16_t>(function);
    return instruction;
}

Instruction memory(std::uint8_t opcode,
                   std::uint8_t fpr,
                   std::uint8_t base,
                   bool wide) {
    Instruction instruction{};
    instruction.opcode = opcode;
    instruction.rd = fpr;
    instruction.rs1 = base;
    instruction.field = 2;
    instruction.literal = wide;
    return instruction;
}

void put(Machine& machine, const Instruction& instruction) {
    Encoded encoded{};
    REQUIRE(encode(instruction, encoded));
    for (std::uint8_t index = 0; index < encoded.count; ++index) {
        REQUIRE(machine.write_physical32(kBase + 4U * index, encoded.words[index]));
    }
}

std::uint32_t f32_bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

std::uint64_t f64_bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

Machine run_fp(const Instruction& instruction,
               const std::function<void(Machine&)>& setup = {}) {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, instruction);
    machine.set_pc(kBase);
    if (setup) {
        setup(machine);
    }
    const auto result = machine.step();
    REQUIRE(result.kind == kr32::core::StepResult::Kind::Retired);
    return machine;
}

} // namespace

TEST_CASE("scalar FP arithmetic follows single and double precision") {
    const Machine add = run_fp(fp(0x000), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(1.5F));
        machine.set_fpr(2, f32_bits(2.25F));
    });
    CHECK(add.fpr(0) == f32_bits(3.75F));

    const Machine sub = run_fp(fp(0x001), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(5.0F));
        machine.set_fpr(2, f32_bits(1.25F));
    });
    CHECK(sub.fpr(0) == f32_bits(3.75F));

    const Machine mul = run_fp(fp(0x002), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(1.5F));
        machine.set_fpr(2, f32_bits(2.0F));
    });
    CHECK(mul.fpr(0) == f32_bits(3.0F));

    const Machine divide = run_fp(fp(0x003), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(3.0F));
        machine.set_fpr(2, f32_bits(2.0F));
    });
    CHECK(divide.fpr(0) == f32_bits(1.5F));

    const Machine square_root = run_fp(fp(0x004, 0, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(9.0F));
    });
    CHECK(square_root.fpr(0) == f32_bits(3.0F));

    const Machine dadd = run_fp(fp(0x100), [](Machine& machine) {
        machine.set_fpr(1, f64_bits(1.5));
        machine.set_fpr(2, f64_bits(2.25));
    });
    CHECK(dadd.fpr(0) == f64_bits(3.75));
}

TEST_CASE("FP NaN and min/max rules are canonical") {
    const Machine canonical = run_fp(fp(0x000), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(std::numeric_limits<float>::infinity()));
        machine.set_fpr(2, f32_bits(-std::numeric_limits<float>::infinity()));
    });
    CHECK(canonical.fpr(0) == 0x7FC0'0000U);

    const Machine sqrt_negative = run_fp(fp(0x004, 0, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(-1.0F));
    });
    CHECK(sqrt_negative.fpr(0) == 0x7FC0'0000U);

    const Machine min_nan = run_fp(fp(0x005), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(std::numeric_limits<float>::quiet_NaN()));
        machine.set_fpr(2, f32_bits(4.0F));
    });
    CHECK(min_nan.fpr(0) == f32_bits(4.0F));

    const Machine max_nan = run_fp(fp(0x006), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(4.0F));
        machine.set_fpr(2, f32_bits(std::numeric_limits<float>::quiet_NaN()));
    });
    CHECK(max_nan.fpr(0) == f32_bits(4.0F));

    const Machine both_nan = run_fp(fp(0x005), [](Machine& machine) {
        machine.set_fpr(1, 0x7F80'0001U);
        machine.set_fpr(2, 0x7F80'0002U);
    });
    CHECK(both_nan.fpr(0) == 0x7FC0'0000U);
}

TEST_CASE("FABS and FNEG operate on the sign bit") {
    const Machine absolute = run_fp(fp(0x007, 0, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, 0x8000'0000U | f32_bits(2.0F));
    });
    CHECK(absolute.fpr(0) == f32_bits(2.0F));

    const Machine negate = run_fp(fp(0x008, 0, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(2.0F));
    });
    CHECK(negate.fpr(0) == (0x8000'0000U | f32_bits(2.0F)));
}

TEST_CASE("FP compares write a GPR and set Z from the result") {
    const Machine equal = run_fp(fp(0x009, 5, 1, 2), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(2.0F));
        machine.set_fpr(2, f32_bits(2.0F));
    });
    CHECK(equal.reg(5) == 1);
    CHECK((equal.flags() & kr32::core::kFlagZ) == 0);

    const Machine not_equal = run_fp(fp(0x00A, 5, 1, 2), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(2.0F));
        machine.set_fpr(2, f32_bits(3.0F));
    });
    CHECK(not_equal.reg(5) == 1);
    CHECK((not_equal.flags() & kr32::core::kFlagZ) == 0);

    const Machine nan = run_fp(fp(0x009, 5, 1, 2), [](Machine& machine) {
        machine.set_fpr(1, 0x7FC0'0000U);
        machine.set_fpr(2, f32_bits(2.0F));
    });
    CHECK(nan.reg(5) == 0);
    CHECK((nan.flags() & kr32::core::kFlagZ) != 0);

    const Machine less_equal = run_fp(fp(0x00B, 5, 1, 2), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(2.0F));
        machine.set_fpr(2, f32_bits(2.0F));
    });
    CHECK(less_equal.reg(5) == 1);
    CHECK((less_equal.flags() & kr32::core::kFlagZ) == 0);
}

TEST_CASE("FP conversions saturate and use raw moves correctly") {
    const Machine word = run_fp(fp(0x022, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(3.9F));
    });
    CHECK(word.reg(3) == 3);

    const Machine negative = run_fp(fp(0x022, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(-2.7F));
    });
    CHECK(negative.reg(3) == static_cast<std::uint32_t>(-2));

    const Machine nan = run_fp(fp(0x022, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, 0x7FC0'0000U);
    });
    CHECK(nan.reg(3) == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()));

    const Machine positive_inf = run_fp(fp(0x022, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(std::numeric_limits<float>::infinity()));
    });
    CHECK(positive_inf.reg(3) == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()));

    const Machine negative_inf = run_fp(fp(0x022, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(-std::numeric_limits<float>::infinity()));
    });
    CHECK(negative_inf.reg(3) == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min()));

    const Machine unsigned_negative = run_fp(fp(0x023, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(-1.0F));
    });
    CHECK(unsigned_negative.reg(3) == 0);

    const Machine unsigned_nan = run_fp(fp(0x023, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, 0x7FC0'0000U);
    });
    CHECK(unsigned_nan.reg(3) == std::numeric_limits<std::uint32_t>::max());

    const Machine from_int = run_fp(fp(0x024, 0, 1, 0), [](Machine& machine) {
        machine.set_reg(1, static_cast<std::uint32_t>(-7));
    });
    CHECK(from_int.fpr(0) == f32_bits(-7.0F));

    const Machine move = run_fp(fp(0x030, 0, 1, 0), [](Machine& machine) {
        machine.set_reg(1, 0xDEAD'BEEFU);
    });
    CHECK(move.fpr(0) == 0xDEAD'BEEFU);

    const Machine move_out = run_fp(fp(0x031, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, 0xFFFF'FFFF'DEAD'BEEFULL);
    });
    CHECK(move_out.reg(3) == 0xDEAD'BEEFU);

    const Machine to_double = run_fp(fp(0x021, 0, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(1.5F));
    });
    CHECK(to_double.fpr(0) == f64_bits(1.5));

    const Machine to_float = run_fp(fp(0x020, 0, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f64_bits(2.5));
    });
    CHECK(to_float.fpr(0) == f32_bits(2.5F));

    const Machine double_to_word = run_fp(fp(0x026, 3, 1, 0), [](Machine& machine) {
        machine.set_fpr(1, f64_bits(3.75));
    });
    CHECK(double_to_word.reg(3) == 3);

    const Machine word_to_double = run_fp(fp(0x028, 0, 1, 0), [](Machine& machine) {
        machine.set_reg(1, 6);
    });
    CHECK(word_to_double.fpr(0) == f64_bits(6.0));
}

TEST_CASE("denormals and FP division by zero do not trap") {
    const Machine denormal = run_fp(fp(0x000), [](Machine& machine) {
        machine.set_fpr(1, 0x0000'0001U);
        machine.set_fpr(2, 0x0000'0001U);
    });
    CHECK(denormal.fpr(0) == 0x0000'0002U);

    const Machine divide_zero = run_fp(fp(0x003), [](Machine& machine) {
        machine.set_fpr(1, f32_bits(1.0F));
        machine.set_fpr(2, f32_bits(0.0F));
    });
    CHECK(divide_zero.fpr(0) == f32_bits(std::numeric_limits<float>::infinity()));
}

TEST_CASE("FLOAD and FSTORE handle 32-bit and 64-bit values") {
    Machine load = make_machine();
    load.reset(kBase);
    put(load, memory(0x50, 1, 2, false));
    REQUIRE(load.write32(kData, f32_bits(1.25F)));
    load.set_pc(kBase);
    load.set_reg(2, kData);
    CHECK(load.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(load.fpr(1) == f32_bits(1.25F));

    Machine load_double = make_machine();
    load_double.reset(kBase);
    put(load_double, memory(0x50, 1, 2, true));
    REQUIRE(load_double.write32(kData, static_cast<std::uint32_t>(f64_bits(3.5) & 0xFFFF'FFFFU)));
    REQUIRE(load_double.write32(kData + 4U, static_cast<std::uint32_t>(f64_bits(3.5) >> 32)));
    load_double.set_pc(kBase);
    load_double.set_reg(2, kData);
    CHECK(load_double.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(load_double.fpr(1) == f64_bits(3.5));

    Machine store = make_machine();
    store.reset(kBase);
    put(store, memory(0x51, 1, 2, false));
    store.set_fpr(1, f32_bits(2.5F));
    store.set_pc(kBase);
    store.set_reg(2, kData);
    CHECK(store.step().kind == kr32::core::StepResult::Kind::Retired);
    std::uint32_t stored = 0;
    REQUIRE(store.read32(kData, stored));
    CHECK(stored == f32_bits(2.5F));
}
