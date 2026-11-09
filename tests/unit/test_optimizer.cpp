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
