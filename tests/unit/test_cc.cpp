#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/link/linker.h"

#include <cstdint>
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
    REQUIRE_MESSAGE(linker::link({user, runtime}, linker::Options{}, image, map, link_error),
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
        return false;
    });

    for (int step = 0; step < 10000; ++step) {
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
