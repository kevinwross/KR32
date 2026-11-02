#include "doctest.h"

#include "kr32/core/machine.h"
#include "kr32/isa/encoding.h"

#include <bit>
#include <cmath>
#include <cstdint>

using kr32::core::Machine;
using kr32::isa::Encoded;
using kr32::isa::Instruction;
using kr32::isa::encode;

namespace {

constexpr std::uint32_t kBase = 0x0001'0000;
constexpr std::uint32_t kData = 0x0001'8000;
constexpr std::uint32_t kTransformStack = 0x0001'C000;

Machine make_machine() {
    return Machine(Machine::Options{1U * 1024U * 1024U, 0, false});
}

Instruction matrix(std::uint32_t function,
                   std::uint8_t td = 0,
                   std::uint8_t ta = 1,
                   std::uint8_t tb = 2) {
    Instruction instruction{};
    instruction.opcode = 0x61;
    instruction.rd = td;
    instruction.rs1 = ta;
    instruction.field = tb;
    instruction.immediate = static_cast<std::int16_t>(function);
    return instruction;
}

Instruction load_store(std::uint8_t opcode,
                       std::uint8_t tile,
                       std::uint8_t base,
                       bool vector,
                       bool strided = false,
                       std::uint32_t stride = 0) {
    Instruction instruction{};
    instruction.opcode = opcode;
    instruction.rd = tile;
    instruction.rs1 = base;
    instruction.field = 2;
    instruction.literal = vector;
    instruction.extension = strided;
    instruction.extension_word = stride;
    return instruction;
}

void put_at(Machine& machine, std::uint32_t address, const Instruction& instruction) {
    Encoded encoded{};
    REQUIRE(encode(instruction, encoded));
    for (std::uint8_t index = 0; index < encoded.count; ++index) {
        REQUIRE(machine.write_physical32(address + 4U * index, encoded.words[index]));
    }
}

void put(Machine& machine, const Instruction& instruction) {
    put_at(machine, kBase, instruction);
}

std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

} // namespace

TEST_CASE("MMUL and MMAC match a reference 4x4 multiply") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, matrix(0x000));
    for (std::size_t index = 0; index < 16; ++index) {
        machine.set_tile_value(1, index, 1.0F);
        machine.set_tile_value(2, index, 2.0F);
    }
    machine.set_pc(kBase);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t index = 0; index < 16; ++index) {
        CHECK(machine.tile_value(0, index) == 8.0F);
    }

    Machine accumulate = make_machine();
    accumulate.reset(kBase);
    put(accumulate, matrix(0x001));
    for (std::size_t index = 0; index < 16; ++index) {
        accumulate.set_tile_value(0, index, 1.0F);
        accumulate.set_tile_value(1, index, 1.0F);
        accumulate.set_tile_value(2, index, 2.0F);
    }
    accumulate.set_pc(kBase);
    CHECK(accumulate.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t index = 0; index < 16; ++index) {
        CHECK(accumulate.tile_value(0, index) == 9.0F);
    }
}

TEST_CASE("MMUL reads a non-trivial reference product") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, matrix(0x000));
    const float left[16] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
    };
    const float right[16] = {
        1, 0, 0, 0,
        0, 2, 0, 0,
        0, 0, 3, 0,
        0, 0, 0, 4,
    };
    for (std::size_t index = 0; index < 16; ++index) {
        machine.set_tile_value(1, index, left[index]);
        machine.set_tile_value(2, index, right[index]);
    }
    machine.set_pc(kBase);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    const float expected[16] = {
        1, 4, 9, 16,
        5, 12, 21, 32,
        9, 20, 33, 48,
        13, 28, 45, 64,
    };
    for (std::size_t index = 0; index < 16; ++index) {
        CHECK(machine.tile_value(0, index) == expected[index]);
    }
}

TEST_CASE("MVEC multiplies a matrix by row zero and clears the other rows") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, matrix(0x002));
    for (std::size_t index = 0; index < 16; ++index) {
        machine.set_tile_value(1, index, static_cast<float>(index + 1));
    }
    machine.set_tile_value(2, 0, 1.0F);
    machine.set_pc(kBase);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t row = 0; row < 4; ++row) {
        CHECK(machine.tile_value(0, row * 4U) == static_cast<float>(row * 4U + 1));
        for (std::size_t column = 1; column < 4; ++column) {
            CHECK(machine.tile_value(0, row * 4U + column) == 0.0F);
        }
    }
}

TEST_CASE("MTRANS and VDOT match their reference operations") {
    Machine transpose = make_machine();
    transpose.reset(kBase);
    put(transpose, matrix(0x003, 0, 1, 0));
    for (std::size_t index = 0; index < 16; ++index) {
        transpose.set_tile_value(1, index, static_cast<float>(index));
    }
    transpose.set_pc(kBase);
    CHECK(transpose.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            CHECK(transpose.tile_value(0, row * 4U + column) ==
                  transpose.tile_value(1, column * 4U + row));
        }
    }

    Machine dot = make_machine();
    dot.reset(kBase);
    put(dot, matrix(0x004, 0, 1, 2));
    dot.set_tile_value(1, 0, 1.0F);
    dot.set_tile_value(1, 1, 2.0F);
    dot.set_tile_value(1, 2, 3.0F);
    dot.set_tile_value(1, 3, 4.0F);
    dot.set_tile_value(2, 0, 5.0F);
    dot.set_tile_value(2, 1, 6.0F);
    dot.set_tile_value(2, 2, 7.0F);
    dot.set_tile_value(2, 3, 8.0F);
    dot.set_pc(kBase);
    CHECK(dot.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(dot.fpr(0) == bits(70.0F));
}

TEST_CASE("MLOAD, MSTORE, VLOAD, and VSTORE transfer tiles") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, load_store(0x52, 1, 2, false));
    for (std::size_t index = 0; index < 16; ++index) {
        REQUIRE(machine.write32(kData + static_cast<std::uint32_t>(index * 4U),
                                bits(static_cast<float>(index + 1))));
    }
    machine.set_pc(kBase);
    machine.set_reg(2, kData);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t index = 0; index < 16; ++index) {
        CHECK(machine.tile_value(1, index) == static_cast<float>(index + 1));
    }

    machine.set_pc(kBase + 4U);
    put_at(machine, kBase + 4U, load_store(0x53, 1, 2, false));
    for (std::size_t index = 0; index < 16; ++index) {
        machine.set_tile_value(1, index, static_cast<float>(index + 11));
    }
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t index = 0; index < 16; ++index) {
        std::uint32_t value = 0;
        REQUIRE(machine.read32(kData + static_cast<std::uint32_t>(index * 4U), value));
        CHECK(value == bits(static_cast<float>(index + 11)));
    }

    Machine vector = make_machine();
    vector.reset(kBase);
    put(vector, load_store(0x52, 1, 2, true));
    for (std::size_t index = 0; index < 4; ++index) {
        REQUIRE(vector.write32(kData + static_cast<std::uint32_t>(index * 4U),
                               bits(static_cast<float>(index + 1))));
    }
    vector.set_pc(kBase);
    vector.set_reg(2, kData);
    CHECK(vector.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const float expected = row == 0 ? static_cast<float>(column + 1) : 0.0F;
            CHECK(vector.tile_value(1, row * 4U + column) == expected);
        }
    }

    Machine vector_store = make_machine();
    vector_store.reset(kBase);
    put(vector_store, load_store(0x53, 1, 2, true));
    for (std::size_t index = 0; index < 4; ++index) {
        vector_store.set_tile_value(1, index, static_cast<float>(index + 21));
    }
    vector_store.set_pc(kBase);
    vector_store.set_reg(2, kData);
    CHECK(vector_store.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t index = 0; index < 4; ++index) {
        std::uint32_t value = 0;
        REQUIRE(vector_store.read32(kData + static_cast<std::uint32_t>(index * 4U), value));
        CHECK(value == bits(static_cast<float>(index + 21)));
    }
}

TEST_CASE("a strided MLOAD reads four rows from a larger matrix") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, load_store(0x52, 1, 2, false, true, 32));
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            REQUIRE(machine.write32(kData + static_cast<std::uint32_t>(row * 32 + column * 4),
                                    bits(static_cast<float>(row * 10 + column))));
        }
    }
    machine.set_pc(kBase);
    machine.set_reg(2, kData);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            CHECK(machine.tile_value(1, row * 4U + column) ==
                  static_cast<float>(row * 10 + column));
        }
    }
}

TEST_CASE("transform stack push, pop, and drop preserve the parent tile") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, matrix(0x005, 1));
    machine.set_pc(kBase);
    machine.set_tsp(kTransformStack);
    machine.set_tile_value(1, 0, 42.0F);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.tsp() == kTransformStack - 64U);

    put_at(machine, kBase + 4U, matrix(0x006, 2));
    machine.set_pc(kBase + 4U);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.tile_value(2, 0) == 42.0F);
    CHECK(machine.tsp() == kTransformStack);

    machine.set_tsp(kTransformStack);
    put_at(machine, kBase + 8U, matrix(0x007));
    machine.set_pc(kBase + 8U);
    CHECK(machine.step().kind == kr32::core::StepResult::Kind::Retired);
    CHECK(machine.tsp() == kTransformStack + 64U);
}

TEST_CASE("transform stack alignment faults without changing TSP") {
    Machine machine = make_machine();
    machine.reset(kBase);
    put(machine, matrix(0x005, 1));
    machine.set_pc(kBase);
    machine.set_tsp(kTransformStack + 1U);
    const auto result = machine.step();
    CHECK(result.kind == kr32::core::StepResult::Kind::Raised);
    CHECK(result.fault.kind == kr32::core::FaultKind::Alignment);
    CHECK(machine.tsp() == kTransformStack + 1U);
}
