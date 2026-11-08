#include "cc_test_support.h"

TEST_CASE("a compiled C program supports goto labels") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int n = 0;\n"
        "    goto start;\n"
        "    n = 99;\n"
        "start:\n"
        "    n = n + 1;\n"
        "    return n;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 1);
}

TEST_CASE("a compiled C program supports switch and default") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int value = 2;\n"
        "    int result = 0;\n"
        "    switch (value) {\n"
        "    case 1: result = 10; break;\n"
        "    case 2: result = 20; break;\n"
        "    default: result = 30;\n"
        "    }\n"
        "    return result;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 20);
}

TEST_CASE("the preprocessor expands object macros and conditionals") {
    const ProgramResult program = compile_and_run(
        "#define ANSWER 42\n"
        "#define UNUSED 9\n"
        "#if 1\n"
        "int main(void) { return ANSWER; }\n"
        "#else\n"
        "int main(void) { return UNUSED; }\n"
        "#endif\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("the preprocessor expands function-like macros") {
    const ProgramResult program = compile_and_run(
        "#define ADD(a, b) ((a) + (b))\n"
        "int main(void) { return ADD(2, 3) + ADD(4, 5); }\n");
    CHECK(program.ok);
    CHECK(program.result == 14);
}

TEST_CASE("the preprocessor expands variadic macros") {
    const ProgramResult program = compile_and_run(
        "#define SUM(a, ...) ((a) + (__VA_ARGS__))\n"
        "#define REST(...) (__VA_ARGS__)\n"
        "int main(void) {\n"
        "    if (SUM(1, 2) != 3) return 1;\n"
        "    if (REST(4) != 4) return 2;\n"
        "    if (REST(5, 6) != 6) return 3;\n"
        "    int value = (7, 8);\n"
        "    if (value != 8) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("Casts truncate to char and normalize _Bool") {
    const ProgramResult character = compile_and_run(
        "int main(void) { return (char)0x141; }\n");
    CHECK(character.ok);
    CHECK(character.result == static_cast<std::uint32_t>(0x41));

    const ProgramResult boolean = compile_and_run(
        "int main(void) { _Bool value = (_Bool)7; return value; }\n");
    CHECK(boolean.ok);
    CHECK(boolean.result == 1);
}

TEST_CASE("the runtime provides memory and string helpers") {
    const ProgramResult program = compile_and_run(
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "int main(void) {\n"
        "    char *first = malloc(8);\n"
        "    char *second = malloc(8);\n"
        "    memset(first, 0, 8);\n"
        "    first[0] = 'A';\n"
        "    first[1] = 'B';\n"
        "    first[2] = 0;\n"
        "    memcpy(second, first, 3);\n"
        "    return strlen(second) + second[1];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 68);
}

TEST_CASE("extern globals are declarations, not definitions") {
    const ProgramResult program = compile_and_run(
        "extern int shared;\n"
        "int shared = 7;\n"
        "int main(void) { return shared; }\n");
    CHECK(program.ok);
    CHECK(program.result == 7);
}

TEST_CASE("C character escapes include vertical tab and form feed") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    if ('\\v' != 11) return 1;\n"
        "    if ('\\f' != 12) return 2;\n"
        "    if ('\\a' != 7) return 3;\n"
        "    if ('\\b' != 8) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

