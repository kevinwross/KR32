#include "doctest.h"

#include "kr32/asm/assembler.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint32_t word_at(const std::vector<std::uint8_t>& bytes, std::size_t offset = 0) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

kr32::assembler::Result assemble_ok(std::string_view source,
                                    std::uint32_t base = 0x0001'0000) {
    kr32::assembler::Result result;
    kr32::assembler::Error error;
    const bool ok = kr32::assembler::assemble(
        source, kr32::assembler::Options{base}, result, error);
    REQUIRE_MESSAGE(ok, error.message);
    return result;
}

} // namespace

TEST_CASE("the assembler emits the golden MOV and memory encodings") {
    const auto mov = assemble_ok("MOV R1, #42\n");
    REQUIRE(mov.bytes.size() == 4);
    CHECK(word_at(mov.bytes) == 0x0020'082A);

    const auto load = assemble_ok("LOAD R4, [R5 + 8]\n");
    REQUIRE(load.bytes.size() == 4);
    CHECK(word_at(load.bytes) == 0x028A'6808);

    const auto branch = assemble_ok("JZ R7\n");
    REQUIRE(branch.bytes.size() == 4);
    CHECK(word_at(branch.bytes) == 0x720E'0000);

    const auto add = assemble_ok("FADD.S F1, F2, F3\n");
    REQUIRE(add.bytes.size() == 4);
    CHECK(word_at(add.bytes) == 0xC024'6000);
}

TEST_CASE("labels and absolute jump targets use an extension word") {
    const auto result = assemble_ok(
        "start: MOV R1, #1\n"
        "       JMP start\n");
    REQUIRE(result.bytes.size() == 12);
    CHECK(word_at(result.bytes, 0) == 0x0020'0801);
    CHECK(word_at(result.bytes, 4) == 0x7000'1800);
    CHECK(word_at(result.bytes, 8) == 0x0001'0000);
}

TEST_CASE("the assembler emits FP, atomic, and bit escape encodings") {
    const auto add = assemble_ok("FADD.S F1, F2, F3\n");
    REQUIRE(add.bytes.size() == 4);
    CHECK(word_at(add.bytes) == 0xC024'6000);

    const auto bits = assemble_ok(
        "BSWAP R1, R2\n"
        "CLZ R3, R4\n"
        "CTZ R5, R6\n"
        "POPCNT R7, R8\n"
        "BFX R9, R10, #4, #8\n"
        "BFI R11, R12, #2, #6\n");
    REQUIRE(bits.bytes.size() == 24);
    CHECK(word_at(bits.bytes, 0) == 0xC424'0000);
    CHECK(word_at(bits.bytes, 4) == 0xC468'0060);
    CHECK(word_at(bits.bytes, 8) == 0xC4AC'0080);
    CHECK(word_at(bits.bytes, 12) == 0xC4F0'00A0);
    CHECK(word_at(bits.bytes, 16) == 0xC534'E024);
    CHECK(word_at(bits.bytes, 20) == 0xC578'A042);

    const auto atomic = assemble_ok(
        "LL R1, [R2]\n"
        "SC R3, [R4]\n"
        "FENCE\n");
    REQUIRE(atomic.bytes.size() == 12);
    CHECK(word_at(atomic.bytes, 0) == 0x8C24'4800);
    CHECK(word_at(atomic.bytes, 4) == 0x8E68'4800);
    CHECK(word_at(atomic.bytes, 8) == 0x9000'0000);
}

TEST_CASE("directives emit bytes, words, strings, and alignment") {
    const auto words = assemble_ok(".word 0x12345678\n");
    REQUIRE(words.bytes.size() == 4);
    CHECK(word_at(words.bytes) == 0x1234'5678);

    const auto floats = assemble_ok(".float 1.5, -2.25\n");
    REQUIRE(floats.bytes.size() == 8);
    CHECK(word_at(floats.bytes, 0) == 0x3FC0'0000);
    CHECK(word_at(floats.bytes, 4) == 0xC010'0000);

    const auto doubles = assemble_ok(".double 1.5\n");
    REQUIRE(doubles.bytes.size() == 8);
    CHECK(word_at(doubles.bytes, 0) == 0x0000'0000);
    CHECK(word_at(doubles.bytes, 4) == 0x3FF8'0000);

    const auto bytes = assemble_ok(".byte 1, 2, 0xFF\n");
    REQUIRE(bytes.bytes.size() == 3);
    CHECK(bytes.bytes[0] == 1);
    CHECK(bytes.bytes[1] == 2);
    CHECK(bytes.bytes[2] == 0xFF);

    const auto string = assemble_ok(".asciz \"hi\"\n");
    REQUIRE(string.bytes.size() == 3);
    CHECK(string.bytes[0] == 'h');
    CHECK(string.bytes[1] == 'i');
    CHECK(string.bytes[2] == 0);

    const auto aligned = assemble_ok(".byte 1\n.align 4\n.byte 2\n");
    REQUIRE(aligned.bytes.size() == 5);
    CHECK(aligned.bytes[0] == 1);
    CHECK(aligned.bytes[1] == 0);
    CHECK(aligned.bytes[2] == 0);
    CHECK(aligned.bytes[3] == 0);
    CHECK(aligned.bytes[4] == 2);
}

TEST_CASE("org leaves a zero-filled gap") {
    const auto result = assemble_ok(".org 0x10010\n.word 0xDEADBEEF\n");
    REQUIRE(result.bytes.size() == 0x14);
    for (std::size_t index = 0; index < 0x10; ++index) {
        CHECK(result.bytes[index] == 0);
    }
    CHECK(word_at(result.bytes, 0x10) == 0xDEAD'BEEF);
}

TEST_CASE("equ constants can be used as immediates") {
    const auto result = assemble_ok(
        ".equ VALUE, 7\n"
        "MOV R1, #VALUE\n");
    REQUIRE(result.bytes.size() == 8);
    CHECK(word_at(result.bytes, 0) == 0x0020'1800);
    CHECK(word_at(result.bytes, 4) == 7);
}

TEST_CASE("undefined symbols and duplicate labels are reported") {
    kr32::assembler::Result result;
    kr32::assembler::Error error;
    CHECK_FALSE(kr32::assembler::assemble(
        "JMP nowhere\n", kr32::assembler::Options{}, result, error));
    CHECK(error.line == 1);

    CHECK_FALSE(kr32::assembler::assemble(
        "start:\nstart:\n", kr32::assembler::Options{}, result, error));
    CHECK(error.line == 2);
}
