#include "cc_test_support.h"

TEST_CASE("a compiled C program uses the floating-point function ABI") {
    const ProgramResult program = compile_and_run(
        "float DoubleFloat(float x) { return x * x; }\n"
        "double DoubleDouble(double x) { return x * x; }\n"
        "double Mix(int n, double d, float f) { return n + d + f; }\n"
        "int main(void) {\n"
        "    return (int)DoubleFloat(2.5f) + (int)DoubleDouble(3.25) +\n"
        "           (int)Mix(3, 2.5, 1.25f);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 6 + 10 + 6);
}

TEST_CASE("a compiled C program supports FP globals and comparisons") {
    const ProgramResult program = compile_and_run(
        "double value = 4.5;\n"
        "int main(void) {\n"
        "    float f = 2.5f;\n"
        "    double d = 3.25;\n"
        "    if (d > f && value == 4.5) {\n"
        "        return (int)(value * value);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 20);
}

TEST_CASE("a compiled C program supports variadic integer and FP arguments") {
    const ProgramResult program = compile_and_run(
        "#include <stdarg.h>\n"
        "int sum(int count, ...) {\n"
        "    va_list ap;\n"
        "    va_start(ap, count);\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < count; i = i + 1) {\n"
        "        total = total + va_arg(ap, int);\n"
        "    }\n"
        "    va_end(ap);\n"
        "    return total;\n"
        "}\n"
        "double dsum(int count, ...) {\n"
        "    va_list ap;\n"
        "    va_start(ap, count);\n"
        "    double total = 0.0;\n"
        "    for (int i = 0; i < count; i = i + 1) {\n"
        "        total = total + va_arg(ap, double);\n"
        "    }\n"
        "    va_end(ap);\n"
        "    return total;\n"
        "}\n"
        "double mixed(int count, ...) {\n"
        "    va_list ap;\n"
        "    va_start(ap, count);\n"
        "    int n = va_arg(ap, int);\n"
        "    double d = va_arg(ap, double);\n"
        "    double f = va_arg(ap, double);\n"
        "    va_end(ap);\n"
        "    return n + d + f;\n"
        "}\n"
        "int main(void) {\n"
        "    return sum(3, 1, 2, 3) + (int)dsum(2, 1.5, 2.5) +\n"
        "           (int)mixed(3, 4, 2.5, 1.25f);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 17);
}

TEST_CASE("a compiled C program promotes char and float variadic arguments") {
    const ProgramResult program = compile_and_run(
        "#include <stdarg.h>\n"
        "int promoted(int count, ...) {\n"
        "    va_list ap;\n"
        "    va_start(ap, count);\n"
        "    int character = va_arg(ap, int);\n"
        "    double real = va_arg(ap, double);\n"
        "    va_end(ap);\n"
        "    return character + (int)real;\n"
        "}\n"
        "int main(void) {\n"
        "    char c = 7;\n"
        "    float f = 2.5f;\n"
        "    return promoted(2, c, f);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 9);
}

TEST_CASE("variadic calls keep fixed FP args and support more than four extras") {
    const ProgramResult program = compile_and_run(
        "#include <stdarg.h>\n"
        "double weighted(double base, int count, ...) {\n"
        "    va_list ap;\n"
        "    va_start(ap, count);\n"
        "    double total = base;\n"
        "    for (int i = 0; i < count; i = i + 1) {\n"
        "        total = total + va_arg(ap, double);\n"
        "    }\n"
        "    va_end(ap);\n"
        "    return total;\n"
        "}\n"
        "int main(void) {\n"
        "    return (int)weighted(1.5, 5, 1.0, 2.0, 3.0, 4.0, 5.0);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 16);
}

TEST_CASE("sizeof, alignof, offsetof, and static assertions work") {
    const ProgramResult program = compile_and_run(
        "#include <stddef.h>\n"
        "struct Point { char tag; int x; double y; };\n"
        "_Static_assert(sizeof(struct Point) == 16, \"point size\");\n"
        "_Static_assert(offsetof(struct Point, y) == 8, \"point y offset\");\n"
        "int main(void) {\n"
        "    int value = 7;\n"
        "    return sizeof(value) + sizeof(value + 1) +\n"
        "           _Alignof(double) + offsetof(struct Point, x);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 4 + 4 + 8 + 4);
}

TEST_CASE("libc malloc reuses and coalesces free blocks") {
    const ProgramResult program = compile_and_run(
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    char *a = (char *)malloc(100);\n"
        "    char *b = (char *)malloc(200);\n"
        "    char *c = (char *)malloc(300);\n"
        "    if (a == 0 || b == 0 || c == 0) return 1;\n"
        "    free(b);\n"
        "    char *d = (char *)malloc(64);\n"
        "    if (d != b) return 2;\n"
        "    free(a);\n"
        "    free(c);\n"
        "    free(d);\n"
        "    char *e = (char *)malloc(300);\n"
        "    if (e != a) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C compound assignments and increments follow C semantics") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int x = 6;\n"
        "    x += 1; x -= 1; x *= 2; x /= 2; x %= 7;\n"
        "    x <<= 1; x >>= 1; x &= 7; x |= 8; x ^= 1;\n"
        "    ++x; x++;\n"
        "    return x;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 17);
}

TEST_CASE("C pointer compound assignments scale by element size") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int values[4];\n"
        "    int *p = values;\n"
        "    p++;\n"
        "    *p = 11;\n"
        "    p += 2;\n"
        "    *p = 22;\n"
        "    return values[1] + values[3];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 33);
}

