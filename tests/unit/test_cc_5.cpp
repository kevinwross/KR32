#include "cc_test_support.h"

TEST_CASE("libc heapwalk validates block links and free counts") {
    const ProgramResult program = compile_and_run(
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    char *a = (char *)malloc(64);\n"
        "    char *b = (char *)malloc(128);\n"
        "    if (a == 0 || b == 0) return 1;\n"
        "    free(a);\n"
        "    char *c = (char *)malloc(32);\n"
        "    if (c == 0) return 2;\n"
        "    free(b);\n"
        "    free(c);\n"
        "    return _heapwalk();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("libc printf formats integers and floating-point values") {
    const ProgramResult program = compile_and_run(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    printf(\"int=%d hex=%x str=%s char=%c\\n\", 42, 42, \"ok\", 'A');\n"
        "    printf(\"f=%.2f e=%.2e g=%g\\n\", 3.14159, 12345.0, 0.000123);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.output ==
          "int=42 hex=2a str=ok char=A\n"
          "f=3.14 e=1.23e+04 g=0.000123\n");
}

TEST_CASE("FP math builtins map to the KR32 FP instructions") {
    const ProgramResult program = compile_and_run(
        "#include <math.h>\n"
        "int main(void) {\n"
        "    double root = sqrt(81.0);\n"
        "    double magnitude = fabs(-2.5);\n"
        "    double low = fmin(3.0, 1.5);\n"
        "    double high = fmax(3.0, 1.5);\n"
        "    return (int)(root + magnitude + low + high);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 16);
}

TEST_CASE("LL/SC and bit builtins expose the atomic and bit escapes") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int value = 1;\n"
        "    int old = __builtin_ll(&value);\n"
        "    int ok = __builtin_sc(&value, 3);\n"
        "    __builtin_fence();\n"
        "    int swapped = __builtin_bswap32(0x11223344);\n"
        "    int leading = __builtin_clz(1);\n"
        "    int trailing = __builtin_ctz(8);\n"
        "    int bits = __builtin_popcount(0xF0F0);\n"
        "    if (old != 1 || ok != 1 || value != 3) return 1;\n"
        "    if (swapped != 0x44332211 || leading != 31 || trailing != 3) return 2;\n"
        "    if (bits != 8) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("libc string and memory functions work together") {
    const ProgramResult program = compile_and_run(
        "#include <string.h>\n"
        "int main(void) {\n"
        "    char text[64];\n"
        "    strcpy(text, \"hello\");\n"
        "    strcat(text, \" world\");\n"
        "    if (strcmp(text, \"hello world\") != 0) return 1;\n"
        "    if (strncmp(text, \"hello\", 5) != 0) return 2;\n"
        "    if (strchr(text, 'w') == 0 || strrchr(text, 'o') == 0) return 3;\n"
        "    if (strstr(text, \"world\") == 0) return 4;\n"
        "    char overlap[16];\n"
        "    strcpy(overlap, \"abcdef\");\n"
        "    memmove(overlap + 2, overlap, 4);\n"
        "    if (strcmp(overlap, \"ababcd\") != 0) return 5;\n"
        "    if (memcmp(\"abc\", \"abc\", 3) != 0) return 6;\n"
        "    if (memchr(overlap, 'd', 16) == 0) return 7;\n"
        "    char tokens[32];\n"
        "    strcpy(tokens, \"one,two;three\");\n"
        "    if (strcmp(strtok(tokens, \",;\"), \"one\") != 0) return 8;\n"
        "    if (strcmp(strtok(0, \",;\"), \"two\") != 0) return 9;\n"
        "    if (strcmp(strtok(0, \",;\"), \"three\") != 0) return 10;\n"
        "    char *copy = strdup(\"abc\");\n"
        "    if (copy == 0 || strcmp(copy, \"abc\") != 0) return 11;\n"
        "    free(copy);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("overflow builtins report wraparound") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int output = 0;\n"
        "    int add_overflow = __builtin_add_overflow(2147483647, 1, &output);\n"
        "    int mul_overflow = __builtin_mul_overflow(65536, 65536, &output);\n"
        "    if (add_overflow != 1 || mul_overflow != 1 || output != 0) return 1;\n"
        "    return __builtin_add_overflow(2, 3, &output);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("a compiled C program supports arrays and indexing") {
    const ProgramResult program = compile_and_run(
        "int sum(int *values, int count) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < count; i = i + 1) {\n"
        "        total = total + values[i];\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main(void) {\n"
        "    int values[3];\n"
        "    values[0] = 10;\n"
        "    values[1] = 20;\n"
        "    values[2] = 12;\n"
        "    return sum(values, 3);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("a compiled C program supports structs and member access") {
    const ProgramResult program = compile_and_run(
        "struct Point { int x; int y; };\n"
        "int main(void) {\n"
        "    struct Point point;\n"
        "    point.x = 3;\n"
        "    point.y = 4;\n"
        "    struct Point *pointer = &point;\n"
        "    pointer->x = pointer->x + 1;\n"
        "    return point.x + point.y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 8);
}

TEST_CASE("a compiled C program supports typedefs, enums, and unions") {
    const ProgramResult program = compile_and_run(
        "typedef struct Pair { int first; int second; } Pair;\n"
        "typedef enum Color { RED, GREEN = 3, BLUE } Color;\n"
        "union Bits { int word; char bytes[4]; };\n"
        "int main(void) {\n"
        "    Pair pair;\n"
        "    pair.first = GREEN;\n"
        "    pair.second = BLUE;\n"
        "    union Bits bits;\n"
        "    bits.word = 0x01020304;\n"
        "    return pair.first + pair.second + bits.bytes[0];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 11);
}

