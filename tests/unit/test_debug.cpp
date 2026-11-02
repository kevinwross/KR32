#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/debug/debug.h"
#include "kr32/isa/encoding.h"
#include "kr32/link/linker.h"

#include <cstdint>
#include <string>

using namespace kr32;

namespace {

container::Image compile_debug_image(std::string_view source) {
    const cc::Result compiled = cc::compile(source, "debug-test.c");
    const std::string diagnostic =
        compiled.diagnostics.empty() ? "compile failed" : compiled.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(compiled.ok);

    container::Image user;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(assembler::assemble_object(compiled.assembly, user, assembly_error),
                    assembly_error.message.c_str());
    container::Image runtime;
    REQUIRE_MESSAGE(assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error),
                    assembly_error.message.c_str());
    container::Image image;
    linker::Map map;
    linker::Error link_error;
    REQUIRE_MESSAGE(linker::link({user, runtime}, linker::Options{}, image, map, link_error),
                    link_error.message.c_str());
    return image;
}

const debug::Line* find_line(const debug::Info& info, int line) {
    for (const debug::Line& candidate : info.lines) {
        if (candidate.line == line) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("debug info maps functions, variables, and source lines") {
    const container::Image image = compile_debug_image(
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main(void) {\n"
        "    int x = 10;\n"
        "    int y = 20;\n"
        "    return add(x, y);\n"
        "}\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    REQUIRE_FALSE(info.sources.empty());
    CHECK(info.sources.front().path == "debug-test.c");
    CHECK(info.functions.size() == 2);
    CHECK(info.variables.size() == 4);
    CHECK_FALSE(info.lines.empty());

    bool found_add = false;
    bool found_main = false;
    for (const debug::Function& function : info.functions) {
        found_add = found_add || function.name == "add";
        found_main = found_main || function.name == "main";
    }
    CHECK(found_add);
    CHECK(found_main);

    const debug::Line* line = find_line(info, 2);
    REQUIRE(line != nullptr);
    CHECK(line->source == 0);
    CHECK(line->column > 0);
}

TEST_CASE("debug info records float and double symbols") {
    const container::Image image = compile_debug_image(
        "double global_value = 4.5;\n"
        "double mix(int n, double d) {\n"
        "    float local_float = 1.5f;\n"
        "    return d + local_float + n;\n"
        "}\n"
        "int main(void) { return (int)mix(2, 3.5); }\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    bool found_float_type = false;
    bool found_double_type = false;
    for (const debug::TypeInfo& type : info.types) {
        found_float_type = found_float_type || (type.name == "float" && type.size == 4);
        found_double_type = found_double_type || (type.name == "double" && type.size == 8);
    }
    CHECK(found_float_type);
    CHECK(found_double_type);

    bool found_float_local = false;
    bool found_double_local = false;
    for (const debug::Variable& variable : info.variables) {
        found_float_local = found_float_local ||
                            (variable.name == "local_float" && variable.type == "float" &&
                             variable.size == 4);
        found_double_local = found_double_local ||
                             (variable.name == "d" && variable.type == "double" &&
                              variable.size == 8);
    }
    CHECK(found_float_local);
    CHECK(found_double_local);

    bool found_double_global = false;
    for (const debug::Global& global : info.globals) {
        found_double_global = found_double_global ||
                              (global.name == "global_value" && global.type == "double" &&
                               global.size == 8);
    }
    CHECK(found_double_global);
}

TEST_CASE("synthetic header includes preserve source line numbers") {
    const container::Image image = compile_debug_image(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    int value = 7;\n"
        "    printf(\"%d\\n\", value);\n"
        "    return value;\n"
        "}\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    REQUIRE(find_line(info, 3) != nullptr);
    REQUIRE(find_line(info, 4) != nullptr);
    CHECK(find_line(info, 23) == nullptr);
    for (const debug::Line& line : info.lines) {
        CHECK(line.line <= 6);
    }
}

TEST_CASE("line lookup is scoped to known functions") {
    const container::Image image = compile_debug_image(
        "int main(void) {\n"
        "    return 0;\n"
        "}\n");

    debug::Session session;
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Function* function = nullptr;
    for (const debug::Function& candidate : session.info().functions) {
        if (candidate.name == "main") {
            function = &candidate;
            break;
        }
    }
    REQUIRE(function != nullptr);
    session.machine().set_pc(function->end);
    CHECK(session.line_at(function->end) == nullptr);
}

TEST_CASE("DebugSession stops at breakpoints and inspects frames") {
    const container::Image image = compile_debug_image(
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main(void) {\n"
        "    int x = 10;\n"
        "    int y = 20;\n"
        "    return add(x, y);\n"
        "}\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    const debug::Line* line = find_line(info, 2);
    REQUIRE(line != nullptr);

    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    session.set_breakpoint(line->address);
    const core::StepResult result = session.run();
    CHECK(result.kind == core::StepResult::Kind::Retired);
    CHECK(session.at_breakpoint());
    CHECK(session.machine().pc() == line->address);

    std::uint32_t value = 0;
    REQUIRE_MESSAGE(session.read_variable("a", value, error), error.c_str());
    CHECK(value == 10);
    REQUIRE_MESSAGE(session.read_variable("b", value, error), error.c_str());
    CHECK(value == 20);

    const std::vector<debug::Frame> frames = session.call_stack();
    REQUIRE(frames.size() >= 2);
    CHECK(frames.front().function == "add");
    bool saw_main = false;
    for (const debug::Frame& frame : frames) {
        saw_main = saw_main || frame.function == "main";
    }
    CHECK(saw_main);

    const core::StepResult out = session.step_out();
    CHECK(out.kind == core::StepResult::Kind::Retired);
    const debug::Function* current = session.function_at(session.machine().pc());
    REQUIRE(current != nullptr);
    CHECK(current->name == "main");
}

TEST_CASE("DebugSession steps by source line") {
    const container::Image image = compile_debug_image(
        "int add(int a, int b) { return a + b; }\n"
        "int main(void) {\n"
        "    int x = 1;\n"
        "    int y = add(2, 3);\n"
        "    return y + x;\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Line* first = find_line(info, 3);
    REQUIRE(first != nullptr);
    session.set_breakpoint(first->address);
    (void)session.run();
    REQUIRE(session.at_breakpoint());

    const core::StepResult line = session.step_line_in();
    CHECK(line.kind == core::StepResult::Kind::Retired);
    const debug::Line* next = session.line_at(session.machine().pc());
    REQUIRE(next != nullptr);
    CHECK(next->line != first->line);
}

TEST_CASE("DebugSession steps over and out by source line") {
    const container::Image image = compile_debug_image(
        "int add(int a, int b) { return a + b; }\n"
        "int main(void) {\n"
        "    int x = 1;\n"
        "    int y = add(2, 3);\n"
        "    return y + x;\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Line* call_line = find_line(info, 4);
    REQUIRE(call_line != nullptr);
    session.set_breakpoint(call_line->address);
    (void)session.run();
    REQUIRE(session.at_breakpoint());

    const core::StepResult over = session.step_line_over();
    CHECK(over.kind == core::StepResult::Kind::Retired);
    const debug::Function* function = session.function_at(session.machine().pc());
    REQUIRE(function != nullptr);
    CHECK(function->name == "main");
    const debug::Line* after = session.line_at(session.machine().pc());
    REQUIRE(after != nullptr);
    CHECK(after->line != call_line->line);

    const core::StepResult out = session.step_line_out();
    CHECK(out.kind == core::StepResult::Kind::Retired);
}

TEST_CASE("DebugSession captures console output") {
    const container::Image image = compile_debug_image(
        "int putchar(int c);\n"
        "void exit(int code);\n"
        "int main(void) {\n"
        "    putchar('O');\n"
        "    putchar('K');\n"
        "    return 0;\n"
        "}\n");
    debug::Session session;
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run();
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.console() == "OK");
    CHECK(session.machine().exit_code() == 0);
}

TEST_CASE("DebugSession steps over a call") {
    const container::Image image = compile_debug_image(
        "int twice(int value) { return value + value; }\n"
        "int main(void) {\n"
        "    int result = twice(21);\n"
        "    return result;\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Function* main_function = nullptr;
    for (const debug::Function& function : info.functions) {
        if (function.name == "main") {
            main_function = &function;
        }
    }
    REQUIRE(main_function != nullptr);
    session.set_breakpoint(main_function->start);
    (void)session.run();
    CHECK(session.at_breakpoint());

    session.clear_breakpoints();
    bool at_call = false;
    for (int guard = 0; guard < 100 && !at_call; ++guard) {
        std::uint32_t words[2] = {0, 0};
        REQUIRE(session.machine().read32(session.machine().pc(), words[0]));
        (void)session.machine().read32(session.machine().pc() + 4, words[1]);
        isa::Instruction instruction;
        if (isa::decode(words, 2, instruction) && instruction.opcode == 0x3AU) {
            at_call = true;
            break;
        }
        (void)session.step();
    }
    CHECK(at_call);
    const core::StepResult result = session.step_over();
    CHECK(result.kind == core::StepResult::Kind::Retired);
    const debug::Function* current = session.function_at(session.machine().pc());
    REQUIRE(current != nullptr);
    CHECK(current->name == "main");

    std::uint32_t value = 0;
    for (int guard = 0; guard < 4; ++guard) {
        if (session.read_variable("result", value, error) && value == 42) {
            break;
        }
        (void)session.step();
    }
    CHECK(value == 42);
}
