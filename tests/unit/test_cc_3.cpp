#include "cc_test_support.h"

TEST_CASE("C increments work in for loops and on array elements") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int values[3];\n"
        "    values[0] = 1; values[1] = 2; values[2] = 3;\n"
        "    values[0] += 4; values[1]++;\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < 3; i++) total += values[i];\n"
        "    int *p = values;\n"
        "    *p++ = 9;\n"
        "    return total + values[0];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 5 + 3 + 3 + 9);
}

TEST_CASE("C floating-point increments and compound assignments convert back") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    float value = 1.5f;\n"
        "    value++;\n"
        "    ++value;\n"
        "    value += 2.0f;\n"
        "    return (int)value;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 5);
}

TEST_CASE("C postfix increments return the old value and truncate chars") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int i = 3;\n"
        "    int a = i++;\n"
        "    int b = ++i;\n"
        "    char c = 127;\n"
        "    c++;\n"
        "    c += 2;\n"
        "    _Bool z = 1;\n"
        "    z++;\n"
        "    z += 1;\n"
        "    return a + b + (int)c + z;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 3 + 5 - 126 + 1);
}

TEST_CASE("C supports multidimensional arrays") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int a[2][3][4];\n"
        "    a[0][0][0] = 1;\n"
        "    a[1][2][3] = 42;\n"
        "    return a[0][0][0] + a[1][2][3];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 43);
}

TEST_CASE("C supports global aggregate and array initializers") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; char c; };\n"
        "int a[3] = {1, 2, 3};\n"
        "int m[2][2] = {{4, 5}, {6, 7}};\n"
        "struct P p = {10, 20, 'Z'};\n"
        "char text[4] = \"hey\";\n"
        "int b[4] = {9};\n"
        "int main(void) {\n"
        "    return a[0] + a[2] + m[0][1] + m[1][0] + p.x + p.y + p.c +\n"
        "           b[0] + b[3] + text[0];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 248);
}

TEST_CASE("C long long stores 64-bit values and carries") {
    const ProgramResult program = compile_and_run(
        "long long global = 5;\n"
        "int main(void) {\n"
        "    long long a = 4294967295;\n"
        "    long long b = a + 1;\n"
        "    long long c = b + 4294967295;\n"
        "    long long d = (b * 3) >> 32;\n"
        "    long long e = 1;\n"
        "    e = e << 33;\n"
        "    return b == 4294967296 && c == 8589934591 &&\n"
        "                   d == 3 && e == 8589934592 &&\n"
        "                   global == 5 && sizeof(long long) == 8\n"
        "               ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C long long functions pass and return 64-bit values") {
    const ProgramResult program = compile_and_run(
        "long long add_one(long long value) { return value + 1; }\n"
        "int main(void) {\n"
        "    long long value = 4294967295;\n"
        "    return add_one(value) == 4294967296 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C unsigned long long divides and takes remainders") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    unsigned long long value = 8589934591;\n"
        "    return value / 3 == 2863311530 && value % 3 == 1 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C printf formats long long values") {
    const ProgramResult program = compile_and_run(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    long long signed_value = -7;\n"
        "    unsigned long long unsigned_value = 8589934591;\n"
        "    printf(\"%lld %llu %llx\\n\", signed_value, unsigned_value,\n"
        "           unsigned_value);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.output == "-7 8589934591 1ffffffff\n");
}

