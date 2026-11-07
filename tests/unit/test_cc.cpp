#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/link/linker.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace kr32;

namespace {

struct ProgramResult {
    std::string output;
    std::uint32_t result = 0;
    bool ok = false;
};

ProgramResult compile_and_run(std::string_view source) {
    ProgramResult program;
    const cc::Result compiled = cc::compile(source);
    const std::string diagnostic =
        compiled.diagnostics.empty() ? "compile failed" : compiled.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(compiled.ok);

    container::Image user;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(assembler::assemble_object(compiled.assembly, user, assembly_error),
                    assembly_error.message);

    container::Image runtime;
    REQUIRE_MESSAGE(assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error),
                    assembly_error.message);

    container::Image image;
    linker::Map map;
    linker::Error link_error;
    linker::Options link_options;
    link_options.stack_top = 0x0011'0000;
    link_options.stack_limit = 0x0001'0000;
    REQUIRE_MESSAGE(linker::link({user, runtime}, link_options, image, map, link_error),
                    link_error.message);

    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        REQUIRE(machine.load_image(section.address, section.data));
    }
    machine.reset(image.entry);
    machine.set_syscall_handler([&](core::Machine& current, std::uint32_t vector) {
        if (vector == 33) {
            program.output.push_back(static_cast<char>(current.reg(0) & 0xFFU));
            return true;
        }
        if (vector == 32) {
            current.halt(current.reg(0));
            return true;
        }
        if (vector == 40) {
            current.set_reg(0, 1'700'000'000U);
            return true;
        }
        if (vector == 42) {
            current.set_reg(0, 0);
            return true;
        }
        if (vector == 43) {
            current.set_reg(0, 0xFFFF'FFFCU);
            return true;
        }
        return false;
    });

    for (int step = 0; step < 1000000; ++step) {
        const core::StepResult step_result = machine.step();
        if (step_result.kind == core::StepResult::Kind::Halted) {
            program.result = machine.reg(0);
            program.ok = true;
            return program;
        }
        if (step_result.kind == core::StepResult::Kind::Raised) {
            return program;
        }
    }
    return program;
}

} // namespace

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
        "    return 0;\n"
        "}\n");
    CHECK(qsort_result.ok);
    CHECK(qsort_result.result == 0);
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

TEST_CASE("the C compiler reports syntax errors with positions") {
    const cc::Result result = cc::compile("int main(void) { return 1 + ; }\n");
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().line >= 1);
}

TEST_CASE("quoted user headers are expanded into the translation unit") {
    const auto directory = std::filesystem::current_path() / "kr32-cc-header-test";
    std::filesystem::create_directories(directory);
    {
        std::ofstream header(directory / "pair.h", std::ios::binary);
        header << "#pragma once\n"
                  "struct pair { int left; int right; };\n";
    }
    const auto source_path = directory / "main.c";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "#include \"pair.h\"\n"
                  "int main(void) {\n"
                  "    struct pair value;\n"
                  "    value.left = 3;\n"
                  "    value.right = 4;\n"
                  "    return value.left + value.right;\n"
                  "}\n";
    }
    const cc::Result result = cc::compile(
        "#include \"pair.h\"\n"
        "int main(void) {\n"
        "    struct pair value;\n"
        "    value.left = 3;\n"
        "    value.right = 4;\n"
        "    return value.left + value.right;\n"
        "}\n",
        source_path.string());
    CHECK(result.ok);
    CHECK(result.diagnostics.empty());
    std::filesystem::remove_all(directory);
}
