#include "cc_test_support.h"

TEST_CASE("C signed long long divides and takes signed remainders") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    long long value = -7;\n"
        "    long long quotient = value / 2;\n"
        "    long long remainder = value % 2;\n"
        "    return quotient == -3 && remainder == -1 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C supports nested designated initializers") {
    const ProgramResult program = compile_and_run(
        "struct Inner { int x; int y; };\n"
        "struct Outer { struct Inner in; int z; };\n"
        "struct Outer g = {.in.y = 5};\n"
        "int main(void) {\n"
        "    struct Outer o = {.in.y = 2, .z = 3};\n"
        "    return o.in.x + o.in.y + o.z + g.in.y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("C supports designated aggregate and array initializers") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; };\n"
        "int g[3] = {[1] = 5};\n"
        "int main(void) {\n"
        "    int a[4] = {[2] = 7, 8};\n"
        "    struct P p = {.y = 2};\n"
        "    return p.x + p.y + a[0] + a[2] + a[3] +\n"
        "           g[0] + g[1] + g[2];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 22);
}

TEST_CASE("C short and unsigned short use 16-bit storage") {
    const ProgramResult program = compile_and_run(
        "struct P { short a; short b; };\n"
        "short add(short x, short y) { return x + y; }\n"
        "short g = 300;\n"
        "int main(void) {\n"
        "    short s = -2;\n"
        "    unsigned short u = 65535;\n"
        "    struct P p = {3, 4};\n"
        "    short* q = &s;\n"
        "    *q = *q + 5;\n"
        "    return add(s, p.a) + p.b + (u > 0) + (g == 300) +\n"
        "           sizeof(short) + sizeof(p);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 18);
}

TEST_CASE("C supports aggregate and nested array initializers") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; char c; };\n"
        "int main(void) {\n"
        "    int a[3] = {1, 2, 3};\n"
        "    int m[2][2] = {{4, 5}, {6, 7}};\n"
        "    struct P p = {10, 20, 'Z'};\n"
        "    int b[4] = {9};\n"
        "    return a[0] + a[2] + m[0][1] + m[1][0] + p.x + p.y + p.c + b[0] + b[3];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 144);
}

TEST_CASE("C unsigned and long integer operations use unsigned semantics") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    unsigned int u = 1u;\n"
        "    u = u - 2u;\n"
        "    if (u != 0xFFFFFFFFu) return 1;\n"
        "    if (!(u > 1u)) return 2;\n"
        "    if (7u / 2u != 3u) return 3;\n"
        "    if (7u % 2u != 1u) return 4;\n"
        "    if ((0x80000000u >> 31) != 1u) return 5;\n"
        "    unsigned char c = 255;\n"
        "    c++;\n"
        "    if (c != 0u) return 6;\n"
        "    long value = 9;\n"
        "    if (-7 / 2 != -3) return 8;\n"
        "    if (-7 % 2 != -1) return 9;\n"
        "    return value == 9 ? 0 : 7;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C do-while executes its body before testing") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int i = 0;\n"
        "    int total = 0;\n"
        "    do { total += i; i++; } while (i < 4);\n"
        "    return total;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 6);
}

TEST_CASE("C conditional expressions select a value") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int x = 5;\n"
        "    int i = 2;\n"
        "    int ternary = (x > i) ? 0 : 1;\n"
        "    return ternary + (i > x ? 10 : 20);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 20);
}

TEST_CASE("C switch supports fallthrough and default") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int x = 1;\n"
        "    int total = 0;\n"
        "    switch (x) {\n"
        "    case 1: total = total + 1;\n"
        "    case 2: total = total + 2; break;\n"
        "    case 3: total = total + 4; break;\n"
        "    default: total = total + 8;\n"
        "    }\n"
        "    return total;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 3);
}

