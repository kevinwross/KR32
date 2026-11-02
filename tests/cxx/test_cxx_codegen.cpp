#include "doctest.h"

#include "cxx/codegen.h"
#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/cxx/cxx.h"
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
    if (!linker::link(objects, linker::Options{}, image, map, link_error)) {
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
