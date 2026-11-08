#include "cxx_test_support.h"

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

TEST_CASE("matrix debug info keeps array bounds and float members") {
    container::Image image;
    std::string error;
    REQUIRE_MESSAGE(build_cluster_image(
                        "#include <kr32/matrix.hpp>\n"
                        "int main() {\n"
                        "    kr32::mat4 m;\n"
                        "    kr32::vec4 v;\n"
                        "    v.x = 1.0;\n"
                        "    return 0;\n"
                        "}\n",
                        image, error),
                    error);
    debug::Info info;
    REQUIRE_MESSAGE(debug::parse_info(image, info, error), error);

    const debug::ClassInfo* matrix = nullptr;
    const debug::ClassInfo* vector = nullptr;
    for (const debug::ClassInfo& klass : info.classes) {
        if (klass.name == "kr32::mat4") matrix = &klass;
        if (klass.name == "kr32::vec4") vector = &klass;
    }
    REQUIRE(matrix != nullptr);
    REQUIRE(vector != nullptr);
    REQUIRE(matrix->members.size() == 1);
    CHECK(matrix->members[0].name == "m");
    CHECK(matrix->members[0].type == "float[16]");
    REQUIRE(vector->members.size() == 4);
    CHECK(vector->members[0].type == "float");

    const debug::Function* main = nullptr;
    for (const debug::Function& function : info.functions) {
        if (function.name == "main") main = &function;
    }
    REQUIRE(main != nullptr);
    bool found_return_line = false;
    for (const debug::Line& line : info.lines) {
        if (line.source == 0 && line.line == 6 &&
            line.address >= main->start && line.address < main->end) {
            found_return_line = true;
        }
    }
    CHECK(found_return_line);
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

TEST_CASE("new and delete handle null, placement, nothrow, and failure") {
    const ProgramResult null_delete = compile_and_run(
        "int destroyed = 0;\n"
        "class Guard { public: ~Guard() { destroyed = 1; } };\n"
        "int main() {\n"
        "    Guard* pointer = 0;\n"
        "    delete pointer;\n"
        "    Guard* array = 0;\n"
        "    delete[] array;\n"
        "    return destroyed;\n"
        "}\n");
    INFO(null_delete.diagnostics);
    REQUIRE(null_delete.ok);
    CHECK(null_delete.result == 0);

    const ProgramResult placement = compile_and_run(
        "class Value { public: int value; Value(int input) { value = input; } };\n"
        "int main() {\n"
        "    unsigned int storage[4];\n"
        "    Value* value = new (storage) Value(7);\n"
        "    if (value != (Value*)storage || value->value != 7) return 1;\n"
        "    int* scalar_storage = (int*)storage;\n"
        "    int* scalar = new (scalar_storage) int(42);\n"
        "    return *scalar == 42 ? 42 : 2;\n"
        "}\n");
    INFO(placement.diagnostics);
    REQUIRE(placement.ok);
    CHECK(placement.result == 42);

    const ProgramResult nothrow = compile_and_run(
        "int main() {\n"
        "    int* value = new (std::nothrow) int[0x7FFFFFFF];\n"
        "    return value == 0 ? 42 : 0;\n"
        "}\n");
    INFO(nothrow.diagnostics);
    REQUIRE(nothrow.ok);
    CHECK(nothrow.result == 42);

    const ProgramResult failure = compile_and_run(
        "int main() {\n"
        "    try {\n"
        "        int* value = new int[0x7FFFFFFF];\n"
        "        delete[] value;\n"
        "    } catch (std::bad_alloc& error) {\n"
        "        return 42;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    INFO(failure.diagnostics);
    REQUIRE(failure.ok);
    CHECK(failure.result == 42);

    const ProgramResult constructor_failure = compile_and_run(
        "class Throwing {\n"
        "public:\n"
        "    Throwing() { throw 7; }\n"
        "};\n"
        "int main() {\n"
        "    try { Throwing* value = new Throwing; }\n"
        "    catch (int error) { return 42; }\n"
        "    return 0;\n"
        "}\n");
    INFO(constructor_failure.diagnostics);
    REQUIRE(constructor_failure.ok);
    CHECK(constructor_failure.result == 42);
}

TEST_CASE("new arrays initialize and delete dispatches virtual destructors") {
    const ProgramResult arrays = compile_and_run(
        "int main() {\n"
        "    int* values = new int[4]{1, 2, 3};\n"
        "    if (values[0] != 1 || values[2] != 3 || values[3] != 0) return 1;\n"
        "    delete[] values;\n"
        "    int* zeroed = new int[2]();\n"
        "    if (zeroed[0] != 0 || zeroed[1] != 0) return 2;\n"
        "    delete[] zeroed;\n"
        "    float* floats = new float[2]{1.5f, 2.5f};\n"
        "    if (floats[0] != 1.5f || floats[1] != 2.5f) return 3;\n"
        "    delete[] floats;\n"
        "    double* aligned = new double(2.5);\n"
        "    if (((unsigned int)aligned & 7U) != 0 || *aligned != 2.5) return 4;\n"
        "    delete aligned;\n"
        "    return 42;\n"
        "}\n");
    INFO(arrays.diagnostics);
    REQUIRE(arrays.ok);
    CHECK(arrays.result == 42);

    const ProgramResult virtual_delete = compile_and_run(
        "int base_destroyed = 0;\n"
        "int derived_destroyed = 0;\n"
        "class Base {\n"
        "public:\n"
        "    virtual ~Base() { base_destroyed = 1; }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    ~Derived() { derived_destroyed = 1; }\n"
        "};\n"
        "int main() {\n"
        "    Base* pointer = new Derived;\n"
        "    delete pointer;\n"
        "    return base_destroyed == 1 && derived_destroyed == 1 ? 42 : 0;\n"
        "}\n");
    INFO(virtual_delete.diagnostics);
    REQUIRE(virtual_delete.ok);
    CHECK(virtual_delete.result == 42);

    const ProgramResult aligned_matrix = compile_and_run(
        "#include <kr32/matrix.hpp>\n"
        "int main() {\n"
        "    kr32::mat4* matrix = new kr32::mat4;\n"
        "    if (((unsigned int)matrix & 15U) != 0) return 1;\n"
        "    matrix->m[0] = 1.0;\n"
        "    float value = matrix->m[0];\n"
        "    delete matrix;\n"
        "    return value == 1.0 ? 42 : 2;\n"
        "}\n");
    INFO(aligned_matrix.diagnostics);
    REQUIRE(aligned_matrix.ok);
    CHECK(aligned_matrix.result == 42);

    const ProgramResult virtual_array = compile_and_run(
        "int destroyed = 0;\n"
        "class Base {\n"
        "public:\n"
        "    int value;\n"
        "    virtual ~Base() { destroyed = destroyed + 1; }\n"
        "};\n"
        "int main() {\n"
        "    Base* values = new Base[2];\n"
        "    values[0].value = 1;\n"
        "    values[1].value = 2;\n"
        "    int result = values[0].value + values[1].value;\n"
        "    delete[] values;\n"
        "    return result == 3 && destroyed == 2 ? 42 : 0;\n"
        "}\n");
    INFO(virtual_array.diagnostics);
    REQUIRE(virtual_array.ok);
    CHECK(virtual_array.result == 42);
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

TEST_CASE("floating point converts to long long") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    double positive = 10000000000.0;\n"
        "    double negative = -10000000000.0;\n"
        "    long long p = (long long)positive;\n"
        "    long long n = (long long)negative;\n"
        "    return (int)(p / 1000000000) == 10 &&\n"
        "           (int)(n / 1000000000) == -10 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ long long converts to floating point") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long long positive = 10000000000;\n"
        "    long long negative = -10000000000;\n"
        "    double p = (double)positive;\n"
        "    double n = (double)negative;\n"
        "    return (int)(p / 1000000000.0) == 10 &&\n"
        "           (int)(n / 1000000000.0) == -10 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ long long casts extend and wide shifts work") {
    const ProgramResult cast_program = compile_and_run(
        "int main() {\n"
        "    unsigned int value = 4294967295;\n"
        "    long long wide = (long long)value;\n"
        "    int negative = -1;\n"
        "    unsigned long long sign_extended = (unsigned long long)negative;\n"
        "    return (int)(wide >> 32) == 0 &&\n"
        "           (int)(sign_extended >> 32) == -1 ? 42 : 0;\n"
        "}\n");
    CHECK(cast_program.ok);
    CHECK(cast_program.result == 42);

    const ProgramResult shift_program = compile_and_run(
        "int main() {\n"
        "    long long value = 1;\n"
        "    long long high = value << 40;\n"
        "    return (high >> 40) == 1 ? 42 : 0;\n"
        "}\n");
    CHECK(shift_program.ok);
    CHECK(shift_program.result == 42);
}

TEST_CASE("C++ long long compound assignments preserve both halves") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long long value = 100000;\n"
        "    value += 200000;\n"
        "    value *= 2;\n"
        "    return (int)(value / 1000);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 600);
}

TEST_CASE("long long literal suffixes select 64-bit arithmetic") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long long value = 1LL << 40;\n"
        "    return (int)((value >> 40) + (1000000000LL / 1000000000));\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 2);
}

TEST_CASE("thread_local dynamic initializers run at startup") {
    const ProgramResult program = compile_and_run(
        "int seed() { return 42; }\n"
        "thread_local int value = seed();\n"
        "thread_local long long wide = seed() * 1000000000LL;\n"
        "int main() { return value + (int)(wide / 1000000000) - 42; }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("thread_local long long keeps both halves") {
    const ProgramResult program = compile_and_run(
        "thread_local long long value = 10000000000;\n"
        "int main() { return (int)(value / 1000000000); }\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
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

