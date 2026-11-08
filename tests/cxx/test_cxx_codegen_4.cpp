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

TEST_CASE("functions return empty braced initializer_lists") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "std::initializer_list<int> empty() { return {}; }\n"
        "int main() {\n"
        "    std::initializer_list<int> values = empty();\n"
        "    return values.size_;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("initializer_list constructors keep 64-bit elements") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "struct Sum {\n"
        "    long long total;\n"
        "    Sum(std::initializer_list<long long> values) {\n"
        "        total = 0;\n"
        "        for (long long value : values) total += value;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Sum sum{10000000000, 2};\n"
        "    return (int)(sum.total / 1000000000);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("constructors accept initializer_list arguments") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "struct Sum {\n"
        "    int total;\n"
        "    Sum(std::initializer_list<int> values) {\n"
        "        total = 0;\n"
        "        for (int value : values) total += value;\n"
        "    }\n"
        "};\n"
        "int main() { Sum sum{1, 2, 3}; return sum.total; }\n");
    CHECK(program.ok);
    CHECK(program.result == 6);
}

TEST_CASE("initializer_list keeps 64-bit elements") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "int main() {\n"
        "    auto values = {10000000000, 2};\n"
        "    return (int)(values.data_[0] / 1000000000) + values.data_[1];\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 12);
}

TEST_CASE("initializer_list begin and end iterate values") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "int main() {\n"
        "    auto values = {7, 8};\n"
        "    int total = 0;\n"
        "    for (const int* p = values.begin(); p != values.end(); ++p) {\n"
        "        total += *p;\n"
        "    }\n"
        "    return total;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 15);
}

TEST_CASE("pointer-returning methods initialize locals") {
    const ProgramResult program = compile_and_run(
        "struct Holder { int* value; int* get() { return value; } };\n"
        "int main() {\n"
        "    int number = 42;\n"
        "    Holder holder;\n"
        "    holder.value = &number;\n"
        "    int* pointer = holder.get();\n"
        "    return *pointer;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("initializer_list parameters accept braced arguments") {
    const ProgramResult program = compile_and_run(
        "#include <initializer_list>\n"
        "int sum(std::initializer_list<int> values) {\n"
        "    int total = 0;\n"
        "    for (int index = 0; index < values.size_; ++index) {\n"
        "        total += values.data_[index];\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main() { return sum({1, 2, 3, 4}); }\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("range-for binds reference loop variables") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int values[3] = {1, 2, 3};\n"
        "    for (int& value : values) value = value + 1;\n"
        "    if (values[0] != 2) return 1;\n"
        "    if (values[1] != 3) return 2;\n"
        "    if (values[2] != 4) return 3;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("range-for iterates classes with begin and end") {
    const ProgramResult program = compile_and_run(
        "struct Range {\n"
        "    int values[3];\n"
        "    int* begin() { return values; }\n"
        "    int* end() { return values + 3; }\n"
        "};\n"
        "int main() {\n"
        "    Range range;\n"
        "    range.values[0] = 1;\n"
        "    range.values[1] = 2;\n"
        "    range.values[2] = 3;\n"
        "    int sum = 0;\n"
        "    for (int value : range) sum += value;\n"
        "    return sum == 6 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("range-for iterates braced initializer lists") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int total = 0;\n"
        "    for (int value : {1, 2, 3, 4}) { total += value; }\n"
        "    return total;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("explicit class template specializations override generic instantiations") {
    const ProgramResult program = compile_and_run(
        "template <typename T> class Box {\n"
        "public:\n"
        "    T value;\n"
        "    int generic() { return 1; }\n"
        "};\n"
        "template <> class Box<int> {\n"
        "public:\n"
        "    int value;\n"
        "    int specialized() { return 41; }\n"
        "};\n"
        "int main() {\n"
        "    Box<int> box;\n"
        "    box.value = 1;\n"
        "    return box.specialized() + box.value;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
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
