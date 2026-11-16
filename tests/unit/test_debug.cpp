#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/cxx/cxx.h"
#include "kr32/debug/debug.h"
#include "kr32/isa/encoding.h"
#include "kr32/link/linker.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

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

container::Image compile_cxx_debug_image(std::string_view source) {
    const cxx::Result compiled = cxx::compile(source);
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
        if (candidate.source == 0 && candidate.line == line) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("debug sessions expose guest command-line arguments") {
    const container::Image image = compile_debug_image(
        "int sys_argc(void);\n"
        "int sys_arg(int index, char *buffer, int size);\n"
        "int main(void) {\n"
        "    char buffer[16];\n"
        "    if (sys_argc() != 2) return 1;\n"
        "    if (sys_arg(1, buffer, 16) != 5) return 2;\n"
        "    return buffer[0] == 'h' ? 0 : 3;\n"
        "}\n");
    debug::Session session;
    session.set_arguments({"first", "hello"});
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run(100000);
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.machine().exit_code() == 0);
}

TEST_CASE("debug sessions handle the host syscall range") {
    const container::Image image = compile_debug_image(
        "int sys_random(void);\n"
        "int sys_time(void);\n"
        "int getchar(void);\n"
        "int open(const char *path, int flags);\n"
        "int close(int fd);\n"
        "int read(int fd, void *buffer, int count);\n"
        "int write(int fd, const void *buffer, int count);\n"
        "int seek(int fd, int offset, int whence);\n"
        "int main(void) {\n"
        "    if (sys_random() == 0) return 1;\n"
        "    if (sys_random() == 0) return 2;\n"
        "    if (sys_time() == 0) return 3;\n"
        "    if (getchar() != 'Q') return 4;\n"
        "    if (getchar() != -1) return 5;\n"
        "    if (open(\"data.txt\", 0) >= 0) return 6;\n"
        "    if (close(0) >= 0) return 7;\n"
        "    char buffer[1];\n"
        "    if (read(0, buffer, 1) >= 0) return 8;\n"
        "    if (write(0, buffer, 1) >= 0) return 9;\n"
        "    if (seek(0, 0, 0) >= 0) return 10;\n"
        "    return 0;\n"
        "}\n");
    debug::Session session;
    session.set_input("Q");
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run(100000);
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.machine().exit_code() == 0);
}

TEST_CASE("debug sessions expose the file dialog syscall") {
    const std::filesystem::path selected =
        std::filesystem::current_path() / "kr32-debug-dialog.txt";
    {
        std::ofstream output(selected, std::ios::binary);
        output << 'Q';
    }
    INFO(selected.string());
    CHECK(std::filesystem::exists(selected));
    const container::Image image = compile_debug_image(
        "int sys_open_dialog(char *path, int capacity);\n"
        "int open(const char *path, int flags);\n"
        "int close(int fd);\n"
        "int read(int fd, void *buffer, int count);\n"
        "int main(void) {\n"
        "    char path[512];\n"
        "    int length = sys_open_dialog(path, 512);\n"
        "    if (length <= 0) return 1;\n"
        "    if (path[length] != 0) return 2;\n"
        "    int fd = open(path, 0);\n"
        "    if (fd < 0) return 3;\n"
        "    char value = 0;\n"
        "    if (read(fd, &value, 1) != 1) return 4;\n"
        "    close(fd);\n"
        "    return value == 'Q' ? 0 : 5;\n"
        "}\n");
    debug::Session session;
    session.set_file_dialog([&](const FileDialogOptions& options) {
        CHECK(options.kind == FileDialogKind::Open);
        CHECK(options.must_exist);
        return FileDialogResult{FileDialogStatus::Selected, selected.string(), {}};
    });
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run(100000);
    INFO(session.host_error());
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.machine().exit_code() == 0);
    std::error_code cleanup;
    std::filesystem::remove(selected, cleanup);
}

TEST_CASE("C++ iostreams use the FILE host layer") {
    const std::filesystem::path path =
        std::filesystem::current_path() / "cxx-stream-test.txt";
    const container::Image image = compile_cxx_debug_image(
        "#include <fstream>\n"
        "int main() {\n"
        "    std::ofstream out(\"cxx-stream-test.txt\");\n"
        "    if (!out.is_open()) return 1;\n"
        "    out << \"hello \" << 42 << std::endl;\n"
        "    out.close();\n"
        "    std::ifstream in(\"cxx-stream-test.txt\");\n"
        "    if (!in.is_open()) return 2;\n"
        "    char word[16];\n"
        "    int value = 0;\n"
        "    in >> word >> value;\n"
        "    in.close();\n"
        "    std::cout << word << \" \" << value << std::endl;\n"
        "    return value == 42 ? 0 : 3;\n"
        "}\n");
    debug::Session session;
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run(100000);
    INFO(session.host_error());
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.machine().exit_code() == 0);
    CHECK(session.console() == "hello 42\n");
    std::error_code cleanup;
    std::filesystem::remove(path, cleanup);
}

TEST_CASE("C++ debug sessions expose guest command-line arguments") {
    const container::Image image = compile_cxx_debug_image(
        "int main(int argc, char** argv) {\n"
        "    if (argc != 2) return 1;\n"
        "    return argv[1][0] == 'h' ? 0 : 2;\n"
        "}\n");
    debug::Session session;
    session.set_arguments({"hello"});
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run(100000);
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.machine().exit_code() == 0);
}

TEST_CASE("C++ debug info covers templates and lambdas") {
    const container::Image image = compile_cxx_debug_image(
        "template <typename T> T twice(T value) { return value + value; }\n"
        "int main() {\n"
        "    int value = 20;\n"
        "    auto increment = [](int input) { return input + 1; };\n"
        "    return twice(value) + increment(0);\n"
        "}\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    CHECK_FALSE(info.functions.empty());
    CHECK_FALSE(info.lines.empty());
    CHECK_FALSE(info.variables.empty());
    bool found_main = false;
    for (const debug::Function& function : info.functions) {
        if (function.name == "main") {
            found_main = true;
        }
    }
    CHECK(found_main);
}

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
    CHECK_FALSE(info.lines.empty());
    std::size_t user_variables = 0;
    for (const debug::Variable& variable : info.variables) {
        if (variable.function == "add" || variable.function == "main") {
            ++user_variables;
        }
    }
    CHECK(user_variables == 4);

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
        if (line.source == 0) {
            CHECK(line.line <= 6);
        }
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

TEST_CASE("libc functions carry switchable source records") {
    const container::Image image = compile_debug_image(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    printf(\"ok\\n\");\n"
        "    return 0;\n"
        "}\n");

    debug::Session session;
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Function* printf_function = nullptr;
    for (const debug::Function& candidate : session.info().functions) {
        if (candidate.name == "printf") {
            printf_function = &candidate;
            break;
        }
    }
    REQUIRE(printf_function != nullptr);
    bool found_libc_source = false;
    int libc_source_id = -1;
    for (const debug::Source& source : session.info().sources) {
        if (source.path == "src/cc/libc.c") {
            found_libc_source = true;
            libc_source_id = source.id;
        }
    }
    CHECK(found_libc_source);
    REQUIRE(libc_source_id >= 0);

    session.machine().set_pc(printf_function->start + 16);
    const debug::Line* line = session.line_at(session.machine().pc());
    REQUIRE(line != nullptr);
    CHECK(line->source == libc_source_id);
}

TEST_CASE("assembly debug line labels are unique per source") {
    container::Image first;
    container::Image second;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(
        assembler::assemble_object(
            ".file \"first.S\"\n"
            ".text\n"
            ".global first\n"
            "first:\n"
            "    MOV R0, #1\n"
            "    RET\n",
            first, assembly_error),
        assembly_error.message.c_str());
    REQUIRE_MESSAGE(
        assembler::assemble_object(
            ".file \"second.S\"\n"
            ".text\n"
            ".global second\n"
            "second:\n"
            "    MOV R0, #2\n"
            "    RET\n",
            second, assembly_error),
        assembly_error.message.c_str());

    container::Image linked;
    linker::Map map;
    linker::Error link_error;
    REQUIRE_MESSAGE(linker::link({first, second}, linker::Options{}, linked,
                                 map, link_error),
                    link_error.message.c_str());
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(linked, info, error), error.c_str());

    int first_source = -1;
    int second_source = -1;
    for (const debug::Source& source : info.sources) {
        if (source.path == "first.S") first_source = source.id;
        if (source.path == "second.S") second_source = source.id;
    }
    CHECK(first_source >= 0);
    CHECK(second_source >= 0);

    std::uint32_t first_line = 0;
    std::uint32_t second_line = 0;
    for (const debug::Line& line : info.lines) {
        if (line.source == first_source && line.line == 5) {
            first_line = line.address;
        }
        if (line.source == second_source && line.line == 5) {
            second_line = line.address;
        }
    }
    CHECK(first_line != 0);
    CHECK(second_line != 0);
    CHECK(first_line != second_line);
}

TEST_CASE("breakpoints can be disabled") {
    const container::Image image = compile_debug_image(
        "int main(void) {\n"
        "    return 0;\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    const debug::Line* line = find_line(info, 2);
    REQUIRE(line != nullptr);
    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const std::uint32_t initial_pc = session.machine().pc();
    session.set_breakpoint(initial_pc, false);
    session.set_breakpoint(line->address);
    (void)session.run();
    CHECK(session.machine().pc() == line->address);
    CHECK(session.at_breakpoint());
    session.clear_breakpoints();
    session.machine().set_pc(initial_pc);
    session.set_breakpoint(line->address, false);
    CHECK(session.breakpoints().count(line->address) == 1);
    CHECK(!session.breakpoint_enabled(line->address));
    session.machine().set_pc(line->address);
    CHECK(!session.at_breakpoint());
    session.set_breakpoint_enabled(line->address, true);
    CHECK(session.breakpoint_enabled(line->address));
    CHECK(session.at_breakpoint());
    session.clear_breakpoint(line->address);
    CHECK(session.breakpoints().empty());
    CHECK(session.disabled_breakpoints().empty());
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

TEST_CASE("DebugSession steps over a call that runs millions of steps") {
    // The callee takes roughly 1.6M instructions, past the old one-million
    // guard; stepping over used to stop inside it instead of at the next line.
    const container::Image image = compile_debug_image(
        "static int spin(int n) {\n"
        "    int total = 0;\n"
        "    while (n > 0) {\n"
        "        total += n;\n"
        "        --n;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main(void) {\n"
        "    int total = spin(200000);\n"
        "    return total;\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Line* call_line = find_line(info, 10);
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
    CHECK(after->line == 11);
}

TEST_CASE("DebugSession steps out of a called function") {
    const container::Image image = compile_debug_image(
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
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
    const debug::Line* inside = find_line(info, 2);
    REQUIRE(inside != nullptr);
    session.set_breakpoint(inside->address);
    (void)session.run();
    REQUIRE(session.at_breakpoint());
    const debug::Function* callee = session.function_at(session.machine().pc());
    REQUIRE(callee != nullptr);
    CHECK(callee->name == "add");
    const core::StepResult out = session.step_line_out();
    CHECK(out.kind == core::StepResult::Kind::Retired);
    const debug::Function* caller = session.function_at(session.machine().pc());
    REQUIRE(caller != nullptr);
    CHECK(caller->name == "main");
    const debug::Line* caller_line =
        session.line_at(session.machine().pc());
    REQUIRE(caller_line != nullptr);
    CHECK(caller_line->line == 6);
}

TEST_CASE("DebugSession steps over a false if-body") {
    const container::Image image = compile_debug_image(
        "int main(void) {\n"
        "    int x = 1;\n"
        "    if (x == 2) {\n"
        "        return 9;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Line* condition = find_line(info, 3);
    REQUIRE(condition != nullptr);
    session.set_breakpoint(condition->address);
    (void)session.run();
    REQUIRE(session.at_breakpoint());

    const core::StepResult over = session.step_line_over();
    CHECK(over.kind == core::StepResult::Kind::Retired);
    const debug::Line* after = session.line_at(session.machine().pc());
    REQUIRE(after != nullptr);
    CHECK(after->line > condition->line);
    CHECK(after->line != 4);
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

TEST_CASE("debug sessions expose and select thread snapshots") {
    debug::Session session;
    debug::ThreadInfo main;
    main.id = 1;
    main.name = "main";
    main.pc = 0x1000;
    main.current = true;
    debug::ThreadInfo worker;
    worker.id = 2;
    worker.name = "worker";
    worker.pc = 0x2000;
    session.set_threads({main, worker});
    REQUIRE(session.threads().size() == 2);
    CHECK(session.selected_thread() == 1);
    session.select_thread(2);
    CHECK(session.selected_thread() == 2);
    session.select_thread(99);
    CHECK(session.selected_thread() == 2);
}

TEST_CASE("debug sessions treat a zero step limit as unlimited") {
    const container::Image image = compile_debug_image(
        "int main(void) {\n"
        "    return 0;\n"
        "}\n");
    debug::Session session;
    std::string error;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const core::StepResult result = session.run(0);
    CHECK(result.kind == core::StepResult::Kind::Halted);
    CHECK(session.machine().exit_code() == 0);
}

TEST_CASE("C debug info describes struct members and arrays") {
    const container::Image image = compile_debug_image(
        "struct pair { int left; int right; };\n"
        "int main(void) {\n"
        "    struct pair value;\n"
        "    int items[4];\n"
        "    value.left = 1;\n"
        "    items[0] = 2;\n"
        "    return value.left + items[0];\n"
        "}\n");
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    const debug::ClassInfo* pair = nullptr;
    for (const debug::ClassInfo& klass : info.classes) {
        if (klass.name == "struct:pair") {
            pair = &klass;
            break;
        }
    }
    REQUIRE(pair != nullptr);
    REQUIRE(pair->members.size() == 2);
    CHECK(pair->members[0].name == "left");
    CHECK(pair->members[0].offset == 0);
    CHECK(pair->members[1].name == "right");
    CHECK(pair->members[1].offset == 4);

    bool found_struct = false;
    bool found_array = false;
    for (const debug::Variable& variable : info.variables) {
        if (variable.name == "value" && variable.type == "struct:pair") {
            found_struct = true;
        }
        if (variable.name == "items" && variable.type == "int[4]") {
            found_array = true;
        }
    }
    CHECK(found_struct);
    CHECK(found_array);
}
TEST_CASE("a function entry maps to its declaration line") {
    // The prologue runs before the first statement marker, so without a marker
    // on the declaration line the debugger shows no source at the entry and
    // cannot take a breakpoint there.
    const container::Image image = compile_debug_image(
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main(void) {\n"
        "    return add(1, 2);\n"
        "}\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    const std::pair<const char*, int> expected[] = {{"add", 1}, {"main", 4}};
    for (const auto& [name, declaration_line] : expected) {
        const debug::Function* function = nullptr;
        for (const debug::Function& candidate : info.functions) {
            if (candidate.name == name) {
                function = &candidate;
            }
        }
        REQUIRE(function != nullptr);

        const debug::Line* entry = nullptr;
        for (const debug::Line& candidate : info.lines) {
            if (candidate.address == function->start) {
                entry = &candidate;
                break;
            }
        }
        INFO(name);
        REQUIRE(entry != nullptr);
        CHECK(entry->line == declaration_line);
    }
}
TEST_CASE("a C++ function entry maps to its declaration line") {
    const container::Image image = compile_cxx_debug_image(
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main() {\n"
        "    return add(1, 2);\n"
        "}\n");

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());

    const std::pair<const char*, int> expected[] = {{"add", 1}, {"main", 4}};
    for (const auto& [name, declaration_line] : expected) {
        const debug::Function* function = nullptr;
        for (const debug::Function& candidate : info.functions) {
            if (candidate.name == name) {
                function = &candidate;
            }
        }
        INFO(name);
        REQUIRE(function != nullptr);

        const debug::Line* entry = nullptr;
        for (const debug::Line& candidate : info.lines) {
            if (candidate.address == function->start) {
                entry = &candidate;
                break;
            }
        }
        REQUIRE(entry != nullptr);
        CHECK(entry->line == declaration_line);
    }
}
TEST_CASE("a promoted local reports its register location") {
    // The optimizer rewrites a promoted slot's stores into register moves, so
    // the frame slot is stale for the whole interval. The P record says where
    // the value really is, and the session reads it from there.
    const cc::Result compiled = cc::compile(
        "int compute(int seed) {\n"
        "    int total = seed;\n"
        "    int index = 0;\n"
        "    while (index < 3) {\n"
        "        total = total + index;\n"
        "        index = index + 1;\n"
        "    }\n"
        "    return total + index;\n"
        "}\n"
        "int main(void) {\n"
        "    return compute(4);\n"
        "}\n",
        "debug-test.c", 0, true);
    REQUIRE_MESSAGE(compiled.ok, "compile failed");

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

    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    REQUIRE_FALSE(info.locations.empty());
    bool found = false;
    for (const debug::Location& location : info.locations) {
        if (location.function == "compute" && location.name == "total") {
            found = true;
            CHECK(location.start < location.end);
            CHECK(location.reg >= 2);
            CHECK_FALSE(location.fp);
        }
    }
    CHECK(found);

    // The session reads the register the record names, not the stale frame slot.
    debug::Session session;
    REQUIRE_MESSAGE(session.load(image, error), error.c_str());
    const debug::Location* location = nullptr;
    for (int step = 0; step < 100000 && location == nullptr; ++step) {
        location = session.location_at("compute", "total", session.machine().pc());
        if (location == nullptr) {
            (void)session.machine().step();
        }
    }
    REQUIRE(location != nullptr);
    session.machine().set_reg(static_cast<std::size_t>(location->reg), 1234);
    std::uint32_t value = 0;
    REQUIRE_MESSAGE(session.read_variable("total", value, error), error.c_str());
    CHECK(value == 1234);
}