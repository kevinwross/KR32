#include "cc_test_support.h"

TEST_CASE("the C compiler compiles arithmetic and functions") {
    const cc::Result result = cc::compile(
        "int add(int a, int b) { return a + b; }\n"
        "int main(void) { return add(2, 3); }\n");
    const std::string diagnostic =
        result.diagnostics.empty() ? "compile failed" : result.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(result.ok);
    CHECK(result.assembly.find("add:") != std::string::npos);
    CHECK(result.assembly.find("CALL add") != std::string::npos);
}

TEST_CASE("C local declarations support multiple declarators") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int a = 1, b = 2;\n"
        "    return a + b - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);

    const ProgramResult pointers = compile_and_run(
        "int main(void) {\n"
        "    int value = 3;\n"
        "    int *left = &value, *right = &value;\n"
        "    return *left + *right - 6;\n"
        "}\n");
    CHECK(pointers.ok);
    CHECK(pointers.result == 0);

    const ProgramResult globals = compile_and_run(
        "int a = 1, b = 2;\n"
        "int main(void) { return a + b - 3; }\n");
    CHECK(globals.ok);
    CHECK(globals.result == 0);

    const ProgramResult pointer_globals = compile_and_run(
        "int value = 3;\n"
        "int *left = &value, *right = &value;\n"
        "int main(void) { return *left + *right - 6; }\n");
    CHECK(pointer_globals.ok);
    CHECK(pointer_globals.result == 0);

    const ProgramResult string_pointer = compile_and_run(
        "const char *message = \"hello\";\n"
        "int main(void) { return message[0] - 'h'; }\n");
    CHECK(string_pointer.ok);
    CHECK(string_pointer.result == 0);

    const ProgramResult typedefs = compile_and_run(
        "typedef int A, B;\n"
        "int main(void) { A x = 1; B y = 2; return x + y - 3; }\n");
    CHECK(typedefs.ok);
    CHECK(typedefs.result == 0);

    const ProgramResult restrict_parameters = compile_and_run(
        "int add(int * restrict left, int * restrict right) {\n"
        "    return *left + *right;\n"
        "}\n"
        "int main(void) {\n"
        "    int a = 1; int b = 2;\n"
        "    return add(&a, &b) - 3;\n"
        "}\n");
    CHECK(restrict_parameters.ok);
    CHECK(restrict_parameters.result == 0);

    const ProgramResult string_pointers = compile_and_run(
        "const char *names[2] = { \"one\", \"two\" };\n"
        "int main(void) { return names[0][0] - 'o'; }\n");
    CHECK(string_pointers.ok);
    CHECK(string_pointers.result == 0);

    const ProgramResult address_pointers = compile_and_run(
        "int a = 1; int b = 2;\n"
        "int *values[2] = { &a, &b };\n"
        "int main(void) { return *values[0] + *values[1] - 3; }\n");
    CHECK(address_pointers.ok);
    CHECK(address_pointers.result == 0);

    const ProgramResult double_pointer = compile_and_run(
        "int main(void) {\n"
        "    int value = 4;\n"
        "    int *pointer = &value;\n"
        "    int **double_pointer = &pointer;\n"
        "    return **double_pointer - 4;\n"
        "}\n");
    CHECK(double_pointer.ok);
    CHECK(double_pointer.result == 0);

    const ProgramResult standard_headers = compile_and_run(
        "#include <stdint.h>\n"
        "#include <stdbool.h>\n"
        "#include <limits.h>\n"
        "int main(void) {\n"
        "    uint32_t value = UINT32_MAX;\n"
        "    int8_t small = INT8_MAX;\n"
        "    bool flag = true;\n"
        "    if (value != 0xFFFFFFFFU) return 1;\n"
        "    if (small != 127) return 2;\n"
        "    if (!flag) return 3;\n"
        "    if (INT_MAX != 2147483647) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(standard_headers.ok);
    CHECK(standard_headers.result == 0);

    const ProgramResult ctype_header = compile_and_run(
        "#include <ctype.h>\n"
        "int main(void) {\n"
        "    if (!isdigit('7')) return 1;\n"
        "    if (!isalpha('Q')) return 2;\n"
        "    if (!isspace('\\n')) return 3;\n"
        "    if (tolower('A') != 'a') return 4;\n"
        "    if (toupper('b') != 'B') return 5;\n"
        "    if (isxdigit('G')) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(ctype_header.ok);
    CHECK(ctype_header.result == 0);

    const ProgramResult assert_header = compile_and_run(
        "#include <assert.h>\n"
        "int main(void) {\n"
        "    assert(1 == 1);\n"
        "    assert(2 > 1);\n"
        "    return 0;\n"
        "}\n");
    CHECK(assert_header.ok);
    CHECK(assert_header.result == 0);

    const ProgramResult numeric_conversions = compile_and_run(
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    if (atoi(\"  -42x\") != -42) return 1;\n"
        "    if (atol(\"123\") != 123) return 2;\n"
        "    char *end = 0;\n"
        "    long value = strtol(\"0x1f rest\", &end, 0);\n"
        "    if (value != 31) return 3;\n"
        "    if (*end != ' ') return 4;\n"
        "    if (atof(\"-3.5\") != -3.5) return 5;\n"
        "    return 0;\n"
        "}\n");
    CHECK(numeric_conversions.ok);
    CHECK(numeric_conversions.result == 0);

    const ProgramResult time_header = compile_and_run(
        "#include <time.h>\n"
        "int main(void) {\n"
        "    time_t stored = 0;\n"
        "    time_t now = time(&stored);\n"
        "    if (now <= 0) return 1;\n"
        "    if (stored != now) return 2;\n"
        "    if (difftime(10.0, 4.0) != 6.0) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(time_header.ok);
    CHECK(time_header.result == 0);

    const ProgramResult string_scanning = compile_and_run(
        "#include <string.h>\n"
        "int main(void) {\n"
        "    const char *text = \"abc123\";\n"
        "    if (strspn(text, \"abc\") != 3) return 1;\n"
        "    if (strcspn(text, \"123\") != 3) return 2;\n"
        "    char *found = strpbrk(text, \"23\");\n"
        "    if (found == 0) return 3;\n"
        "    if (*found != '2') return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(string_scanning.ok);
    CHECK(string_scanning.result == 0);

    const ProgramResult stdlib_random = compile_and_run(
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    if (abs(-7) != 7) return 1;\n"
        "    if (labs(-9) != 9) return 2;\n"
        "    srand(1);\n"
        "    int first = rand();\n"
        "    if (first <= 0) return 3;\n"
        "    srand(1);\n"
        "    if (rand() != first) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(stdlib_random.ok);
    CHECK(stdlib_random.result == 0);

    const ProgramResult varargs_formatting = compile_and_run(
        "#include <stdio.h>\n"
        "#include <stdarg.h>\n"
        "int format(char *buffer, int size, const char *text, ...) {\n"
        "    va_list arguments;\n"
        "    va_start(arguments, text);\n"
        "    int result = vsnprintf(buffer, size, text, arguments);\n"
        "    va_end(arguments);\n"
        "    return result;\n"
        "}\n"
        "int main(void) {\n"
        "    char buffer[32];\n"
        "    int count = format(buffer, 32, \"%d-%s\", 42, \"ok\");\n"
        "    if (count != 5) return 1;\n"
        "    if (buffer[0] != '4') return 2;\n"
        "    if (buffer[3] != 'o') return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(varargs_formatting.ok);
    CHECK(varargs_formatting.result == 0);

    const ProgramResult static_locals = compile_and_run(
        "int next(void) {\n"
        "    static int value = 0;\n"
        "    value = value + 1;\n"
        "    return value;\n"
        "}\n"
        "int count(void) {\n"
        "    static int total;\n"
        "    total = total + 2;\n"
        "    return total;\n"
        "}\n"
        "int main(void) {\n"
        "    if (next() != 1) return 1;\n"
        "    if (next() != 2) return 2;\n"
        "    if (count() != 2) return 3;\n"
        "    if (count() != 4) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(static_locals.ok);
    CHECK(static_locals.result == 0);

    const ProgramResult string_collation = compile_and_run(
        "#include <string.h>\n"
        "int main(void) {\n"
        "    if (strcoll(\"abc\", \"abd\") >= 0) return 1;\n"
        "    char buffer[8];\n"
        "    int length = strxfrm(buffer, \"hello\", 8);\n"
        "    if (length != 5) return 2;\n"
        "    if (buffer[0] != 'h') return 3;\n"
        "    if (buffer[4] != 'o') return 4;\n"
        "    if (buffer[5] != 0) return 5;\n"
        "    return 0;\n"
        "}\n");
    CHECK(string_collation.ok);
    CHECK(string_collation.result == 0);

    const ProgramResult static_aggregates = compile_and_run(
        "struct P { int x; int y; };\n"
        "int total(void) {\n"
        "    static int values[3] = {1, 2, 3};\n"
        "    static struct P point = {4, 5};\n"
        "    values[0] = values[0] + 1;\n"
        "    return values[0] + values[1] + values[2] + point.x + point.y;\n"
        "}\n"
        "int main(void) { return total() - 16; }\n");
    CHECK(static_aggregates.ok);
    CHECK(static_aggregates.result == 0);

    const ProgramResult memccpy_result = compile_and_run(
        "#include <string.h>\n"
        "char source[8] = \"abc:def\";\n"
        "int main(void) {\n"
        "    char buffer[8];\n"
        "    void *end = memccpy(buffer, source, ':', 8);\n"
        "    if (end == 0) return 1;\n"
        "    if (buffer[0] != 'a') return 2;\n"
        "    if (buffer[3] != ':') return 3;\n"
        "    if (memccpy(buffer, source, 'z', 8) != 0) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(memccpy_result.ok);
    CHECK(memccpy_result.result == 0);

    const ProgramResult strtok_r_result = compile_and_run(
        "#include <string.h>\n"
        "char text[16] = \"one,two,three\";\n"
        "int main(void) {\n"
        "    char *save = 0;\n"
        "    char *first = strtok_r(text, \",\", &save);\n"
        "    char *second = strtok_r(0, \",\", &save);\n"
        "    char *third = strtok_r(0, \",\", &save);\n"
        "    if (first == 0 || first[0] != 'o') return 1;\n"
        "    if (second == 0 || second[0] != 't') return 2;\n"
        "    if (third == 0 || third[0] != 't') return 3;\n"
        "    if (strtok_r(0, \",\", &save) != 0) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(strtok_r_result.ok);
    CHECK(strtok_r_result.result == 0);

    const ProgramResult function_name = compile_and_run(
        "#include <string.h>\n"
        "int check(void) { return strcmp(__func__, \"check\"); }\n"
        "int main(void) { return check(); }\n");
    CHECK(function_name.ok);
    CHECK(function_name.result == 0);

    const ProgramResult source_identity = compile_and_run(
        "int main(void) {\n"
        "    int line = __LINE__;\n"
        "    if (line != 2) return 1;\n"
        "    if (__FILE__[0] == 0) return 2;\n"
        "    return 0;\n"
        "}\n");
    CHECK(source_identity.ok);
    CHECK(source_identity.result == 0);

    const ProgramResult standard_macros = compile_and_run(
        "int main(void) {\n"
        "    if (__STDC__ != 1) return 1;\n"
        "    if (__STDC_VERSION__ < 199901) return 2;\n"
        "    if (__DATE__[0] == 0) return 3;\n"
        "    if (__TIME__[2] != ':') return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(standard_macros.ok);
    CHECK(standard_macros.result == 0);

    const ProgramResult counter = compile_and_run(
        "int main(void) {\n"
        "    int first = __COUNTER__;\n"
        "    int second = __COUNTER__;\n"
        "    return first == 0 && second == 1 ? 0 : 1;\n"
        "}\n");
    CHECK(counter.ok);
    CHECK(counter.result == 0);

    const ProgramResult bounded_strings = compile_and_run(
        "#include <string.h>\n"
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    if (strnlen(\"hello\", 3) != 3) return 1;\n"
        "    if (strnlen(\"hi\", 8) != 2) return 2;\n"
        "    char *copy = strndup(\"hello\", 3);\n"
        "    if (copy == 0) return 3;\n"
        "    if (copy[0] != 'h') return 4;\n"
        "    if (copy[2] != 'l') return 5;\n"
        "    if (copy[3] != 0) return 6;\n"
        "    free(copy);\n"
        "    return 0;\n"
        "}\n");
    CHECK(bounded_strings.ok);
    CHECK(bounded_strings.result == 0);

    const ProgramResult function_pointers = compile_and_run(
        "typedef int (*binary_fn)(int, int);\n"
        "int add(int a, int b) { return a + b; }\n"
        "int multiply(int a, int b) { return a * b; }\n"
        "binary_fn choose(int which) { return which == 0 ? add : multiply; }\n"
        "int apply(binary_fn operation, int a, int b) {\n"
        "    return operation(a, b);\n"
        "}\n"
        "binary_fn global_operation = add;\n"
        "int (*direct_global)(int, int) = multiply;\n"
        "binary_fn operations[2] = { add, multiply };\n"
        "int main(void) {\n"
        "    binary_fn operation = add;\n"
        "    if (operation(2, 3) != 5) return 1;\n"
        "    operation = multiply;\n"
        "    if (operation(2, 3) != 6) return 2;\n"
        "    if (apply(add, 4, 5) != 9) return 3;\n"
        "    operation = choose(1);\n"
        "    if (operation(3, 4) != 12) return 4;\n"
        "    if (global_operation(1, 2) != 3) return 5;\n"
        "    int (*direct)(int, int) = add;\n"
        "    if (direct(5, 6) != 11) return 6;\n"
        "    if (direct_global(2, 4) != 8) return 7;\n"
        "    if (operations[0](2, 3) != 5) return 8;\n"
        "    if (operations[1](2, 4) != 8) return 9;\n"
        "    return 0;\n"
        "}\n");
    CHECK(function_pointers.ok);
    CHECK(function_pointers.result == 0);

    const ProgramResult qsort_result = compile_and_run(
        "#include <stdlib.h>\n"
        "int values[5] = {5, 1, 4, 2, 3};\n"
        "int compare(const void *left, const void *right) {\n"
        "    int a = *(const int *)left;\n"
        "    int b = *(const int *)right;\n"
        "    return a - b;\n"
        "}\n"
        "int main(void) {\n"
        "    qsort(values, 5, sizeof(int), compare);\n"
        "    if (values[0] != 1) return 1;\n"
        "    if (values[1] != 2) return 2;\n"
        "    if (values[4] != 5) return 3;\n"
        "    int key = 4;\n"
        "    int *found = (int *)bsearch(&key, values, 5, sizeof(int), compare);\n"
        "    if (found == 0) return 4;\n"
        "    if (*found != 4) return 5;\n"
        "    return 0;\n"
        "}\n");
    CHECK(qsort_result.ok);
    CHECK(qsort_result.result == 0);

    const ProgramResult stack_arguments = compile_and_run(
        "int sum5(int a, int b, int c, int d, int e) {\n"
        "    return a + b + c + d + e;\n"
        "}\n"
        "int sum6(int a, int b, int c, int d, int e, int f) {\n"
        "    return a + b + c + d + e + f;\n"
        "}\n"
        "long long wide(int a, int b, int c, int d, long long e) {\n"
        "    return e + a + b + c + d;\n"
        "}\n"
        "int main(void) {\n"
        "    if (sum5(1, 2, 3, 4, 5) != 15) return 1;\n"
        "    if (sum6(1, 2, 3, 4, 5, 6) != 21) return 2;\n"
        "    if (wide(1, 2, 3, 4, 10) != 20) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(stack_arguments.ok);
    CHECK(stack_arguments.result == 0);

    const ProgramResult aggregate_initializers = compile_and_run(
        "struct P { int x; int y; };\n"
        "struct P make(int a, int b) {\n"
        "    struct P p; p.x = a; p.y = b; return p;\n"
        "}\n"
        "int main(void) {\n"
        "    struct P p = make(1, 2);\n"
        "    if (p.x != 1) return 1;\n"
        "    if (p.y != 2) return 2;\n"
        "    struct P assigned;\n"
        "    assigned = make(3, 4);\n"
        "    if (assigned.x != 3) return 5;\n"
        "    if (assigned.y != 4) return 6;\n"
        "    struct P copy = assigned;\n"
        "    if (copy.x != 3) return 7;\n"
        "    char text[4] = \"hey\";\n"
        "    if (text[0] != 'h') return 3;\n"
        "    if (text[3] != 0) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(aggregate_initializers.ok);
    CHECK(aggregate_initializers.result == 0);
}

TEST_CASE("C supports compound literals and struct parameters") {
    const ProgramResult compound = compile_and_run(
        "struct Point { int x; int y; };\n"
        "int sum(struct Point p) { return p.x + p.y; }\n"
        "int mutate(struct Point p) { p.x = 99; return p.x; }\n"
        "int main(void) {\n"
        "    struct Point p = (struct Point){2, 3};\n"
        "    if (sum(p) != 5) return 1;\n"
        "    if (sum((struct Point){4, 5}) != 9) return 2;\n"
        "    if (mutate(p) != 99) return 3;\n"
        "    if (p.x != 2) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(compound.ok);
    CHECK(compound.result == 0);

    const ProgramResult scalar = compile_and_run(
        "int main(void) {\n"
        "    int value = (int){7};\n"
        "    return value - 7;\n"
        "}\n");
    CHECK(scalar.ok);
    CHECK(scalar.result == 0);
}

TEST_CASE("C supports bit-fields") {
    const ProgramResult program = compile_and_run(
        "struct Flags { unsigned int a : 1; unsigned int b : 3; };\n"
        "struct S { unsigned int a : 3; unsigned int b : 5; int c : 4; };\n"
        "struct T { unsigned int a : 30; unsigned int b : 5; };\n"
        "int main(void) {\n"
        "    struct Flags flags;\n"
        "    flags.a = 1;\n"
        "    flags.b = 5;\n"
        "    if (flags.a + flags.b != 6) return 1;\n"
        "    struct S value;\n"
        "    value.a = 7;\n"
        "    value.b = 31;\n"
        "    value.c = -1;\n"
        "    if (value.a != 7) return 2;\n"
        "    if (value.b != 31) return 3;\n"
        "    if (value.c != -1) return 4;\n"
        "    if (sizeof(struct S) != 4) return 5;\n"
        "    if (sizeof(struct T) != 8) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C supports common compiler builtins") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int value = 5;\n"
        "    if (__builtin_expect(value, 5) != 5) return 1;\n"
        "    if (__builtin_constant_p(value) != 0) return 2;\n"
        "    __extension__ value = 6;\n"
        "    if (value == 0) __builtin_unreachable();\n"
        "    return value - 6;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C supports anonymous struct and union members") {
    const ProgramResult program = compile_and_run(
        "struct Variant { int tag; union { int integer; float real; }; };\n"
        "struct Nested { int a; struct { int b; int c; }; };\n"
        "int main(void) {\n"
        "    struct Variant value;\n"
        "    value.tag = 1;\n"
        "    value.integer = 7;\n"
        "    if (value.tag != 1) return 1;\n"
        "    if (value.integer != 7) return 2;\n"
        "    struct Nested nested;\n"
        "    nested.a = 1;\n"
        "    nested.b = 2;\n"
        "    nested.c = 3;\n"
        "    if (nested.a + nested.b + nested.c != 6) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("a compiled C program links, runs, and returns a value") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int x = 2;\n"
        "    int y = 3;\n"
        "    return x + y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 5);
}

TEST_CASE("a compiled C program supports recursion") {
    const ProgramResult program = compile_and_run(
        "int fib(int n) {\n"
        "    if (n < 2) return n;\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "int main(void) { return fib(10); }\n");
    CHECK(program.ok);
    CHECK(program.result == 55);
}

TEST_CASE("a compiled C program supports float and double scalars") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    float f = 2.5f;\n"
        "    double d = 3.25;\n"
        "    float sum = f + (float)d;\n"
        "    return (int)(f * f) + (int)(d * d) + (int)sum;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 6 + 10 + 5);
}
TEST_CASE("an undeclared function call is diagnosed") {
    const cc::Result implicit = cc::compile(
        "int main(void) { return helper(1); }\n");
    REQUIRE(implicit.ok);
    REQUIRE_FALSE(implicit.diagnostics.empty());
    CHECK(implicit.diagnostics.front().message.find(
              "implicit declaration of function 'helper'") !=
          std::string::npos);

    // A declaration, including a variadic one, keeps the call silent.
    const cc::Result declared = cc::compile(
        "int helper(int value);\n"
        "int main(void) { return helper(1); }\n");
    CHECK(declared.ok);
    CHECK(declared.diagnostics.empty());

    const cc::Result variadic = cc::compile(
        "int printf(const char *format, ...);\n"
        "int main(void) { return printf(\"%d\", 1); }\n");
    CHECK(variadic.ok);
    CHECK(variadic.diagnostics.empty());
}
TEST_CASE("sizeof a string literal is its array size") {
    // A string literal is an array, so sizeof counts its bytes and terminator;
    // treating it as a pointer made `line + (sizeof("exit") - 1)` point one byte
    // short, which broke the shell's `exit <status>` argument.
    CHECK(compile_and_run("int main(void) { return sizeof(\"abc\"); }").result == 4);
    CHECK(compile_and_run("int main(void) { return sizeof(\"\"); }").result == 1);
    CHECK(compile_and_run("int main(void) { return sizeof(\"ab\" \"cd\"); }").result == 5);
    CHECK(compile_and_run("int main(void) { return sizeof(\"abc\") + 1; }").result == 5);
}