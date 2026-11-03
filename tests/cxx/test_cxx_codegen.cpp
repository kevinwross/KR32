#include "doctest.h"

#include "cxx/codegen.h"
#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/cxx/cxx.h"
#include "kr32/debug/debug.h"
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
    std::string diagnostics;
};

ProgramResult run_assemblies(const std::vector<std::string>& user_assemblies) {
    ProgramResult program;
    std::vector<container::Image> objects;
    assembler::Error assembly_error;
    for (const std::string& assembly : user_assemblies) {
        container::Image object;
        if (!assembler::assemble_object(assembly, object, assembly_error)) {
            program.diagnostics = assembly_error.message;
            return program;
        }
        objects.push_back(std::move(object));
    }
    container::Image runtime;
    if (!assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error)) {
        program.diagnostics = assembly_error.message;
        return program;
    }
    objects.push_back(std::move(runtime));

    container::Image image;
    linker::Map map;
    linker::Error link_error;
    linker::Options link_options;
    link_options.stack_top = 0x0011'0000;
    link_options.stack_limit = 0x0001'0000;
    if (!linker::link(objects, link_options, image, map, link_error)) {
        program.diagnostics = link_error.message;
        return program;
    }

    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        if (!machine.load_image(section.address, section.data)) {
            program.diagnostics = "failed to load image";
            return program;
        }
    }
    machine.reset(image.entry);
    machine.set_tsp(machine.ram_base() + 512U * 1024U);
    machine.set_reg(11, machine.ram_base() + machine.ram_size() - 1024U);
    for (const container::Section& section : image.sections) {
        if (section.name == ".tls") {
            for (std::size_t index = 0; index < section.data.size(); ++index) {
                (void)machine.write8(
                    machine.reg(11) + 32U + static_cast<std::uint32_t>(index),
                    section.data[index]);
            }
        }
    }
    machine.set_syscall_handler([&](core::Machine& current, std::uint32_t vector) {
        if (vector == 33) {
            program.output.push_back(static_cast<char>(current.reg(0) & 0xFFU));
            return true;
        }
        if (vector == 32) {
            current.halt(current.reg(0));
            return true;
        }
        if (vector >= 44 && vector <= 50) {
            current.set_reg(0, 0);
            return true;
        }
        return false;
    });

    for (int step = 0; step < 200000; ++step) {
        const core::StepResult step_result = machine.step();
        if (step_result.kind == core::StepResult::Kind::Halted) {
            program.result = machine.exit_code_set() ? machine.exit_code()
                                                     : machine.reg(0);
            program.ok = true;
            return program;
        }
        if (step_result.kind == core::StepResult::Kind::Raised) {
            program.diagnostics = "machine fault";
            return program;
        }
    }
    program.diagnostics = "step limit";
    return program;
}

ProgramResult compile_and_run(std::string_view source) {
    const cxx::Result compiled = cxx::compile(source);
    if (!compiled.ok) {
        ProgramResult failed;
        for (const cxx::Diagnostic& diagnostic : compiled.diagnostics) {
            failed.diagnostics += std::to_string(diagnostic.line) + ":" +
                                  std::to_string(diagnostic.column) + ": " +
                                  diagnostic.message + "\n";
        }
        return failed;
    }
    return run_assemblies({compiled.assembly});
}

ProgramResult compile_and_run_pair(std::string_view first, std::string_view second) {
    const cxx::Result first_result = cxx::compile(first);
    const cxx::Result second_result = cxx::compile(second);
    if (!first_result.ok || !second_result.ok) {
        ProgramResult failed;
        for (const cxx::Result* result : {&first_result, &second_result}) {
            for (const cxx::Diagnostic& diagnostic : result->diagnostics) {
                failed.diagnostics += std::to_string(diagnostic.line) + ":" +
                                      std::to_string(diagnostic.column) + ": " +
                                      diagnostic.message + "\n";
            }
        }
        return failed;
    }
    return run_assemblies({first_result.assembly, second_result.assembly});
}

ProgramResult compile_and_run_many(const std::vector<std::string>& sources) {
    std::vector<std::string> assemblies;
    ProgramResult failed;
    for (const std::string& source : sources) {
        const cxx::Result compiled = cxx::compile(source);
        if (!compiled.ok) {
            for (const cxx::Diagnostic& diagnostic : compiled.diagnostics) {
                failed.diagnostics += std::to_string(diagnostic.line) + ":" +
                                      std::to_string(diagnostic.column) + ": " +
                                      diagnostic.message + "\n";
            }
            return failed;
        }
        assemblies.push_back(compiled.assembly);
    }
    return run_assemblies(assemblies);
}

} // namespace

TEST_CASE("mangling matches the documented itanium-like examples") {
    const auto int_type =
        cxx::ast::Type::make_builtin(cxx::ast::BuiltinKind::Int);
    CHECK(cxx::mangle_function("add", {int_type, int_type}, false) == "_Z3addii");
    CHECK(cxx::mangle_function("math::twice", {int_type}, false) ==
          "_ZN4math5twiceEi");
    CHECK(cxx::mangle_function("f", {}, false) == "_Z1fv");
    CHECK(cxx::mangle_function("putchar", {int_type}, true) == "putchar");
    CHECK(cxx::mangle_variable("ns::value", false) == "_ZN2ns5valueE");
}

TEST_CASE("C++ functions compile, link, and return a value") {
    const ProgramResult program = compile_and_run(
        "int add(int a, int b) { return a + b; }\n"
        "int main() { return add(20, 22); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ recursion and control flow run") {
    const ProgramResult program = compile_and_run(
        "int fib(int n) {\n"
        "    if (n < 2) { return n; }\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "int main() { return fib(10); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 55);
}

TEST_CASE("namespaces and overloads run") {
    const ProgramResult program = compile_and_run(
        "namespace math {\n"
        "    int twice(int value) { return value * 2; }\n"
        "    int twice(int value, int other) { return value * 2 + other * 2; }\n"
        "}\n"
        "int main() { return math::twice(5) + math::twice(1, 2); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 16);
}

TEST_CASE("references pass by address") {
    const ProgramResult program = compile_and_run(
        "void bump(int& value) { value = value + 1; }\n"
        "int main() { int value = 41; bump(value); return value; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("arrays, pointers, and loops run") {
    const ProgramResult program = compile_and_run(
        "int sum(int* values, int count) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < count; ++i) { total += values[i]; }\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    int values[3];\n"
        "    values[0] = 10;\n"
        "    values[1] = 20;\n"
        "    values[2] = 12;\n"
        "    return sum(values, 3);\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("global variables initialize and update") {
    const ProgramResult program = compile_and_run(
        "int counter = 40;\n"
        "int bump() { counter = counter + 2; return counter; }\n"
        "int main() { return bump(); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ calls extern C runtime functions") {
    const ProgramResult program = compile_and_run(
        "extern \"C\" int putchar(int);\n"
        "int main() { putchar('H'); putchar('i'); return 0; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.output == "Hi");
    CHECK(program.result == 0);
}

TEST_CASE("multiple translation units link through mangled symbols") {
    const ProgramResult program = compile_and_run_pair(
        "int helper(int value);\n"
        "int main() { return helper(40); }\n",
        "int helper(int value) { return value + 2; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("using declarations and using namespace resolve names") {
    const ProgramResult declaration = compile_and_run(
        "namespace math { int twice(int value) { return value * 2; } }\n"
        "using math::twice;\n"
        "int main() { return twice(21); }\n");
    INFO(declaration.diagnostics);
    REQUIRE(declaration.ok);
    CHECK(declaration.result == 42);

    const ProgramResult directive = compile_and_run(
        "namespace math { int twice(int value) { return value * 2; } }\n"
        "using namespace math;\n"
        "int main() { return twice(21); }\n");
    INFO(directive.diagnostics);
    REQUIRE(directive.ok);
    CHECK(directive.result == 42);
}

TEST_CASE("double arithmetic, calls, and returns use FPRs") {
    const ProgramResult program = compile_and_run(
        "double half(double value) { return value / 2.0; }\n"
        "int main() { return (int)half(84.0); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("mixed integer and FP arguments use independent register banks") {
    const ProgramResult program = compile_and_run(
        "double mix(int a, double b, int c, double d) {\n"
        "    return a + b + c + d;\n"
        "}\n"
        "int main() { return (int)mix(1, 2.5, 3, 4.5); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 11);
}

TEST_CASE("FP locals, loops, comparisons, and spills run") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    double total = 0.0;\n"
        "    for (int i = 0; i < 10; ++i) { total = total + 1.5; }\n"
        "    if (total > 14.0) { return (int)total; }\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 15);
}

TEST_CASE("FP globals use float and double data directives") {
    const ProgramResult program = compile_and_run(
        "double scale = 2.5;\n"
        "float bias = 1.5f;\n"
        "int main() { return (int)(scale + bias); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 4);
}

TEST_CASE("FP and integer conversions run") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    double value = 3.75;\n"
        "    int whole = (int)value;\n"
        "    float small = (float)whole;\n"
        "    double widened = (double)small;\n"
        "    return (int)widened + 39;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("references to double pass by address") {
    const ProgramResult program = compile_and_run(
        "void scale(double& value) { value = value * 2.0; }\n"
        "int main() { double value = 21.0; scale(value); return (int)value; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("float return values and float arithmetic run") {
    const ProgramResult program = compile_and_run(
        "float half(float value) { return value / 2.0f; }\n"
        "int main() { return (int)(half(84.0f) + 0.5f); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("stack-passed integer varargs and va_list run") {
    const ProgramResult program = compile_and_run(
        "int sum(int count, ...) {\n"
        "    va_list ap;\n"
        "    __builtin_va_start(ap, count);\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < count; ++i) {\n"
        "        total += __builtin_va_arg(ap, int);\n"
        "    }\n"
        "    __builtin_va_end(ap);\n"
        "    return total;\n"
        "}\n"
        "int main() { return sum(3, 10, 20, 12); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("promoted float and double varargs read through va_list") {
    const ProgramResult program = compile_and_run(
        "double sum(int count, ...) {\n"
        "    va_list ap;\n"
        "    __builtin_va_start(ap, count);\n"
        "    double total = 0.0;\n"
        "    for (int i = 0; i < count; ++i) {\n"
        "        total += __builtin_va_arg(ap, double);\n"
        "    }\n"
        "    __builtin_va_end(ap);\n"
        "    return total;\n"
        "}\n"
        "int main() { return (int)sum(3, 10.5, 20.25f, 11.25); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("long double is a double alias and NaN comparisons run") {
    const ProgramResult alias = compile_and_run(
        "long double widen(long double value) { return value * 2.0; }\n"
        "int main() { return (int)widen(21.0); }\n");
    INFO(alias.diagnostics);
    REQUIRE(alias.ok);
    CHECK(alias.result == 42);

    const ProgramResult nan = compile_and_run(
        "int main() {\n"
        "    double zero = 0.0;\n"
        "    double value = zero / zero;\n"
        "    if (value != value) { return 42; }\n"
        "    return 0;\n"
        "}\n");
    INFO(nan.diagnostics);
    REQUIRE(nan.ok);
    CHECK(nan.result == 42);
}

TEST_CASE("virtual dispatch and RTTI run through vtables") {
    const ProgramResult dispatch = compile_and_run(
        "class Base {\n"
        "public:\n"
        "    Base() { }\n"
        "    virtual int value() { return 1; }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    Derived() { }\n"
        "    int value() override { return 42; }\n"
        "};\n"
        "int main() { Derived object; Base* pointer = &object; return pointer->value(); }\n");
    INFO(dispatch.diagnostics);
    REQUIRE(dispatch.ok);
    CHECK(dispatch.result == 42);

    const ProgramResult cast = compile_and_run(
        "class Base {\n"
        "public:\n"
        "    Base() { }\n"
        "    virtual int value() { return 1; }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    Derived() { }\n"
        "    int value() override { return 42; }\n"
        "};\n"
        "int main() {\n"
        "    Derived object;\n"
        "    Base* base = &object;\n"
        "    Derived* down = dynamic_cast<Derived*>(base);\n"
        "    if (down == 0) { return 1; }\n"
        "    if (typeid(base) != typeid(Derived*)) { return 2; }\n"
        "    return down->value();\n"
        "}\n");
    INFO(cast.diagnostics);
    REQUIRE(cast.ok);
    CHECK(cast.result == 42);
}

TEST_CASE("failed dynamic_cast returns null") {
    const ProgramResult program = compile_and_run(
        "class Base {\n"
        "public:\n"
        "    Base() { }\n"
        "    virtual int value() { return 1; }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    Derived() { }\n"
        "    int value() override { return 42; }\n"
        "};\n"
        "int main() {\n"
        "    Base object;\n"
        "    Derived* down = dynamic_cast<Derived*>(&object);\n"
        "    return down == 0 ? 42 : 0;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("abstract classes cannot be instantiated") {
    const cxx::Result result = cxx::compile(
        "class Abstract {\n"
        "public:\n"
        "    virtual int value() = 0;\n"
        "};\n"
        "int main() { Abstract value; return 0; }\n");
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("abstract") != std::string::npos);
}

TEST_CASE("matrix intrinsics lower to tile instructions") {
    const ProgramResult product = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4 a; a.m[0] = 2.0; a.m[5] = 2.0; a.m[10] = 2.0; a.m[15] = 2.0;\n"
        "    kr32::mat4 b; b.m[0] = 3.0; b.m[5] = 3.0; b.m[10] = 3.0; b.m[15] = 3.0;\n"
        "    kr32::mat4 c = kr32::matrix_mul(a, b);\n"
        "    return c.m[0] == 6.0 && c.m[5] == 6.0 ? 42 : 0;\n"
        "}\n");
    INFO(product.diagnostics);
    REQUIRE(product.ok);
    CHECK(product.result == 42);

    const ProgramResult transform = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4 m; m.m[0] = 1.0; m.m[5] = 1.0; m.m[10] = 1.0; m.m[15] = 1.0;\n"
        "    kr32::vec4 v; v.x = 2.0; v.y = 3.0; v.z = 4.0; v.w = 1.0;\n"
        "    kr32::vec4 r = kr32::transform(m, v);\n"
        "    return r.x == 2.0 && r.y == 3.0 && r.z == 4.0 ? 42 : 0;\n"
        "}\n");
    INFO(transform.diagnostics);
    REQUIRE(transform.ok);
    CHECK(transform.result == 42);

    const ProgramResult dot = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::vec4 a; a.x = 1.0; a.y = 2.0; a.z = 3.0; a.w = 4.0;\n"
        "    kr32::vec4 b; b.x = 1.0; b.y = 1.0; b.z = 1.0; b.w = 1.0;\n"
        "    return kr32::dot(a, b) == 10.0 ? 42 : 0;\n"
        "}\n");
    INFO(dot.diagnostics);
    REQUIRE(dot.ok);
    CHECK(dot.result == 42);
}

TEST_CASE("matrix transpose, multiply-add, and transform stack run") {
    const ProgramResult transpose = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4 a; a.m[0] = 1.0; a.m[1] = 7.0; a.m[15] = 1.0;\n"
        "    kr32::mat4 b = kr32::transpose(a);\n"
        "    return b.m[4] == 7.0 ? 42 : 0;\n"
        "}\n");
    INFO(transpose.diagnostics);
    REQUIRE(transpose.ok);
    CHECK(transpose.result == 42);

    const ProgramResult add = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4 a; a.m[0] = 2.0; a.m[15] = 1.0;\n"
        "    kr32::mat4 b; b.m[0] = 3.0; b.m[15] = 1.0;\n"
        "    kr32::mat4 c; c.m[0] = 4.0;\n"
        "    kr32::mat4 r = kr32::matrix_mul_add(a, b, c);\n"
        "    return r.m[0] == 10.0 ? 42 : 0;\n"
        "}\n");
    INFO(add.diagnostics);
    REQUIRE(add.ok);
    CHECK(add.result == 42);

    const ProgramResult stack = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4 a; a.m[0] = 5.0;\n"
        "    kr32::push(a);\n"
        "    kr32::mat4 b;\n"
        "    kr32::pop(b);\n"
        "    kr32::drop();\n"
        "    return b.m[0] == 5.0 ? 42 : 0;\n"
        "}\n");
    INFO(stack.diagnostics);
    REQUIRE(stack.ok);
    CHECK(stack.result == 42);

    const ProgramResult alias = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4 a; a.m[0] = 2.0; a.m[15] = 1.0;\n"
        "    kr32::mat4 b = kr32::matrix_mul(a, a);\n"
        "    a = kr32::matrix_mul(a, b);\n"
        "    return a.m[0] == 8.0 ? 42 : 0;\n"
        "}\n");
    INFO(alias.diagnostics);
    REQUIRE(alias.ok);
    CHECK(alias.result == 42);
}

TEST_CASE("try/catch handles thrown scalars and nested handlers") {
    const ProgramResult scalar = compile_and_run(
        "int main() {\n"
        "    try { throw 42; } catch (int value) { return value; }\n"
        "    return 0;\n"
        "}\n");
    INFO(scalar.diagnostics);
    REQUIRE(scalar.ok);
    CHECK(scalar.result == 42);

    const ProgramResult nested = compile_and_run(
        "int main() {\n"
        "    try {\n"
        "        try { throw 42; } catch (double value) { return 1; }\n"
        "    } catch (int value) { return value; }\n"
        "    return 0;\n"
        "}\n");
    INFO(nested.diagnostics);
    REQUIRE(nested.ok);
    CHECK(nested.result == 42);

    const ProgramResult rethrow = compile_and_run(
        "int main() {\n"
        "    try {\n"
        "        try { throw 42; } catch (int value) { throw; }\n"
        "    } catch (int value) { return value; }\n"
        "    return 0;\n"
        "}\n");
    INFO(rethrow.diagnostics);
    REQUIRE(rethrow.ok);
    CHECK(rethrow.result == 42);
}

TEST_CASE("exceptions emit ABI metadata and runtime entry points") {
    const cxx::Result result = cxx::compile(
        "int main() { try { throw 1; } catch (int value) { return value; } return 0; }\n");
    REQUIRE(result.ok);
    CHECK(result.assembly.find(".cpp_except") != std::string::npos);
    CHECK(result.assembly.find("__kr32_personality_v0") != std::string::npos);
    CHECK(result.assembly.find("__cxa_begin_catch") != std::string::npos);
    CHECK(result.assembly.find("__cxa_end_catch") != std::string::npos);
    CHECK(result.assembly.find("__cxa_atexit") != std::string::npos);
}

TEST_CASE("class exceptions are caught by reference and value") {
    const ProgramResult reference = compile_and_run(
        "class Error {\n"
        "public:\n"
        "    int code;\n"
        "    Error(int input) { code = input; }\n"
        "};\n"
        "int main() {\n"
        "    try { Error error(42); throw error; }\n"
        "    catch (const Error& caught) { return caught.code; }\n"
        "    return 0;\n"
        "}\n");
    INFO(reference.diagnostics);
    REQUIRE(reference.ok);
    CHECK(reference.result == 42);

    const ProgramResult value = compile_and_run(
        "class Error {\n"
        "public:\n"
        "    int code;\n"
        "    Error(int input) { code = input; }\n"
        "};\n"
        "int main() {\n"
        "    try { Error error(42); throw error; }\n"
        "    catch (Error caught) { return caught.code; }\n"
        "    return 0;\n"
        "}\n");
    INFO(value.diagnostics);
    REQUIRE(value.ok);
    CHECK(value.result == 42);

    const ProgramResult destruction = compile_and_run(
        "int destroyed = 0;\n"
        "class Error {\n"
        "public:\n"
        "    ~Error() { destroyed = 1; }\n"
        "};\n"
        "int main() {\n"
        "    int caught = 0;\n"
        "    try { Error error; throw error; }\n"
        "    catch (const Error& caught_error) { caught = 1; }\n"
        "    return destroyed == 1 && caught == 1 ? 42 : 0;\n"
        "}\n");
    INFO(destruction.diagnostics);
    REQUIRE(destruction.ok);
    CHECK(destruction.result == 42);
}

TEST_CASE("unwinder walks multiple frames and resumes through cleanup pads") {
    const ProgramResult program = compile_and_run(
        "int result = 0;\n"
        "class Guard {\n"
        "public:\n"
        "    int digit;\n"
        "    Guard(int value) { digit = value; }\n"
        "    ~Guard() { result = result * 10 + digit; }\n"
        "};\n"
        "void deep() { Guard guard(3); throw 1; }\n"
        "void middle() { Guard guard(2); deep(); }\n"
        "int main() {\n"
        "    try { Guard guard(1); middle(); }\n"
        "    catch (int value) { return result == 321 ? 42 : 0; }\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("exceptions propagate across function calls") {
    const ProgramResult program = compile_and_run(
        "int risky() { throw 42; }\n"
        "int main() {\n"
        "    try { return risky(); }\n"
        "    catch (int value) { return value; }\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);

    const ProgramResult cleanup = compile_and_run(
        "int result = 0;\n"
        "class Guard {\n"
        "public:\n"
        "    Guard() { }\n"
        "    ~Guard() { result = 42; }\n"
        "};\n"
        "int risky() { Guard guard; throw 1; }\n"
        "int main() {\n"
        "    try { risky(); } catch (int value) { return result; }\n"
        "    return 0;\n"
        "}\n");
    INFO(cleanup.diagnostics);
    REQUIRE(cleanup.ok);
    CHECK(cleanup.result == 42);
}

TEST_CASE("throw unwinds live destructors before catch") {
    const ProgramResult program = compile_and_run(
        "int result = 0;\n"
        "class Guard {\n"
        "public:\n"
        "    Guard() { }\n"
        "    ~Guard() { result = 42; }\n"
        "};\n"
        "int main() {\n"
        "    try { Guard guard; throw 1; }\n"
        "    catch (int value) { return result; }\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("new and delete allocate and construct objects") {
    const ProgramResult scalar = compile_and_run(
        "int main() { int* value = new int(42); int result = *value; delete value; "
        "return result; }\n");
    INFO(scalar.diagnostics);
    REQUIRE(scalar.ok);
    CHECK(scalar.result == 42);

    const ProgramResult object = compile_and_run(
        "class Value {\n"
        "public:\n"
        "    int value;\n"
        "    Value(int input) { value = input; }\n"
        "};\n"
        "int main() { Value* value = new Value(42); int result = value->value; "
        "delete value; return result; }\n");
    INFO(object.diagnostics);
    REQUIRE(object.ok);
    CHECK(object.result == 42);

    const ProgramResult array = compile_and_run(
        "int main() {\n"
        "    int* values = new int[3];\n"
        "    values[0] = 10; values[1] = 20; values[2] = 12;\n"
        "    int result = values[0] + values[1] + values[2];\n"
        "    delete[] values;\n"
        "    return result;\n"
        "}\n");
    INFO(array.diagnostics);
    REQUIRE(array.ok);
    CHECK(array.result == 42);

    const ProgramResult class_array = compile_and_run(
        "int destroyed = 0;\n"
        "class Item {\n"
        "public:\n"
        "    int value;\n"
        "    Item() { value = 7; }\n"
        "    ~Item() { destroyed = destroyed + 1; }\n"
        "};\n"
        "int main() {\n"
        "    Item* items = new Item[3];\n"
        "    int result = items[2].value;\n"
        "    delete[] items;\n"
        "    return destroyed == 3 ? result : 0;\n"
        "}\n");
    INFO(class_array.diagnostics);
    REQUIRE(class_array.ok);
    CHECK(class_array.result == 7);

    const ProgramResult throwing_array = compile_and_run(
        "int destroyed = 0;\n"
        "int constructed = 0;\n"
        "class Item {\n"
        "public:\n"
        "    Item() { if (constructed == 2) { throw 7; } constructed = constructed + 1; }\n"
        "    ~Item() { destroyed = destroyed + 1; }\n"
        "};\n"
        "int main() {\n"
        "    try { Item* items = new Item[3]; delete[] items; }\n"
        "    catch (int value) { return destroyed == 2 ? 42 : 0; }\n"
        "    return 0;\n"
        "}\n");
    INFO(throwing_array.diagnostics);
    REQUIRE(throwing_array.ok);
    CHECK(throwing_array.result == 42);
}

TEST_CASE("delete runs the destructor") {
    const ProgramResult program = compile_and_run(
        "int result = 0;\n"
        "class Guard {\n"
        "public:\n"
        "    Guard() { }\n"
        "    ~Guard() { result = 42; }\n"
        "};\n"
        "int main() { Guard* guard = new Guard(); delete guard; return result; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ debug info records classes, members, this, and lines") {
    const cxx::Result compiled = cxx::compile(
        "class Point {\n"
        "public:\n"
        "    int x;\n"
        "    int y;\n"
        "    int sum() { return x + y; }\n"
        "};\n"
        "int main() { Point point; point.x = 20; point.y = 22; return point.sum(); }\n");
    REQUIRE(compiled.ok);
    container::Image user;
    assembler::Error assembly_error;
    INFO(assembly_error.message);
    REQUIRE_MESSAGE(assembler::assemble_object(compiled.assembly, user, assembly_error),
                    assembly_error.message.c_str());
    container::Image runtime;
    REQUIRE(assembler::assemble_object(cc::runtime_assembly(), runtime,
                                       assembly_error));
    container::Image image;
    linker::Map map;
    linker::Error link_error;
    REQUIRE(linker::link({user, runtime}, linker::Options{}, image, map,
                         link_error));
    debug::Info info;
    std::string error;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error.c_str());
    REQUIRE_FALSE(info.classes.empty());
    const debug::ClassInfo* point = nullptr;
    for (const debug::ClassInfo& candidate : info.classes) {
        if (candidate.name.find("Point") != std::string::npos) {
            point = &candidate;
        }
    }
    REQUIRE(point != nullptr);
    CHECK(point->size == 8);
    REQUIRE(point->members.size() == 2);
    CHECK(point->members[0].name == "x");
    CHECK(point->members[0].offset == 0);
    CHECK(point->members[1].name == "y");
    CHECK(point->members[1].offset == 4);
    bool found_this = false;
    for (const debug::Variable& variable : info.variables) {
        if (variable.name == "this") {
            found_this = true;
            CHECK(variable.type.find("Point") != std::string::npos);
        }
    }
    CHECK(found_this);
    CHECK_FALSE(info.lines.empty());
    CHECK_FALSE(info.functions.empty());
}

TEST_CASE("function-local statics initialize once behind a guard") {
    const ProgramResult program = compile_and_run(
        "int next() {\n"
        "    static int count = 0;\n"
        "    count = count + 1;\n"
        "    return count;\n"
        "}\n"
        "int main() {\n"
        "    int first = next();\n"
        "    int second = next();\n"
        "    return first == 1 && second == 2 ? 42 : 0;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("thread_local class objects construct and destruct") {
    const ProgramResult program = compile_and_run(
        "#include <stdio.h>\n"
        "struct Guard {\n"
        "    int value;\n"
        "    Guard() { value = 42; }\n"
        "    ~Guard() { putchar('T'); }\n"
        "};\n"
        "thread_local Guard guard;\n"
        "int main() { putchar('B'); return guard.value == 42 ? 0 : 1; }\n");
    CHECK(program.ok);
    CHECK(program.output == "BT");
    CHECK(program.result == 0);
}

TEST_CASE("thread/atomic/mutex header surface compiles and runs") {
    const ProgramResult program = compile_and_run(
        "#include <kr32/thread.hpp>\n"
        "int main() {\n"
        "    kr32::atomic_int value;\n"
        "    value.store(1);\n"
        "    value.fetch_add(41);\n"
        "    kr32::mutex guard;\n"
        "    guard.lock();\n"
        "    guard.unlock();\n"
        "    return value.load();\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("atomic builtins lower to LL/SC") {
    const ProgramResult program = compile_and_run(
        "int counter = 0;\n"
        "int main() {\n"
        "    __atomic_store_n(&counter, 1, 0);\n"
        "    __atomic_fetch_add(&counter, 41, 0);\n"
        "    return __atomic_load_n(&counter, 0);\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);

    const ProgramResult compare_exchange = compile_and_run(
        "int value = 0;\n"
        "int main() {\n"
        "    int expected = 0;\n"
        "    if (!__atomic_compare_exchange_n(&value, &expected, 42, 0, 0, 0)) {\n"
        "        return 1;\n"
        "    }\n"
        "    return value;\n"
        "}\n");
    INFO(compare_exchange.diagnostics);
    REQUIRE(compare_exchange.ok);
    CHECK(compare_exchange.result == 42);
}

TEST_CASE("thread_local globals use R11 TP offsets") {
    const ProgramResult program = compile_and_run(
        "thread_local int value;\n"
        "int main() { value = 42; return value; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);

    const ProgramResult initialized = compile_and_run(
        "thread_local int value = 42;\n"
        "int main() { return value; }\n");
    INFO(initialized.diagnostics);
    REQUIRE(initialized.ok);
    CHECK(initialized.result == 42);
}

TEST_CASE("function templates instantiate explicitly and by deduction") {
    const ProgramResult explicit_call = compile_and_run(
        "template <typename T> T maximum(T a, T b) { return a > b ? a : b; }\n"
        "int main() { return maximum<int>(20, 42); }\n");
    INFO(explicit_call.diagnostics);
    REQUIRE(explicit_call.ok);
    CHECK(explicit_call.result == 42);

    const ProgramResult deduced = compile_and_run(
        "template <typename T> T maximum(T a, T b) { return a > b ? a : b; }\n"
        "int main() { return (int)maximum(20.5, 42.5); }\n");
    INFO(deduced.diagnostics);
    REQUIRE(deduced.ok);
    CHECK(deduced.result == 42);
}

TEST_CASE("template instantiations are weak across translation units") {
    const ProgramResult program = compile_and_run_pair(
        "template <typename T> T maximum(T a, T b) { return a > b ? a : b; }\n"
        "int helper(int value);\n"
        "int main() { return helper(40); }\n",
        "template <typename T> T maximum(T a, T b) { return a > b ? a : b; }\n"
        "int helper(int value) { return maximum<int>(value, 42); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("inline functions use weak symbols across translation units") {
    const ProgramResult program = compile_and_run_pair(
        "inline int twice(int value) { return value * 2; }\n"
        "int other();\n"
        "int main() { return twice(21); }\n",
        "inline int twice(int value) { return value * 2; }\n"
        "int other() { return twice(1); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("static initialization runs before main in declaration order") {
    const ProgramResult program = compile_and_run(
        "int compute() { return 40; }\n"
        "int first = compute();\n"
        "int second = first + 2;\n"
        "int main() { return second; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("static initializers from another translation unit run before main") {
    const ProgramResult program = compile_and_run_pair(
        "int value();\n"
        "int main() { return value(); }\n",
        "int state = 0;\n"
        "class Init { public: Init() { state = 42; } };\n"
        "Init init;\n"
        "int value() { return state; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("thread_local objects from separate TUs get distinct layouts") {
    const ProgramResult program = compile_and_run_pair(
        "int get_b();\n"
        "thread_local int a_value = 7;\n"
        "int main() { return a_value + get_b(); }\n",
        "thread_local int b_value = 42;\n"
        "int get_b() { return b_value; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 49);
}

TEST_CASE("exceptions cross translation-unit frame tables") {
    const ProgramResult program = compile_and_run_pair(
        "int thrower();\n"
        "int main() {\n"
        "    try { return thrower(); }\n"
        "    catch (int value) { return value; }\n"
        "    return 0;\n"
        "}\n",
        "int thrower() { throw 42; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("class static locals construct at the static object's address") {
    const ProgramResult program = compile_and_run(
        "class Counter {\n"
        "public:\n"
        "    int value;\n"
        "    Counter() { value = 42; }\n"
        "    ~Counter() { value = 0; }\n"
        "};\n"
        "int get() { static Counter counter; return counter.value; }\n"
        "int main() { return get(); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("three translation units share startup and symbol tables") {
    const ProgramResult program = compile_and_run_many(
        {"int value();\n"
         "int main() { return value(); }\n",
         "int state = 0;\n"
         "class Init { public: Init() { state = 42; } };\n"
         "Init init;\n",
         "extern int state;\n"
         "int value() { return state; }\n"});
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("static destructors from another translation unit run at exit") {
    const ProgramResult program = compile_and_run_pair(
        "int main() { return 0; }\n",
        "#include <kr32/runtime.hpp>\n"
        "class Final { public: ~Final() { exit(42); } };\n"
        "Final final_object;\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("static objects are destroyed after main in reverse order") {
    const ProgramResult program = compile_and_run(
        "#include <kr32/runtime.hpp>\n"
        "int order = 0;\n"
        "class Second {\n"
        "public:\n"
        "    ~Second() { order = 2; }\n"
        "};\n"
        "class First {\n"
        "public:\n"
        "    ~First() { if (order == 2) { exit(42); } }\n"
        "};\n"
        "First first;\n"
        "Second second;\n"
        "int main() { return 0; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("class copy construction and assignment are memberwise byte copies") {
    const ProgramResult program = compile_and_run(
        "class Pair {\n"
        "public:\n"
        "    int a;\n"
        "    int b;\n"
        "};\n"
        "int main() {\n"
        "    Pair first; first.a = 20; first.b = 22;\n"
        "    Pair second = first;\n"
        "    Pair third; third = first;\n"
        "    return second.a + second.b + third.a + third.b - 42;\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("multidimensional arrays preserve dimension order") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int a[2][3][4];\n"
        "    a[0][0][0] = 1;\n"
        "    a[1][2][3] = 42;\n"
        "    return a[0][0][0] + a[1][2][3];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 43);
}

TEST_CASE("pointer increments and compound assignments scale by element size") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int values[3];\n"
        "    int* p = values;\n"
        "    p++;\n"
        "    *p = 11;\n"
        "    p += 1;\n"
        "    *p = 22;\n"
        "    return values[1] + values[2];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 33);
}

TEST_CASE("class compound assignment reports an unsupported operation") {
    const cxx::Result result = cxx::compile(
        "struct Pair { int x; };\n"
        "int main() { Pair a; Pair b; a += b; return 0; }\n");
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("compound assignment") !=
          std::string::npos);
}

TEST_CASE("std thread, mutex, and condition_variable surface compiles") {
    const ProgramResult program = compile_and_run(
        "#include <thread>\n"
        "#include <mutex>\n"
        "#include <condition_variable>\n"
        "int main() {\n"
        "    std::thread thread;\n"
        "    std::mutex guard;\n"
        "    std::condition_variable condition;\n"
        "    std::this_thread::yield();\n"
        "    if (thread.joinable()) return 1;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("exit runs static destructors once") {
    const ProgramResult program = compile_and_run(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "struct Guard { ~Guard() { printf(\"F\"); } };\n"
        "Guard guard;\n"
        "int main() { printf(\"B\"); exit(7); return 0; }\n");
    CHECK(program.ok);
    CHECK(program.output == "BF");
    CHECK(program.result == 7);
}

TEST_CASE("C++ unsigned arithmetic uses unsigned comparisons and shifts") {
    const ProgramResult arithmetic = compile_and_run(
        "int signed_div(int x, int y) { return x / y; }\n"
        "int main() {\n"
        "    unsigned int a = 1;\n"
        "    unsigned int b = 2;\n"
        "    unsigned int wrapped = a - b;\n"
        "    int signed_result = -7 / 2;\n"
        "    int shifted = (int)((unsigned)-1 >> 31);\n"
        "    return (wrapped > 0) + (signed_result == -3) + shifted +\n"
        "           signed_div(-7, 2) + (1u < 2u) + sizeof(unsigned int);\n"
        "}\n");
    CHECK(arithmetic.ok);
    CHECK(arithmetic.result == 5);

    const ProgramResult overload = compile_and_run(
        "int pick(int) { return 1; }\n"
        "int pick(unsigned int) { return 2; }\n"
        "int main() { return pick(1u); }\n");
    CHECK(overload.ok);
    CHECK(overload.result == 2);
}

TEST_CASE("C++ by-value class parameters copy and sret returns objects") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; };\n"
        "int sum(P p) { p.x = 100; return p.x + p.y; }\n"
        "P add(P a, P b) { P p; p.x = a.x + b.x; p.y = a.y + b.y; return p; }\n"
        "int main() {\n"
        "    P p; p.x = 1; p.y = 2;\n"
        "    P q; q.x = 4; q.y = 5;\n"
        "    P n = add(p, q);\n"
        "    return sum(p) + p.x + n.x * 10 + n.y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 160);
}

TEST_CASE("class template instantiations are weak across translation units") {
    const std::string header =
        "template <typename T> class Box {\n"
        "public:\n"
        "    T value;\n"
        "    T get() { return value; }\n"
        "};\n";
    const cxx::Result first = cxx::compile(
        header +
        "int use_box() { Box<int> b; b.value = 42; return b.get(); }\n",
        cxx::Options{.source_name = "template_a.cpp"});
    const cxx::Result second = cxx::compile(
        header +
        "int use_box();\n"
        "int main() { Box<int> b; b.value = 1; return use_box() + b.get(); }\n",
        cxx::Options{.source_name = "template_b.cpp"});
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    const ProgramResult program = run_assemblies({first.assembly, second.assembly});
    CHECK(program.ok);
    CHECK(program.result == 43);
}

TEST_CASE("class templates instantiate fields, methods, and constructors") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box {\n"
        "public:\n"
        "    T value;\n"
        "    Box(T initial) { value = initial; }\n"
        "    T get() { return value; }\n"
        "};\n"
        "int main() {\n"
        "    Box<int> number(42);\n"
        "    Box<char> letter('A');\n"
        "    return number.get() + letter.get();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 107);
}

TEST_CASE("C++ friends and private static members enforce access") {
    const ProgramResult friends = compile_and_run(
        "class Box {\n"
        "private:\n"
        "    int value;\n"
        "public:\n"
        "    Box() { value = 42; }\n"
        "    friend int peek(Box& box);\n"
        "    friend class Inspector;\n"
        "};\n"
        "int peek(Box& box) { return box.value; }\n"
        "class Inspector { public: int get(Box& box) { return box.value; } };\n"
        "int main() { Box box; Inspector inspector; return peek(box) + inspector.get(box); }\n");
    CHECK(friends.ok);
    CHECK(friends.result == 84);

    const ProgramResult statics = compile_and_run(
        "class Counter {\n"
        "private:\n"
        "    static int secret;\n"
        "public:\n"
        "    static int get() { return secret; }\n"
        "};\n"
        "int Counter::secret = 42;\n"
        "int main() { return Counter::get(); }\n");
    CHECK(statics.ok);
    CHECK(statics.result == 42);

    const cxx::Result rejected = cxx::compile(
        "class Counter {\n"
        "private:\n"
        "    static int secret;\n"
        "public:\n"
        "    static int get() { return secret; }\n"
        "};\n"
        "int Counter::secret = 42;\n"
        "int main() { return Counter::secret; }\n");
    CHECK_FALSE(rejected.ok);
}

TEST_CASE("C++ access control rejects private members and allows class internals") {
    const ProgramResult internal = compile_and_run(
        "class P {\n"
        "private:\n"
        "    int secret;\n"
        "public:\n"
        "    int x;\n"
        "    P() { secret = 7; x = 1; }\n"
        "    int get() { return secret; }\n"
        "};\n"
        "int main() { P p; p.x = 2; return p.get() + p.x; }\n");
    CHECK(internal.ok);
    CHECK(internal.result == 9);

    const cxx::Result outside = cxx::compile(
        "class P { private: int secret; public: P() { secret = 1; } };\n"
        "int main() { P p; p.secret = 2; return 0; }\n");
    CHECK_FALSE(outside.ok);
    REQUIRE_FALSE(outside.diagnostics.empty());

    const ProgramResult protected_access = compile_and_run(
        "class B { protected: int x; public: B() { x = 1; } };\n"
        "class D : public B { public: int get() { return x; } };\n"
        "int main() { D d; return d.get(); }\n");
    CHECK(protected_access.ok);
    CHECK(protected_access.result == 1);
}

TEST_CASE("C++ supports designated aggregate and array initializers") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; };\n"
        "P global = {.y = 5};\n"
        "int main() {\n"
        "    P p = {.y = 2};\n"
        "    int a[4] = {[2] = 7, 8};\n"
        "    return p.x + p.y + a[0] + a[2] + a[3] + global.y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 22);
}

TEST_CASE("C++ global aggregate initializers are constant data") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; };\n"
        "int a[2][2] = {{1, 2}, {3, 4}};\n"
        "P p = {11, 22};\n"
        "int b[3] = {5};\n"
        "int main() {\n"
        "    return a[0][0] + a[0][1] + a[1][0] + a[1][1] + b[0] + b[2] + p.x + p.y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 48);
}

TEST_CASE("C++ aggregate and nested array initializers run") {
    const ProgramResult program = compile_and_run(
        "struct P { int x; int y; };\n"
        "int main() {\n"
        "    int a[2][2] = {{1, 2}, {3, 4}};\n"
        "    int b[3] = {5};\n"
        "    P p = {11, 22};\n"
        "    return a[0][0] + a[0][1] + a[1][0] + a[1][1] + b[0] + b[2] + p.x + p.y;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 48);
}

TEST_CASE("goto labels and range-for arrays run") {
    const ProgramResult jump = compile_and_run(
        "int main() {\n"
        "    int i = 0;\n"
        "    goto check;\n"
        "body:\n"
        "    i += 1;\n"
        "check:\n"
        "    if (i < 4) goto body;\n"
        "    return i;\n"
        "}\n");
    CHECK(jump.ok);
    CHECK(jump.result == 4);

    const ProgramResult range = compile_and_run(
        "int main() {\n"
        "    int values[4];\n"
        "    values[0] = 1; values[1] = 2; values[2] = 3; values[3] = 4;\n"
        "    int total = 0;\n"
        "    for (int value : values) { total += value; }\n"
        "    return total;\n"
        "}\n");
    CHECK(range.ok);
    CHECK(range.result == 10);
}

TEST_CASE("conditional expressions and switch fallthrough run") {
    const ProgramResult ternary = compile_and_run(
        "int main() {\n"
        "    int x = 5;\n"
        "    int i = 2;\n"
        "    int result = (x > i) ? 0 : 1;\n"
        "    return result + (i > x ? 10 : 20);\n"
        "}\n");
    CHECK(ternary.ok);
    CHECK(ternary.result == 20);

    const ProgramResult fallthrough = compile_and_run(
        "int main() {\n"
        "    int x = 1;\n"
        "    int total = 0;\n"
        "    switch (x) {\n"
        "    case 1: total += 1;\n"
        "    case 2: total += 2; break;\n"
        "    case 3: total += 4; break;\n"
        "    default: total += 8;\n"
        "    }\n"
        "    return total;\n"
        "}\n");
    CHECK(fallthrough.ok);
    CHECK(fallthrough.result == 3);
}

TEST_CASE("classes lay out members and call member functions") {
    const ProgramResult program = compile_and_run(
        "class Point {\n"
        "public:\n"
        "    int x;\n"
        "    int y;\n"
        "    int sum() { return x + y; }\n"
        "};\n"
        "int main() { Point point; point.x = 20; point.y = 22; return point.sum(); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("constructors, this, and member initializer lists run") {
    const ProgramResult program = compile_and_run(
        "class Point {\n"
        "public:\n"
        "    int x;\n"
        "    int y;\n"
        "    Point(int value) : x(value) { y = 1; }\n"
        "    int sum() { return this->x + this->y; }\n"
        "};\n"
        "int main() { Point point(41); return point.sum(); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("destructors run at scope exit") {
    const ProgramResult program = compile_and_run(
        "int result = 0;\n"
        "class Guard {\n"
        "public:\n"
        "    ~Guard() { result = 42; }\n"
        "};\n"
        "int main() { { Guard guard; } return result; }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("single inheritance lays out the base at offset zero") {
    const ProgramResult program = compile_and_run(
        "class Base {\n"
        "public:\n"
        "    int a;\n"
        "    Base() { a = 40; }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    int b;\n"
        "    Derived() { b = 2; }\n"
        "    int sum() { return a + b; }\n"
        "};\n"
        "int main() { Derived value; return value.sum(); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("member access through pointers and FP members run") {
    const ProgramResult pointer = compile_and_run(
        "class Point {\n"
        "public:\n"
        "    int x;\n"
        "    int sum() { return x; }\n"
        "};\n"
        "int main() { Point point; point.x = 42; Point* pointer = &point; "
        "return pointer->sum(); }\n");
    INFO(pointer.diagnostics);
    REQUIRE(pointer.ok);
    CHECK(pointer.result == 42);

    const ProgramResult fp = compile_and_run(
        "class Value {\n"
        "public:\n"
        "    double x;\n"
        "    Value(double value) : x(value) { }\n"
        "    double get() { return x; }\n"
        "};\n"
        "int main() { Value value(21.0); return (int)(value.get() * 2.0); }\n");
    INFO(fp.diagnostics);
    REQUIRE(fp.ok);
    CHECK(fp.result == 42);
}

TEST_CASE("static data members and static member functions run") {
    const ProgramResult program = compile_and_run(
        "class Counter {\n"
        "public:\n"
        "    static int value;\n"
        "    static int get() { return value; }\n"
        "};\n"
        "int Counter::value = 42;\n"
        "int main() { return Counter::get(); }\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
}
