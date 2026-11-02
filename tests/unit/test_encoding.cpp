#include "doctest.h"

#include "kr32/isa/encoding.h"

#include <cstdint>

using kr32::isa::Encoded;
using kr32::isa::Instruction;
using kr32::isa::decode;
using kr32::isa::encode;

namespace {

Instruction make(std::uint8_t opcode,
                 std::uint8_t rd = 0,
                 std::uint8_t rs1 = 0,
                 std::uint8_t field = 0,
                 bool extension = false,
                 bool literal = false,
                 std::int16_t immediate = 0,
                 std::uint32_t extension_word = 0) {
    Instruction instruction{};
    instruction.opcode = opcode;
    instruction.rd = rd;
    instruction.rs1 = rs1;
    instruction.field = field;
    instruction.extension = extension;
    instruction.literal = literal;
    instruction.immediate = immediate;
    instruction.extension_word = extension_word;
    return instruction;
}

void expect_encoding(const Instruction& instruction,
                     std::uint32_t word0,
                     std::uint32_t word1 = 0,
                     std::uint8_t count = 1) {
    Encoded encoded{};
    REQUIRE(encode(instruction, encoded));
    CHECK(encoded.count == count);
    CHECK(encoded.words[0] == word0);
    if (count == 2) {
        CHECK(encoded.words[1] == word1);
    }

    Instruction decoded{};
    REQUIRE(decode(encoded.words, encoded.count, decoded));
    CHECK(decoded.opcode == instruction.opcode);
    CHECK(decoded.rd == instruction.rd);
    CHECK(decoded.rs1 == instruction.rs1);
    CHECK(decoded.field == instruction.field);
    CHECK(decoded.extension == instruction.extension);
    CHECK(decoded.literal == instruction.literal);
    if (!instruction.extension) {
        CHECK(decoded.immediate == instruction.immediate);
    }
}

} // namespace

TEST_CASE("golden base encodings") {
    // Words are pinned in docs/isa-v1.md §12.
    expect_encoding(make(0x00, 1, 0, 0, false, true, 42), 0x0020082A);
    expect_encoding(make(0x20, 3, 1, 2), 0x40624000);
    expect_encoding(make(0x01, 4, 5, 3, false, true, 8), 0x028A6808);
    expect_encoding(make(0x18, 1, 2, 3), 0x30246000);
    expect_encoding(make(0x38, 0, 0, 0, true, true, 0, 0x00400000),
                    0x70001800, 0x00400000, 2);
    expect_encoding(make(0x39, 0, 7, 0), 0x720E0000);
    expect_encoding(make(0x3E, 0, 0, 0, false, true, -16), 0x7C000FF0);
    expect_encoding(make(0x43), 0x86000000);
    expect_encoding(make(0x60, 1, 2, 3), 0xC0246000);
    expect_encoding(make(0x61, 1, 2, 3), 0xC2246000);
    expect_encoding(make(0x62, 1, 2, 0), 0xC4240000);
}

TEST_CASE("rejects out-of-range values") {
    Encoded encoded{};
    CHECK_FALSE(encode(make(0x80), encoded));
    CHECK_FALSE(encode(make(0x00, 16), encoded));
    CHECK_FALSE(encode(make(0x00, 0, 0, 0, false, true, 1024), encoded));
    CHECK_FALSE(encode(make(0x00, 0, 0, 0, false, true, -1025), encoded));
}

TEST_CASE("decode requires the extension word") {
    std::uint32_t word = 0x70001800;
    Instruction instruction{};
    CHECK_FALSE(decode(&word, 1, instruction));

    std::uint32_t words[2] = {0x70001800, 0x00400000};
    REQUIRE(decode(words, 2, instruction));
    CHECK(instruction.extension);
    CHECK(instruction.extension_word == 0x00400000);
}
