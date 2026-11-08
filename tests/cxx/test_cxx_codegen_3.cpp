#include "cxx_test_support.h"

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

TEST_CASE("fixed stack-passed integer arguments run") {
    const ProgramResult program = compile_and_run(
        "int sum5(int a, int b, int c, int d, int e) {\n"
        "    return a + b + c + d + e;\n"
        "}\n"
        "int sum6(int a, int b, int c, int d, int e, int f) {\n"
        "    return a + b + c + d + e + f;\n"
        "}\n"
        "int main() {\n"
        "    if (sum5(1, 2, 3, 4, 5) != 15) return 1;\n"
        "    if (sum6(1, 2, 3, 4, 5, 6) != 21) return 2;\n"
        "    int a = 1; int b = 2; int c = 3; int d = 4; int e = 5;\n"
        "    return sum5(a, b, c, d, e) - 15;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
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

TEST_CASE("derived classes override pure virtuals without the override keyword") {
    const ProgramResult program = compile_and_run(
        "class Base {\n"
        "public:\n"
        "    Base() { }\n"
        "    virtual int value() = 0;\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    int value() { return 42; }\n"
        "};\n"
        "int main() {\n"
        "    Derived object;\n"
        "    Base* pointer = &object;\n"
        "    return pointer->value();\n"
        "}\n");
    INFO(program.diagnostics);
    REQUIRE(program.ok);
    CHECK(program.result == 42);
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

