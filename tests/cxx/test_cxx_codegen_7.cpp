#include "cxx_test_support.h"

TEST_CASE("non-type template array bounds accept constant offsets") {
    const ProgramResult program = compile_and_run(
        "template <int N> struct Array {\n"
        "    int values[N + 1];\n"
        "    int size() { return N + 1; }\n"
        "};\n"
        "int main() {\n"
        "    Array<2> array;\n"
        "    array.values[2] = 42;\n"
        "    return array.size() + array.values[2] - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("non-type template arguments accept constant expressions") {
    const ProgramResult program = compile_and_run(
        "template <int N> class Box {\n"
        "public:\n"
        "    int size() { return N; }\n"
        "};\n"
        "int main() {\n"
        "    Box<2 + 3> five;\n"
        "    Box<1 << 3> eight;\n"
        "    return five.size() * 10 + eight.size();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 58);
}

TEST_CASE("non-type template parameters size arrays") {
    const ProgramResult program = compile_and_run(
        "template <int N> class Box {\n"
        "public:\n"
        "    int data[N];\n"
        "    int size() { return N; }\n"
        "};\n"
        "int main() {\n"
        "    Box<4> box;\n"
        "    box.data[3] = 42;\n"
        "    return box.data[3] + box.size() - 4;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("function template explicit specializations override generic calls") {
    const ProgramResult specialized = compile_and_run(
        "template <typename T> T twice(T value) { return value + value; }\n"
        "template <> int twice<int>(int value) { return 42; }\n"
        "int main() {\n"
        "    int first = twice(1);\n"
        "    int second = twice(1);\n"
        "    return first + second;\n"
        "}\n");
    CHECK(specialized.ok);
    CHECK(specialized.result == 84);

    const ProgramResult generic = compile_and_run(
        "template <typename T> T twice(T value) { return value + value; }\n"
        "int main() { return (int)twice(1.5); }\n");
    CHECK(generic.ok);
    CHECK(generic.result == 3);

    const ProgramResult mixed = compile_and_run(
        "template <typename T> T twice(T value) { return value + value; }\n"
        "template <> int twice<int>(int value) { return 42; }\n"
        "int main() {\n"
        "    int specialized = twice(1);\n"
        "    int generic = (int)twice(1.5);\n"
        "    return specialized * 10 + generic;\n"
        "}\n");
    CHECK(mixed.ok);
    CHECK(mixed.result == 423);
}

TEST_CASE("non-type template parameters bind integer arguments") {
    const ProgramResult program = compile_and_run(
        "template <int N> class Box {\n"
        "public:\n"
        "    int value;\n"
        "    int size() { return N; }\n"
        "};\n"
        "int main() {\n"
        "    Box<7> seven;\n"
        "    Box<9> nine;\n"
        "    return seven.size() * 10 + nine.size();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 79);
}

TEST_CASE("class template static data members use their definition initializer") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box {\n"
        "public:\n"
        "    static T value;\n"
        "};\n"
        "template <typename T> T Box<T>::value = 42;\n"
        "int main() {\n"
        "    Box<int> box;\n"
        "    return Box<int>::value == box.value ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("out-of-class template member definitions attach to instantiations") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box {\n"
        "public:\n"
        "    T value;\n"
        "    T add(T amount);\n"
        "};\n"
        "template <typename T> T Box<T>::add(T amount) {\n"
        "    return value + amount;\n"
        "}\n"
        "int main() {\n"
        "    Box<int> box;\n"
        "    box.value = 40;\n"
        "    return box.add(2);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("specialized out-of-class template members define specialization methods") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box {\n"
        "public:\n"
        "    T value;\n"
        "    T get();\n"
        "};\n"
        "template <> class Box<int> {\n"
        "public:\n"
        "    int value;\n"
        "    int get();\n"
        "};\n"
        "template <> int Box<int>::get() { return 42; }\n"
        "int main() {\n"
        "    Box<int> box;\n"
        "    box.value = 1;\n"
        "    return box.get();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("partial specializations match array patterns") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box { public: int kind() { return 1; } };\n"
        "template <typename T> class Box<T[3]> { public: int kind() { return 5; } };\n"
        "int main() {\n"
        "    Box<int[3]> array_box;\n"
        "    return array_box.kind();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 5);
}

TEST_CASE("partial specializations match reference and multi-level pointer patterns") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box { public: int kind() { return 1; } };\n"
        "template <typename T> class Box<T&> { public: int kind() { return 3; } };\n"
        "template <typename T> class Box<T**> { public: int kind() { return 4; } };\n"
        "int main() {\n"
        "    Box<int&> reference_box;\n"
        "    Box<int**> pointer_box;\n"
        "    return reference_box.kind() * 10 + pointer_box.kind();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 34);
}

TEST_CASE("partial class template specializations match pointer patterns") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box {\n"
        "public:\n"
        "    T value;\n"
        "    int kind() { return 1; }\n"
        "};\n"
        "template <typename T> class Box<T*> {\n"
        "public:\n"
        "    T* value;\n"
        "    int kind() { return 2; }\n"
        "};\n"
        "int main() {\n"
        "    Box<int*> pointer_box;\n"
        "    Box<int> value_box;\n"
        "    return pointer_box.kind() * 10 + value_box.kind();\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 21);
}

TEST_CASE("C++ long long arrays and references keep both halves") {
    const ProgramResult array_program = compile_and_run(
        "int main() {\n"
        "    long long values[2];\n"
        "    values[0] = 10000000000;\n"
        "    values[1] = 2;\n"
        "    return (int)(values[0] / 1000000000) + (int)values[1];\n"
        "}\n");
    CHECK(array_program.ok);
    CHECK(array_program.result == 12);

    const ProgramResult reference_program = compile_and_run(
        "int main() {\n"
        "    long long value = 10000000000;\n"
        "    long long& ref = value;\n"
        "    ref = ref + 1;\n"
        "    return (int)(value / 1000000000);\n"
        "}\n");
    CHECK(reference_program.ok);
    CHECK(reference_program.result == 10);
}

TEST_CASE("C++ long long globals and fields keep both halves") {
    const ProgramResult program = compile_and_run(
        "long long global = 10000000000;\n"
        "struct Box { long long value; };\n"
        "int main() {\n"
        "    Box box;\n"
        "    box.value = global;\n"
        "    return (int)(box.value / 1000000000);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("C++ long long static members keep both halves") {
    const ProgramResult program = compile_and_run(
        "struct Box { static long long value; };\n"
        "long long Box::value = 10000000000;\n"
        "int main() {\n"
        "    return Box::value == 10000000000 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ long long literals carry across 64-bit addition") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long long positive = 10000000000;\n"
        "    long long negative = -10000000000;\n"
        "    return positive + negative == 0 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("C++ printf formats long long values") {
    const ProgramResult program = compile_and_run(
        "#include <stdio.h>\n"
        "int main() {\n"
        "    long long value = 100000;\n"
        "    long long big = value * value;\n"
        "    printf(\"%lld %llx\\n\", big, big);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.output == "10000000000 2540be400\n");
}

TEST_CASE("C++ long long functions pass and return 64-bit values") {
    const ProgramResult program = compile_and_run(
        "long long mul(long long a, long long b) { return a * b; }\n"
        "int main() {\n"
        "    return (int)(mul(100000, 100000) / 1000000000);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("C++ long long locals support 64-bit arithmetic") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long long a = 100000;\n"
        "    long long b = 100000;\n"
        "    long long c = a * b;\n"
        "    return (int)(c / 1000000000);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("range-for iterates initializer_list variables") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "int main() {\n"
        "    auto values = {1, 2, 3, 4};\n"
        "    int total = 0;\n"
        "    for (int value : values) { total += value; }\n"
        "    return total;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("auto initializer_list variables hold braced values") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "int main() {\n"
        "    auto values = {1, 2, 3, 4};\n"
        "    return values.data_[0] + values.data_[3] + values.size_;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 9);
}

TEST_CASE("auto deduces scalar and pointer return types") {
    const ProgramResult program = compile_and_run(
        "int* pointer() { static int value = 42; return &value; }\n"
        "long long wide() { return 10000000000; }\n"
        "double real() { return 2.5; }\n"
        "int main() {\n"
        "    auto p = pointer();\n"
        "    auto w = wide();\n"
        "    auto d = real();\n"
        "    return *p + (int)(w / 1000000000) + (int)d;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 54);
}

TEST_CASE("auto deduces class return types from calls") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "std::initializer_list<int> make() { return {4, 5, 6}; }\n"
        "int main() {\n"
        "    auto values = make();\n"
        "    return values.data_[0] + values.data_[2];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("reference copy constructors are selected") {
    const ProgramResult program = compile_and_run(
        "struct Value {\n"
        "    int number;\n"
        "    Value(int value) { number = value; }\n"
        "    Value(Value& other) { number = other.number; }\n"
        "};\n"
        "int main() { Value first(42); Value second{first}; return second.number; }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("constructors are selected by class argument type") {
    const ProgramResult program = compile_and_run(
        "struct Value {\n"
        "    int number;\n"
        "    Value(int value) { number = value; }\n"
        "    Value(Value other) { number = other.number; }\n"
        "};\n"
        "int main() { Value first(42); Value second{first}; return second.number; }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("functions return braced initializer_lists") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "std::initializer_list<int> make() { return {4, 5, 6}; }\n"
        "int main() {\n"
        "    std::initializer_list<int> values = make();\n"
        "    return values.data_[0] + values.data_[2];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("initializer_list constructors accept empty braced lists") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "struct Count {\n"
        "    int total;\n"
        "    Count(std::initializer_list<int> values) {\n"
        "        total = 0;\n"
        "        for (int value : values) total += value;\n"
        "    }\n"
        "};\n"
        "int main() { Count count{}; return count.total; }\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

