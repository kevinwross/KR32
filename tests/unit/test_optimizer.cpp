#include "doctest.h"

#include "kr32/asm/optimizer.h"

#include <string>

using namespace kr32;

TEST_CASE("assembly optimizer forwards frame slots") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    STORE R0, [R7 + 4]\n"
        "    LOAD R2, [R7 + 4]\n"
        "    CMP R2, R1\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.rewritten >= 1);
    CHECK(assembly.find("LOAD R2") == std::string::npos);
    CHECK(assembly.find("MOV R2, R0") != std::string::npos);
}

TEST_CASE("assembly optimizer folds load copies") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R0, [R7 + 4]\n"
        "    MOV R1, R0\n"
        "    LOAD R0, [R7 + 8]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R1, [R7 + 4]") != std::string::npos);
    CHECK(assembly.find("MOV R1, R0") == std::string::npos);
}

TEST_CASE("assembly optimizer folds add immediate chains") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    ADD R0, R0, #4\n"
        "    ADD R0, R0, #8\n"
        "    SUB R0, R0, #2\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("ADD R0, R0, #10") != std::string::npos);
    CHECK(assembly.find("ADD R0, R0, #8") == std::string::npos);
}

TEST_CASE("assembly optimizer folds symbol addresses") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [counter]\n"
        "    LOAD R0, [R0]\n"
        "    LEA R1, [counter]\n"
        "    STORE R0, [R1]\n"
        "    MOV R1, #0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("LOAD R0, [counter]") != std::string::npos);
    CHECK(assembly.find("STORE R0, [counter]") != std::string::npos);
    CHECK(assembly.find("LEA R0, [counter]") == std::string::npos);
    CHECK(assembly.find("LEA R1, [counter]") == std::string::npos);
}

TEST_CASE("assembly optimizer folds pointer load offsets") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    ADD R0, R0, #4\n"
        "    LOAD R0, [R0]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R0, [R0 + 4]") != std::string::npos);
    CHECK(assembly.find("ADD R0, R0, #4") == std::string::npos);
}

TEST_CASE("assembly optimizer folds comparison values") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    CMP R0, R1\n"
        "    JLT .Ltrue_1\n"
        "    MOV R0, #0\n"
        "    JMP .Lcmpend_2\n"
        ".Ltrue_1:\n"
        "    MOV R0, #1\n"
        ".Lcmpend_2:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 5);
    CHECK(assembly.find("SLT R0, R0, R1") != std::string::npos);
    CHECK(assembly.find("JLT .Ltrue_1") == std::string::npos);
}

TEST_CASE("assembly optimizer removes repeated frame loads") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R0, [R7 + 4]\n"
        "    CMP R0, #0\n"
        "    JZ .Lkeep\n"
        "    LOAD R0, [R7 + 4]\n"
        ".Lkeep:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R0, [R7 + 4]") ==
          assembly.rfind("LOAD R0, [R7 + 4]"));
}

TEST_CASE("assembly optimizer removes unreachable instructions") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    JMP .Ldone\n"
        "    MOV R0, #0\n"
        ".Ldone:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("MOV R0, #0") == std::string::npos);
}

TEST_CASE("assembly optimizer folds pointer store offsets") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    ADD R0, R0, #4\n"
        "    STORE R1, [R0]\n"
        "    MOV R0, #0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("STORE R1, [R0 + 4]") != std::string::npos);
    CHECK(assembly.find("ADD R0, R0, #4") == std::string::npos);
}

TEST_CASE("assembly optimizer removes zero frame adjustments") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    ADJSP #-0\n"
        "    RET\n"
        "    ADJSP #0\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("ADJSP #-0") == std::string::npos);
    CHECK(assembly.find("ADJSP #0") == std::string::npos);
}

TEST_CASE("assembly optimizer folds frame address arithmetic") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [R7 + 4]\n"
        "    ADD R0, R0, #8\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LEA R0, [R7 + 12]") != std::string::npos);
    CHECK(assembly.find("ADD R0, R0, #8") == std::string::npos);
}

TEST_CASE("assembly optimizer folds frame addresses into loads") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [R7 + 4]\n"
        "    LOAD R0, [R0]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LEA R0, [R7 + 4]") == std::string::npos);
    CHECK(assembly.find("LOAD R0, [R7 + 4]") != std::string::npos);
}

TEST_CASE("assembly optimizer folds boolean branches") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    CMP R0, #1\n"
        "    JZ .Ltrue_1\n"
        "    MOV R0, #0\n"
        "    JMP .Lcmpend_2\n"
        ".Ltrue_1:\n"
        "    MOV R0, #1\n"
        ".Lcmpend_2:\n"
        "    CMP R0, #0\n"
        "    JZ .Lelse_3\n"
        ".Lelse_3:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 7);
    CHECK(assembly.find("JNZ .Lelse_3") != std::string::npos);
    CHECK(assembly.find("MOV R0, #0") == std::string::npos);
    CHECK(assembly.find(".Lcmpend_2") == std::string::npos);
}

TEST_CASE("assembly optimizer removes dead frame stores") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    STORE R0, [R7 + 4]\n"
        "    LEA R0, [R7 + 8]\n"
        "    STORE R1, [R7 + 8]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("STORE R0, [R7 + 4]") == std::string::npos);
    CHECK(assembly.find("STORE R1, [R7 + 8]") != std::string::npos);
}

TEST_CASE("assembly optimizer folds immediate comparisons") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R4, #1\n"
        "    MOV R5, R4\n"
        "    CMP R3, R5\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("CMP R3, #1") != std::string::npos);
    CHECK(assembly.find("MOV R5, R4") == std::string::npos);
}

TEST_CASE("assembly optimizer removes overwritten and no-op moves") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R0, R0\n"
        "    MOV R0, #2\n"
        "    LOAD R0, [R7 + 8]\n"
        "    JMP .Lnext\n"
        ".Lnext:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("MOV R0, R0") == std::string::npos);
    CHECK(assembly.find("MOV R0, #2") == std::string::npos);
    CHECK(assembly.find("JMP .Lnext") == std::string::npos);
}
