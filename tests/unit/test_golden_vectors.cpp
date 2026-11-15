#include "doctest.h"

#include "kr32/isa/encoding.h"

#include <cstdint>
#include <vector>

// These checks rebuild the encoding from docs/isa-v1.md §2.1 rather than from
// the implementation's constants, so a drift between the document and
// include/kr32/isa/encoding.h shows up here first.

namespace {

using kr32::isa::Encoded;
using kr32::isa::Instruction;
using kr32::isa::decode;
using kr32::isa::encode;

constexpr std::uint32_t kOpcodeShift = 25;
constexpr std::uint32_t kRdShift = 21;
constexpr std::uint32_t kRs1Shift = 17;
constexpr std::uint32_t kFieldShift = 13;
constexpr std::uint32_t kExtensionBit = 1U << 12;
constexpr std::uint32_t kLiteralBit = 1U << 11;
constexpr std::uint32_t kInlineMask = 0x7FFU;

// §2.1: opcode 31..25, Rd 24..21, Rs1 20..17, Rs2/mode/condition 16..13,
// E bit 12, L bit 11, inline value 10..0.
std::uint32_t golden_word(std::uint8_t opcode, std::uint8_t rd,
                          std::uint8_t rs1, std::uint8_t field, bool extension,
                          bool literal, std::int16_t inline_value) {
    const std::uint32_t word =
        (static_cast<std::uint32_t>(opcode) << kOpcodeShift) |
        (static_cast<std::uint32_t>(rd) << kRdShift) |
        (static_cast<std::uint32_t>(rs1) << kRs1Shift) |
        (static_cast<std::uint32_t>(field) << kFieldShift) |
        (extension ? kExtensionBit : 0U) | (literal ? kLiteralBit : 0U) |
        (static_cast<std::uint32_t>(inline_value) & kInlineMask);
    return word;
}

Instruction make(std::uint8_t opcode, std::uint8_t rd = 0,
                 std::uint8_t rs1 = 0, std::uint8_t field = 0,
                 bool literal = false, std::int16_t immediate = 0) {
    Instruction instruction{};
    instruction.opcode = opcode;
    instruction.rd = rd;
    instruction.rs1 = rs1;
    instruction.field = field;
    instruction.literal = literal;
    instruction.immediate = immediate;
    return instruction;
}

} // namespace

TEST_CASE("every documented opcode encodes from the documented layout") {
    // docs/isa-v1.md §4: base integer, memory extensions, and the three escapes.
    const std::vector<std::uint8_t> opcodes = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x20,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B,
        0x2C, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x33, 0x34, 0x38, 0x39, 0x3A,
        0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
        0x51, 0x52, 0x53, 0x60, 0x61, 0x62,
    };
    REQUIRE(opcodes.size() == 72);

    for (const std::uint8_t opcode : opcodes) {
        const Instruction instruction = make(opcode, 3, 5, 6, true, 0x2A);
        Encoded encoded{};
        REQUIRE(encode(instruction, encoded));
        CHECK(encoded.count == 1);
        CHECK(encoded.words[0] == golden_word(opcode, 3, 5, 6, false, true, 0x2A));

        Instruction back{};
        REQUIRE(decode(encoded.words, encoded.count, back));
        CHECK(back.opcode == opcode);
        CHECK(back.rd == 3);
        CHECK(back.rs1 == 5);
        CHECK(back.field == 6);
        CHECK(back.literal);
        CHECK(back.immediate == 0x2A);
    }
}

TEST_CASE("every documented opcode encodes both widths") {
    const std::vector<std::uint8_t> opcodes = {
        0x00, 0x01, 0x02, 0x09, 0x10, 0x20, 0x29, 0x30, 0x38, 0x3A, 0x3C,
        0x3E, 0x45, 0x46, 0x47, 0x50, 0x51, 0x52, 0x53, 0x60, 0x61, 0x62,
    };
    for (const std::uint8_t opcode : opcodes) {
        Instruction wide = make(opcode, 1, 2, 3, true, 0);
        wide.extension = true;
        wide.extension_word = 0x0040'0000U;
        Encoded encoded{};
        REQUIRE(encode(wide, encoded));
        CHECK(encoded.count == 2);
        CHECK(encoded.words[0] == golden_word(opcode, 1, 2, 3, true, true, 0));
        CHECK(encoded.words[1] == 0x0040'0000U);

        Instruction back{};
        REQUIRE(decode(encoded.words, encoded.count, back));
        CHECK(back.opcode == opcode);
        CHECK(back.extension);
        CHECK(back.extension_word == 0x0040'0000U);
    }
}

TEST_CASE("the golden table matches the documented layout") {
    // docs/isa-v1.md §12, in order. The second word is present when E is set.
    struct Golden {
        std::uint32_t word;
        std::uint32_t extension;
        const char* text;
    };
    const std::vector<Golden> golden = {
        {0x0020'082A, 0, "MOV R1, #42"},
        {0x4062'4000, 0, "ADD R3, R1, R2"},
        {0x3024'6000, 0, "SLT R1, R2, R3"},
        {0x3224'6000, 0, "SLTU R1, R2, R3"},
        {0x028A'6808, 0, "LOAD R4, [R5 + 8]"},
        {0x068A'6808, 0, "LOAD8U R4, [R5 + 8]"},
        {0x04CE'4800, 0, "STORE R6, [R7]"},
        {0x8C24'4800, 0, "LL R1, [R2]"},
        {0x8E24'4800, 0, "SC R1, [R2]"},
        {0x7000'1800, 0x0040'0000, "JMP 0x0040_0000"},
        {0x7400'1800, 0x0040'0000, "CALL 0x0040_0000"},
        {0x720E'0000, 0, "JZ R7"},
        {0x7600'0000, 0, "RET"},
        {0x8600'0000, 0, "HALT"},
        {0x7C00'0FF0, 0, "ADJSP #-16"},
        {0xC024'6000, 0, "FADD.S F1, F2, F3"},
        {0xC06A'0032, 0, "FMOV F3, F5"},
        {0xC224'6000, 0, "MMUL T1, T2, T3"},
        {0xC424'0000, 0, "BSWAP R1, R2"},
        {0x9A00'0000, 0, "PUSHR"},
        {0x9C00'0000, 0, "POPR"},
        {0x9E00'0000, 0, "PUSHFP"},
        {0x1400'0000, 0, "POPFP"},
    };

    for (const Golden& row : golden) {
        const std::uint32_t word = row.word;
        const std::uint8_t opcode =
            static_cast<std::uint8_t>((word >> kOpcodeShift) & 0x7FU);
        const std::uint8_t rd =
            static_cast<std::uint8_t>((word >> kRdShift) & 0xFU);
        const std::uint8_t rs1 =
            static_cast<std::uint8_t>((word >> kRs1Shift) & 0xFU);
        const std::uint8_t field =
            static_cast<std::uint8_t>((word >> kFieldShift) & 0xFU);
        const bool extension = (word & kExtensionBit) != 0U;
        const bool literal = (word & kLiteralBit) != 0U;
        // The inline field is signed 11-bit (§2.1), so bit 10 is the sign.
        std::int16_t inline_value =
            static_cast<std::int16_t>(word & kInlineMask);
        if ((inline_value & 0x400) != 0) {
            inline_value = static_cast<std::int16_t>(inline_value | 0xF800);
        }

        const std::uint32_t rebuilt =
            golden_word(opcode, rd, rs1, field, extension, literal,
                        inline_value);
        INFO(row.text);
        CHECK(rebuilt == word);
        CHECK(extension == (row.extension != 0U));
        if (row.extension != 0U) {
            CHECK(row.extension == 0x0040'0000U);
        }

        // The implementation agrees with the document for the same fields.
        Instruction instruction{};
        instruction.opcode = opcode;
        instruction.rd = rd;
        instruction.rs1 = rs1;
        instruction.field = field;
        instruction.literal = literal;
        if (extension) {
            instruction.extension = true;
            instruction.extension_word = row.extension;
        } else {
            instruction.immediate = inline_value;
        }
        Encoded encoded{};
        REQUIRE(encode(instruction, encoded));
        CHECK(encoded.words[0] == word);
        if (row.extension != 0U) {
            REQUIRE(encoded.count == 2);
            CHECK(encoded.words[1] == row.extension);
        }
    }
}
