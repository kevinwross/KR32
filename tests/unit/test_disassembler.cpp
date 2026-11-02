#include "doctest.h"

#include "kr32/asm/assembler.h"
#include "kr32/dis/disassembler.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::uint8_t> assemble_bytes(std::string_view source) {
    kr32::assembler::Result result;
    kr32::assembler::Error error;
    const bool ok = kr32::assembler::assemble(
        source, kr32::assembler::Options{}, result, error);
    REQUIRE_MESSAGE(ok, error.message);
    return result.bytes;
}

std::string disassemble_text(std::string_view source) {
    const std::vector<std::uint8_t> bytes = assemble_bytes(source);
    kr32::disassembler::Options options;
    options.base = 0x0001'0000;
    return kr32::disassembler::disassemble(bytes, options);
}

} // namespace

TEST_CASE("the disassembler prints the base instruction forms") {
    CHECK(disassemble_text("MOV R1, #42\n").find("MOV R1, #42") != std::string::npos);
    CHECK(disassemble_text("LOAD R4, [R5 + 8]\n").find("LOAD R4, [R5 + 8]") !=
          std::string::npos);
    CHECK(disassemble_text("JZ R7\n").find("JZ R7") != std::string::npos);
    CHECK(disassemble_text("ADD R3, R1, R2\n").find("ADD R3, R1, R2") !=
          std::string::npos);
}

TEST_CASE("the disassembler shows the extension word target") {
    const std::string text = disassemble_text("JMP 0x00400000\n");
    CHECK(text.find("JMP 0x00400000") != std::string::npos);
}

TEST_CASE("invalid instructions are shown as data") {
    std::vector<std::uint8_t> bytes = {
        0xFF, 0xFF, 0xFF, 0xFF,
    };
    const std::string text = kr32::disassembler::disassemble(
        bytes, kr32::disassembler::Options{});
    CHECK(text.find(".word") != std::string::npos);
}

TEST_CASE("the disassembler can use symbols for branch targets") {
    kr32::assembler::Result result;
    kr32::assembler::Error error;
    REQUIRE(kr32::assembler::assemble(
        "start: MOV R1, #1\n"
        "       JMP start\n",
        kr32::assembler::Options{}, result, error));

    kr32::disassembler::Options options;
    options.base = result.base;
    for (const kr32::assembler::Result::Symbol& symbol : result.symbols) {
        options.symbols.emplace_back(symbol.name, symbol.address);
    }
    const std::string text = kr32::disassembler::disassemble(result.bytes, options);
    CHECK(text.find("JMP start") != std::string::npos);
}

TEST_CASE("the base opcode set round-trips through the assembler and disassembler") {
    struct Case {
        const char* source;
        const char* mnemonic;
    };

    const Case cases[] = {
        {"MOV R1, R2\n", "MOV"},
        {"LOAD R1, [R2]\n", "LOAD"},
        {"STORE R1, [R2]\n", "STORE"},
        {"LOAD8U R1, [R2]\n", "LOAD8U"},
        {"LOAD8S R1, [R2]\n", "LOAD8S"},
        {"LOAD16U R1, [R2]\n", "LOAD16U"},
        {"LOAD16S R1, [R2]\n", "LOAD16S"},
        {"STORE8 R1, [R2]\n", "STORE8"},
        {"STORE16 R1, [R2]\n", "STORE16"},
        {"LEA R1, [R2 + 4]\n", "LEA"},
        {"LOAD R1, [R2 + R3]\n", "LOADX"},
        {"STORE R1, [R2 + R3]\n", "STOREX"},
        {"LOAD8U R1, [R2 + R3]\n", "LOAD8UX"},
        {"LOAD8S R1, [R2 + R3]\n", "LOAD8SX"},
        {"LOAD16U R1, [R2 + R3]\n", "LOAD16UX"},
        {"LOAD16S R1, [R2 + R3]\n", "LOAD16SX"},
        {"STORE8 R1, [R2 + R3]\n", "STORE8X"},
        {"STORE16 R1, [R2 + R3]\n", "STORE16X"},        {"SLT R1, R2, R3\n", "SLT"},
        {"SLTU R1, R2, R3\n", "SLTU"},
        {"ADD R1, R2, R3\n", "ADD"},
        {"SUB R1, R2, R3\n", "SUB"},
        {"MUL R1, R2, R3\n", "MUL"},
        {"MULH R1, R2, R3\n", "MULH"},
        {"MULHU R1, R2, R3\n", "MULHU"},
        {"DIV R1, R2, R3\n", "DIV"},
        {"DIVS R1, R2, R3\n", "DIVS"},
        {"MOD R1, R2, R3\n", "MOD"},
        {"MODS R1, R2, R3\n", "MODS"},
        {"CMP R1, R2\n", "CMP"},
        {"AND R1, R2, R3\n", "AND"},
        {"OR R1, R2, R3\n", "OR"},
        {"XOR R1, R2, R3\n", "XOR"},
        {"NOT R1, R2\n", "NOT"},
        {"NEG R1, R2\n", "NEG"},
        {"SHL R1, R2, R3\n", "SHL"},
        {"SHR R1, R2, R3\n", "SHR"},
        {"SAR R1, R2, R3\n", "SAR"},
        {"ROL R1, R2, R3\n", "ROL"},
        {"ROR R1, R2, R3\n", "ROR"},
        {"JMP R1\n", "JMP"},
        {"JZ R1\n", "JZ"},
        {"CALL R1\n", "CALL"},
        {"RET\n", "RET"},
        {"PUSH R1\n", "PUSH"},
        {"POP R1\n", "POP"},
        {"ADJSP #4\n", "ADJSP"},
        {"INT #1\n", "INT"},
        {"SYSCALL #2\n", "SYSCALL"},
        {"IRET\n", "IRET"},
        {"LL R1, [R2]\n", "LL"},
        {"SC R1, [R2]\n", "SC"},
        {"FENCE\n", "FENCE"},
        {"RDCR R1, CR3\n", "RDCR"},
        {"WRCR CR3, R1\n", "WRCR"},
        {"RDSP R1\n", "RDSP"},
        {"WRSP R1\n", "WRSP"},
        {"HALT\n", "HALT"},
        {"BRK\n", "BRK"},
        {"NOP\n", "NOP"},
        {"FADD.S F1, F2, F3\n", "FADD.S"},
        {"FADD.D F1, F2, F3\n", "FADD.D"},
        {"FSQRT.S F1, F2\n", "FSQRT.S"},
        {"FEQ.S R1, F2, F3\n", "FEQ.S"},
        {"FCVT.W.S R1, F2\n", "FCVT.W.S"},
        {"FCVT.D.W F1, R2\n", "FCVT.D.W"},
        {"FMV.W.X F1, R2\n", "FMV.W.X"},
        {"FLOAD.W F1, [R2]\n", "FLOAD.W"},
        {"FSTORE.D F1, [R2]\n", "FSTORE.D"},
        {"BSWAP R1, R2\n", "BSWAP"},
        {"CLZ R1, R2\n", "CLZ"},
        {"CTZ R1, R2\n", "CTZ"},
        {"POPCNT R1, R2\n", "POPCNT"},
        {"BFX R1, R2, #4, #8\n", "BFX"},
        {"BFI R1, R2, #2, #6\n", "BFI"},
        {"MMUL T1, T2, T3\n", "MMUL"},
        {"MVEC T1, T2, T3\n", "MVEC"},
        {"MTRANS T1, T2\n", "MTRANS"},
        {"VDOT F1, T2, T3\n", "VDOT"},
        {"MPUSH T1\n", "MPUSH"},
        {"MPOP T1\n", "MPOP"},
        {"MDROP\n", "MDROP"},
        {"MLOAD T1, [R2]\n", "MLOAD"},
        {"VLOAD T1, [R2]\n", "VLOAD"},
        {"MSTORE T1, [R2]\n", "MSTORE"},
        {"VSTORE T1, [R2]\n", "VSTORE"},
        {"MLOAD T1, [R2], #32\n", "MLOAD"},
    };

    for (const Case& test : cases) {
        const std::vector<std::uint8_t> bytes = assemble_bytes(test.source);
        const std::string text = kr32::disassembler::disassemble(
            bytes, kr32::disassembler::Options{});
        CHECK_MESSAGE(text.find(test.mnemonic) != std::string::npos,
                      test.source, " -> ", text);
    }
}

