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
    CHECK(assembly.find("MOV R2, R0") == std::string::npos);
    CHECK(assembly.find("CMP R0, R1") != std::string::npos);
}

TEST_CASE("assembly optimizer folds push pop pairs") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    PUSH R0\n"
        "    MOV R0, #0\n"
        "    POP R1\n"
        "    STORE R0, [R1]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("PUSH R0") == std::string::npos);
    CHECK(assembly.find("POP R1") == std::string::npos);
    CHECK(assembly.find("MOV R1, R0") != std::string::npos);
}

TEST_CASE("assembly optimizer forwards frame loads across loops") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R0, #0\n"
        "    STORE R0, [R7 + 0]\n"
        ".Lloop:\n"
        "    LOAD R0, [R7 + 0]\n"
        "    CMP R0, #4\n"
        "    JCS .Ldone\n"
        "    ADD R0, R0, #1\n"
        "    STORE R0, [R7 + 0]\n"
        "    JMP .Lloop\n"
        ".Ldone:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R0, [R7 + 0]") == std::string::npos);
}

TEST_CASE("assembly optimizer forwards frame slot zero") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    STORE R0, [R7]\n"
        "    LOAD R0, [R7]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R0, [R7]") == std::string::npos);
}

TEST_CASE("assembly optimizer removes jumps across labels") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    JMP .Ltarget\n"
        ".Lother:\n"
        ".Ltarget:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("JMP .Ltarget") == std::string::npos);
}

TEST_CASE("assembly optimizer strength-reduces immediates") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MUL R0, R1, #4\n"
        "    DIV R2, R3, #8\n"
        "    MOD R4, R5, #8\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.rewritten >= 3);
    CHECK(assembly.find("SHL R0, R1, #2") != std::string::npos);
    CHECK(assembly.find("SHR R2, R3, #3") != std::string::npos);
    CHECK(assembly.find("AND R4, R5, #7") != std::string::npos);
}

TEST_CASE("assembly optimizer removes dead local functions") {
    std::string assembly =
        ".text\n"
        ".global main\n"
        "main:\n"
        "    RET\n"
        "unused_local:\n"
        "    RET\n"
        ".debug_info\n"
        "    .asciz \"F unused_local unused_local .Lend_unused 0 int\"\n"
        "    .asciz \"V unused_local x 0 int 4\"\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("unused_local:") == std::string::npos);
    CHECK(assembly.find("F unused_local") == std::string::npos);
    CHECK(assembly.find("V unused_local") == std::string::npos);
}

TEST_CASE("assembly optimizer folds comparison branches") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    SLT R0, R1, R2\n"
        "    CMP R0, #0\n"
        "    JZ .Ldone\n"
        "    XOR R0, R1, R2\n"
        "    SLTU R0, R0, #1\n"
        "    CMP R0, #0\n"
        "    JNZ .Ldone\n"
        ".Ldone:\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("SLT R0") == std::string::npos);
    CHECK(assembly.find("CMP R1, R2") != std::string::npos);
    CHECK(assembly.find("JGE .Ldone") != std::string::npos);
    CHECK(assembly.find("JZ .Ldone") != std::string::npos);
}

TEST_CASE("assembly optimizer promotes leaf locals") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    PUSH R7\n"
        "    ADJSP #-8\n"
        "    LEA R7, [SP]\n"
        "    STORE R0, [R7 + 4]\n"
        "    MOV R0, #9\n"
        "    LOAD R1, [R7 + 4]\n"
        "    ADD R0, R1, R0\n"
        "    MOV R1, #0\n"
        "    LOAD R2, [R7 + 4]\n"
        "    ADD R0, R0, R2\n"
        "    ADJSP #8\n"
        "    POP R7\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    // The parameter is promoted into R12 and the frame slot disappears.
    CHECK(assembly.find("MOV R12, R0") != std::string::npos);
    CHECK(assembly.find("[R7 + 4]") == std::string::npos);
}

TEST_CASE("assembly optimizer uses CFG liveness for dead stores") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    STORE R0, [R7 + 0]\n"
        "    JZ .Lchoose\n"
        "    STORE R1, [R7 + 0]\n"
        "    JMP .Ldone\n"
        ".Lchoose:\n"
        "    STORE R2, [R7 + 0]\n"
        ".Ldone:\n"
        "    LOAD R0, [R7 + 0]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("STORE R0, [R7 + 0]") == std::string::npos);
    CHECK(assembly.find("STORE R1, [R7 + 0]") != std::string::npos);
    CHECK(assembly.find("STORE R2, [R7 + 0]") != std::string::npos);
}

TEST_CASE("assembly optimizer uses CFG liveness for dead moves") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R1, #1\n"
        "    JZ .Lchoose\n"
        "    MOV R1, #2\n"
        "    JMP .Ldone\n"
        ".Lchoose:\n"
        "    MOV R1, #3\n"
        ".Ldone:\n"
        "    CMP R1, #0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("MOV R1, #1") == std::string::npos);
    CHECK(assembly.find("MOV R1, #2") != std::string::npos);
    CHECK(assembly.find("MOV R1, #3") != std::string::npos);
}

TEST_CASE("assembly optimizer folds load copies") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R0, [R7 + 4]\n"
        "    MOV R1, R0\n"
        "    LOAD R0, [R7 + 8]\n"
        "    RETW\n";
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

TEST_CASE("assembly optimizer folds a symbol offset into the address") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [state]\n"
        "    ADD R0, R0, #4\n"
        "    LOAD R0, [R0]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LEA R0, [state + 4]") != std::string::npos);
    CHECK(assembly.find("ADD R0, R0, #4") == std::string::npos);
}

TEST_CASE("assembly optimizer retargets an address move to the store register") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [state]\n"
        "    ADD R0, R0, #4\n"
        "    MOV R1, R0\n"
        "    MOV R0, #1\n"
        "    STORE R0, [R1]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    // The absolute form wins over materialising the address in a register.
    CHECK(assembly.find("STORE R0, [state + 4]") != std::string::npos);
    CHECK(assembly.find("MOV R1, R0") == std::string::npos);
}

TEST_CASE("assembly optimizer retargets a symbol address move") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [state]\n"
        "    MOV R1, R0\n"
        "    LEA R0, [other]\n"
        "    STORE R0, [R1]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("STORE R0, [state]") != std::string::npos);
    CHECK(assembly.find("MOV R1, R0") == std::string::npos);
}

TEST_CASE("assembly optimizer folds addresses across line labels") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        ".Lline_1:\n"
        "    LEA R0, [state]\n"
        "    ADD R0, R0, #4\n"
        "    MOV R1, R0\n"
        "    MOV R0, #1\n"
        "    STORE R0, [R1]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    INFO(assembly);
    CHECK(assembly.find("STORE R0, [state + 4]") != std::string::npos);
    CHECK(assembly.find("MOV R1, R0") == std::string::npos);
}

TEST_CASE("assembly optimizer stores through a symbol address") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [state + 4]\n"
        "    MOV R1, #1\n"
        "    STORE R1, [R0]\n"
        "    MOV R0, #0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("STOR") != std::string::npos);
    CHECK(assembly.find("E R1, [state + 4]") != std::string::npos);
    CHECK(assembly.find("LEA R0, [state + 4]") == std::string::npos);
}

TEST_CASE("assembly optimizer loads through a symbol address") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R2, [tick]\n"
        "    LOAD R0, [R2]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R0, [tick]") != std::string::npos);
    CHECK(assembly.find("LEA R2, [tick]") == std::string::npos);
}

TEST_CASE("assembly optimizer keeps a lea before a jumped-to label") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LEA R0, [state]\n"
        ".Ltarget:\n"
        "    MOV R1, #1\n"
        "    STORE R1, [R0]\n"
        "    RET\n"
        "other:\n"
        "    JMP .Ltarget\n";
    std::string original = assembly;
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(assembly.find("STORE R1, [state]") == std::string::npos);
    (void)stats;
}

TEST_CASE("assembly optimizer folds a negated constant") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R0, #1\n"
        "    NEG R0, R0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("MOV R0, #-1") != std::string::npos);
    CHECK(assembly.find("NEG R0, R0") == std::string::npos);
}

TEST_CASE("assembly optimizer drops a frame prologue that is never used") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    PUSH R7\n"
        "    LEA R7, [SP]\n"
        "    MOV R0, #1\n"
        "    STORE R0, [state]\n"
        "    POP R7\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("PUSH R7") == std::string::npos);
    CHECK(assembly.find("LEA R7, [SP]") == std::string::npos);
    CHECK(assembly.find("STORE R0, [state]") != std::string::npos);
}

TEST_CASE("assembly optimizer keeps a frame prologue that is used") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    PUSH R7\n"
        "    LEA R7, [SP]\n"
        "    ADJSP #-8\n"
        "    MOV R0, #1\n"
        "    STORE R0, [R7 + 0]\n"
        "    LEA R0, [R7 + 0]\n"
        "    CALL use\n"
        "    ADJSP #8\n"
        "    POP R7\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    (void)stats;
    CHECK(assembly.find("PUSH R7") != std::string::npos);
    CHECK(assembly.find("LEA R0, [R7 + 0]") != std::string::npos);
}

TEST_CASE("assembly optimizer keeps a stack temporary in a register") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R0, [R7 + 4]\n"
        "    ADJSP #-4\n"
        "    STORE R0, [SP]\n"
        "    LEA R1, [array]\n"
        "    MOV R0, R12\n"
        "    SHL R0, R0, #2\n"
        "    ADD R0, R1, R0\n"
        "    LOAD R0, [R0]\n"
        "    LOAD R1, [SP]\n"
        "    ADJSP #4\n"
        "    ADD R0, R1, R0\n"
        "    STORE R0, [R7 + 4]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    INFO(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("ADJSP #-4") == std::string::npos);
    CHECK(assembly.find("STORE R0, [SP]") == std::string::npos);
}

TEST_CASE("assembly optimizer drops an overwritten frame load") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R2, [R7 + 4]\n"
        ".Lline_1:\n"
        "    MOV R2, #0\n"
        "    STORE R2, [R7 + 8]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    INFO(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("LOAD R2, [R7 + 4]") == std::string::npos);
}

TEST_CASE("assembly optimizer keeps a frame load that is read") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R2, [R7 + 4]\n"
        ".Lline_1:\n"
        "    ADD R0, R2, R0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    (void)stats;
    CHECK(assembly.find("LOAD R2, [R7 + 4]") != std::string::npos);
}

TEST_CASE("assembly optimizer stores through a static symbol address") {
    std::string assembly =
        ".text\n"
        ".global srand\n"
        "srand:\n"
        "    PUSH R7\n"
        "    ADJSP #-4\n"
        "    LEA R7, [SP]\n"
        ".Lline_1:\n"
        "    LEA R1, [rand_state]\n"
        "    STORE R0, [R1]\n"
        "    MOV R0, #0\n"
        ".Lreturn_0:\n"
        "    ADJSP #4\n"
        "    POP R7\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    INFO(assembly);
    (void)stats;
    CHECK(assembly.find("STORE R0, [rand_state]") != std::string::npos);
}

TEST_CASE("assembly optimizer drops a truncation before a narrow store") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R0, [R7 + 4]\n"
        "    AND R0, R0, #255\n"
        "    SHL R0, R0, #24\n"
        "    SAR R0, R0, #24\n"
        "    STORE8 R0, [R7 + 8]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    INFO(assembly);
    CHECK(stats.removed >= 1);
    CHECK(assembly.find("STORE8 R0, [R7 + 8]") != std::string::npos);
    CHECK(assembly.find("AND R0, R0, #255") == std::string::npos);
    CHECK(assembly.find("SAR R0, R0, #24") == std::string::npos);
}

TEST_CASE("assembly optimizer keeps a truncation before a full store") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    LOAD R0, [R7 + 4]\n"
        "    AND R0, R0, #255\n"
        "    SHL R0, R0, #24\n"
        "    SAR R0, R0, #24\n"
        "    STORE R0, [R7 + 8]\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    (void)stats;
    CHECK(assembly.find("SAR R0, R0, #24") != std::string::npos);
}

TEST_CASE("assembly optimizer folds a copy chain into the consumer") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R0, R13\n"
        "    MOV R2, R0\n"
        "    MOV R0, R12\n"
        "    ADD R0, R2, R0\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    INFO(assembly);
    CHECK(stats.removed >= 2);
    CHECK(assembly.find("ADD R0, R13, R12") != std::string::npos);
    CHECK(assembly.find("MOV R2, R0") == std::string::npos);
}

TEST_CASE("assembly optimizer keeps a copy read twice") {
    std::string assembly =
        ".text\n"
        ".global probe\n"
        "probe:\n"
        "    MOV R0, R13\n"
        "    ADD R2, R0, #1\n"
        "    ADD R3, R0, #2\n"
        "    RET\n";
    const assembler::OptimizeStats stats =
        assembler::optimize_assembly(assembly);
    (void)stats;
    CHECK(assembly.find("MOV R0, R13") != std::string::npos);
}
