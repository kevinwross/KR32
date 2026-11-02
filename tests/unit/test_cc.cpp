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
