#include "cc_test_support.h"

TEST_CASE("a compiled C program calls putchar and exits") {
    const ProgramResult program = compile_and_run(
        "int putchar(int c);\n"
        "void exit(int code);\n"
        "int main(void) {\n"
        "    putchar('H');\n"
        "    putchar('i');\n"
        "    putchar('!');\n"
        "    putchar('\\n');\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.output == "Hi!\n");
    CHECK(program.result == 0);
}

TEST_CASE("typeof derives types from expressions and type names") {
    const ProgramResult program = compile_and_run(
        "struct Point { int x; int y; };\n"
        "int main(void) {\n"
        "    int source = 3;\n"
        "    typeof(source) copy = 4;\n"
        "    typeof(source + 1) added = 5;\n"
        "    typeof(int*) pointer = &source;\n"
        "    typeof(int[3]) values;\n"
        "    typeof(struct Point) point;\n"
        "    values[0] = 6;\n"
        "    point.x = 2;\n"
        "    point.y = 3;\n"
        "    if (copy != 4) return 1;\n"
        "    if (added != 5) return 2;\n"
        "    if (*pointer != 3) return 3;\n"
        "    if (sizeof(values) != 12) return 4;\n"
        "    if (values[0] != 6) return 5;\n"
        "    if (point.x + point.y != 5) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C11 generic selections choose by expression type") {
    const ProgramResult program = compile_and_run(
        "#define KIND(x) _Generic((x), int: 1, char: 2, double: 3, default: 0)\n"
        "int main(void) {\n"
        "    int i = 0;\n"
        "    char c = 0;\n"
        "    double d = 0;\n"
        "    if (KIND(i) != 1) return 1;\n"
        "    if (KIND(c) != 2) return 2;\n"
        "    if (KIND(d) != 3) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("generic selection control expressions are unevaluated") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int x = 0;\n"
        "    int value = _Generic(x++, int: 5, default: 0);\n"
        "    if (value != 5) return 1;\n"
        "    if (x != 0) return 2;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("generic selection without a match is rejected") {
    const cc::Result result = cc::compile(
        "int main(void) { return _Generic(1, char: 1); }\n");
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("generic") !=
          std::string::npos);
}

TEST_CASE("_Alignas raises declaration alignment") {
    const ProgramResult program = compile_and_run(
        "struct Aligned { char tag; _Alignas(16) int value; };\n"
        "int main(void) {\n"
        "    if (__builtin_offsetof(struct Aligned, value) != 16) return 1;\n"
        "    if (sizeof(struct Aligned) != 32) return 2;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("_Alignas may follow the type") {
    const ProgramResult program = compile_and_run(
        "int _Alignas(16) aligned_global = 1;\n"
        "int main(void) {\n"
        "    int _Alignas(16) aligned_local = 2;\n"
        "    return aligned_global + aligned_local - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("#line updates source identity") {
    const ProgramResult program = compile_and_run(
        "#line 10 \"custom.c\"\n"
        "int main(void) {\n"
        "    if (__LINE__ != 11) return 1;\n"
        "    return __FILE__[0] == 'c' ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("one-argument static assertions work") {
    const ProgramResult program = compile_and_run(
        "_Static_assert(sizeof(int) == 4);\n"
        "int main(void) {\n"
        "    _Static_assert(1);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

