#include "cxx_test_support.h"

TEST_CASE("capturing thread shares globals after join") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int result = 0;\n"
        "int main() {\n"
        "    int value = 42;\n"
        "    std::thread worker([value]() { result = value; }, 0);\n"
        "    worker.join();\n"
        "    return result;\n"
        "}\n",
        image, error));
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    REQUIRE(host.load_image(image, error));
    host.start(image.entry);
    for (int step = 0; step < 2000000; ++step) {
        if (host.cluster().core(0).halted()) {
            break;
        }
        (void)host.step();
    }
    REQUIRE(host.cluster().core(0).halted());
    CHECK(host.cluster().core(0).exit_code() == 42);
}

TEST_CASE("mutex works in a capturing lambda thread") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "#include <mutex>\n"
        "int main() {\n"
        "    std::mutex guard;\n"
        "    std::thread worker([&guard]() {\n"
        "        guard.lock();\n"
        "        guard.unlock();\n"
        "    }, 0);\n"
        "    worker.join();\n"
        "    return 42;\n"
        "}\n",
        image, error));
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    REQUIRE(host.load_image(image, error));
    host.start(image.entry);
    for (int step = 0; step < 2000000; ++step) {
        if (host.cluster().core(0).halted()) {
            break;
        }
        (void)host.step();
    }
    REQUIRE(host.cluster().core(0).halted());
    CHECK(host.cluster().core(0).exit_code() == 42);
}

TEST_CASE("function pointer thread runs on a cluster") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int worker(int value) {\n"
        "    (void)__builtin_kr32_syscall(47, value, 0, 0);\n"
        "    return 0;\n"
        "}\n"
        "int main() {\n"
        "    std::thread thread(worker, 42);\n"
        "    thread.join();\n"
        "    return 0;\n"
        "}\n",
        image, error));
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    REQUIRE(host.load_image(image, error));
    host.start(image.entry);
    for (int step = 0; step < 2000000; ++step) {
        if (host.cluster().core(0).halted() &&
            host.cluster().core(1).halted()) {
            break;
        }
        (void)host.step();
    }
    CHECK(host.cluster().core(0).halted());
    CHECK(host.cluster().core(1).halted());
    CHECK(host.cluster().core(1).exit_code() == 42);
}

TEST_CASE("capturing lambda thread runs on a cluster") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int main() {\n"
        "    int value = 42;\n"
        "    std::thread worker([value]() {\n"
        "        (void)__builtin_kr32_syscall(47, value, 0, 0);\n"
        "    }, 0);\n"
        "    worker.join();\n"
        "    return 0;\n"
        "}\n",
        image, error));
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    REQUIRE(host.load_image(image, error));
    host.start(image.entry);
    for (int step = 0; step < 2000000; ++step) {
        if (host.cluster().core(0).halted() &&
            host.cluster().core(1).halted()) {
            break;
        }
        (void)host.step();
    }
    CHECK(host.cluster().core(0).halted());
    CHECK(host.cluster().core(1).halted());
    CHECK(host.cluster().core(1).exit_code() == 42);
}

TEST_CASE("std thread accepts function pointers and captureless lambdas") {
    const cxx::Result compiled = cxx::compile(
        "#include <thread>\n"
        "int worker(int* value) { return *value; }\n"
        "int main() {\n"
        "    std::thread a(worker, 1);\n"
        "    std::thread b([](int value) { return value; }, 2);\n"
        "    std::thread c([](int value) { return value; }, 3);\n"
        "    c.detach();\n"
        "    return a.joinable() + b.joinable();\n"
        "}\n");
    CHECK(compiled.ok);
}

TEST_CASE("mutex try_lock, recursive_mutex, and hardware_concurrency run") {
    const ProgramResult program = compile_and_run(
        "#include <mutex>\n"
        "#include <thread>\n"
        "int main() {\n"
        "    std::mutex guard;\n"
        "    if (!guard.try_lock()) return 1;\n"
        "    guard.unlock();\n"
        "    std::recursive_mutex recursive;\n"
        "    recursive.lock();\n"
        "    recursive.lock();\n"
        "    recursive.unlock();\n"
        "    recursive.unlock();\n"
        "    return std::thread::hardware_concurrency() > 0 ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
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

TEST_CASE("twelve-byte floating aggregates use a partial second FPR") {
    const ProgramResult program = compile_and_run(
        "struct Three { float a; float b; float c; };\n"
        "Three make() {\n"
        "    Three value; value.a = 1.0f; value.b = 2.0f; value.c = 3.0f;\n"
        "    return value;\n"
        "}\n"
        "float sum(Three value) { return value.a + value.b + value.c; }\n"
        "int main() { Three value = make(); return (int)sum(value); }\n");
    CHECK(program.ok);
    CHECK(program.result == 6);
}

TEST_CASE("sixteen-byte floating aggregates use two FP registers") {
    const ProgramResult program = compile_and_run(
        "struct Four { float a; float b; float c; float d; };\n"
        "Four make() {\n"
        "    Four value;\n"
        "    value.a = 1.0f; value.b = 2.0f; value.c = 3.0f; value.d = 4.0f;\n"
        "    return value;\n"
        "}\n"
        "float sum(Four value) { return value.a + value.b + value.c + value.d; }\n"
        "int main() { Four value = make(); return (int)sum(value); }\n");
    CHECK(program.ok);
    CHECK(program.result == 10);
}

TEST_CASE("larger aggregates pass and return through hidden pointers") {
    const ProgramResult program = compile_and_run(
        "struct Big { int whole; double fraction; };\n"
        "Big make() { Big value; value.whole = 40; value.fraction = 2.5; return value; }\n"
        "int read(Big value) { return value.whole + (int)value.fraction; }\n"
        "int main() { Big value = make(); return read(value); }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("mixed integer and floating aggregates use register ABI") {
    const ProgramResult program = compile_and_run(
        "struct Mixed { int whole; float fraction; };\n"
        "Mixed make() { Mixed value; value.whole = 40; value.fraction = 2.5f; return value; }\n"
        "int read(Mixed value) { return value.whole + (int)value.fraction; }\n"
        "int main() { Mixed value = make(); return read(value); }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("floating aggregates use register ABI") {
    const ProgramResult program = compile_and_run(
        "struct F { float value; };\n"
        "struct D { double value; };\n"
        "F make_f() { F object; object.value = 2.5f; return object; }\n"
        "D make_d() { D object; object.value = 4.5; return object; }\n"
        "float read_f(F object) { return object.value; }\n"
        "double read_d(D object) { return object.value; }\n"
        "int main() {\n"
        "    F f = make_f();\n"
        "    D d = make_d();\n"
        "    return (int)(read_f(f) + read_d(d));\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 7);
}

TEST_CASE("eight-byte classes return in two registers") {
    const ProgramResult program = compile_and_run(
        "struct Pair { int first; int second; };\n"
        "Pair make() { Pair pair; pair.first = 40; pair.second = 2; return pair; }\n"
        "int main() { Pair pair = make(); return pair.first + pair.second; }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("trivial byte and short classes use register ABI") {
    const ProgramResult program = compile_and_run(
        "struct Byte { char value; };\n"
        "struct Short { short value; };\n"
        "Byte make_byte() { Byte object; object.value = 7; return object; }\n"
        "Short make_short() { Short object; object.value = 35; return object; }\n"
        "int read_byte(Byte object) { return object.value; }\n"
        "int read_short(Short object) { return object.value; }\n"
        "int main() {\n"
        "    Byte byte = make_byte();\n"
        "    Short small = make_short();\n"
        "    return read_byte(byte) + read_short(small);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("trivial four-byte classes return in a register") {
    const ProgramResult program = compile_and_run(
        "struct Small { int value; };\n"
        "Small make() { Small object; object.value = 42; return object; }\n"
        "int main() { Small object = make(); return object.value; }\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("trivial four-byte classes pass by value in a register") {
    const ProgramResult program = compile_and_run(
        "struct Small { int value; };\n"
        "int read(Small object) { return object.value; }\n"
        "int main() {\n"
        "    Small object;\n"
        "    object.value = 42;\n"
        "    return read(object);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
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

TEST_CASE("template statics are emitted in comdat sections") {
    const cxx::Result compiled = cxx::compile(
        "template <typename T> struct Counter { static int value; };\n"
        "template <typename T> int Counter<T>::value = 1;\n"
        "template <typename T> int next() { static int local = 0; return ++local; }\n"
        "int main() { return Counter<int>::value + next<int>(); }\n");
    REQUIRE(compiled.ok);
    CHECK(compiled.assembly.find(".section .data.") != std::string::npos);
    CHECK(compiled.assembly.find(".section .bss.") != std::string::npos);
}

TEST_CASE("template function local statics merge across translation units") {
    const std::string header =
        "template <typename T> int next() { static int value = 0; value = value + 1; return value; }\n";
    const cxx::Result first = cxx::compile(
        header +
        "int bump() { return next<int>(); }\n",
        cxx::Options{.source_name = "local_a.cpp"});
    const cxx::Result second = cxx::compile(
        header +
        "int bump();\n"
        "int main() { return bump() * 10 + next<int>(); }\n",
        cxx::Options{.source_name = "local_b.cpp"});
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    const ProgramResult program = run_assemblies({first.assembly, second.assembly});
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 12);
}

TEST_CASE("template static data merges across translation units") {
    const std::string header =
        "template <typename T> struct Counter { static int value; };\n"
        "template <typename T> int Counter<T>::value = 0;\n";
    const cxx::Result first = cxx::compile(
        header +
        "int bump() { return ++Counter<int>::value; }\n",
        cxx::Options{.source_name = "counter_a.cpp"});
    const cxx::Result second = cxx::compile(
        header +
        "int bump();\n"
        "int main() { return bump() * 10 + ++Counter<int>::value; }\n",
        cxx::Options{.source_name = "counter_b.cpp"});
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    const ProgramResult program = run_assemblies({first.assembly, second.assembly});
    CHECK(program.ok);
    CHECK(program.result == 12);
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

TEST_CASE("partial specializations match nested template-id patterns") {
    const ProgramResult program = compile_and_run(
        "template <typename T> struct Box { int kind() { return 1; } };\n"
        "template <typename T> struct Box<Box<T>> { int kind() { return 2; } };\n"
        "int main() { Box<Box<int>> value; return value.kind(); }\n");
    CHECK(program.ok);
    CHECK(program.result == 2);
}

TEST_CASE("template array bounds accept symbolic expressions") {
    const ProgramResult program = compile_and_run(
        "template <int N, int M> struct Grid {\n"
        "    int values[N + M];\n"
        "    int size() { return N + M; }\n"
        "};\n"
        "template <int N, int M> struct Product {\n"
        "    int values[(N + M) * 2];\n"
        "    int size() { return N * 2 + M * 2; }\n"
        "};\n"
        "int main() {\n"
        "    Grid<2, 3> grid;\n"
        "    grid.values[4] = 40;\n"
        "    Product<1, 2> product;\n"
        "    product.values[5] = 2;\n"
        "    return grid.size() + product.size() +\n"
        "           grid.values[4] + product.values[5] - 11;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("non-type template array bounds accept constant multipliers") {
    const ProgramResult program = compile_and_run(
        "template <int N> struct Array {\n"
        "    int values[N * 2 + 1];\n"
        "    int size() { return N * 2 + 1; }\n"
        "};\n"
        "int main() {\n"
        "    Array<2> array;\n"
        "    array.values[4] = 42;\n"
        "    return array.size() + array.values[4] - 6;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 41);
}

