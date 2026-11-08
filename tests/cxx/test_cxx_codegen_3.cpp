#include "cxx_test_support.h"

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

TEST_CASE("captureless lambdas call directly and through auto variables") {
    const ProgramResult immediate = compile_and_run(
        "int main() { return [](int x) { return x + 1; }(41); }\n");
    CHECK(immediate.ok);
    CHECK(immediate.result == 42);

    const ProgramResult stored = compile_and_run(
        "int main() {\n"
        "    int result = [](int x) { return x + 1; }(41);\n"
        "    auto twice = [](int x) { return x * 2; };\n"
        "    return result + twice(0);\n"
        "}\n");
    CHECK(stored.ok);
    CHECK(stored.result == 42);
}

TEST_CASE("lambdas capture values into closure objects") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "    auto f = [a, b](int x) { return a + b + x; };\n"
        "    a = 0;\n"
        "    b = 0;\n"
        "    return f(12);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("lambdas capture by reference and by default") {
    const ProgramResult reference = compile_and_run(
        "int main() {\n"
        "    int x = 1;\n"
        "    auto f = [&x](int y) { x = x + y; return x; };\n"
        "    int result = f(41);\n"
        "    return x + result - 42;\n"
        "}\n");
    CHECK(reference.ok);
    CHECK(reference.result == 42);

    const ProgramResult by_default = compile_and_run(
        "int main() {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "    auto f = [=](int x) { return a + b + x; };\n"
        "    a = 0;\n"
        "    b = 0;\n"
        "    return f(12);\n"
        "}\n");
    CHECK(by_default.ok);
    CHECK(by_default.result == 42);
}

TEST_CASE("condition variable timed waits accept past deadlines") {
    const ProgramResult program = compile_and_run(
        "#include <thread>\n"
        "#include <mutex>\n"
        "#include <condition_variable>\n"
        "int main() {\n"
        "    std::mutex guard;\n"
        "    std::condition_variable condition;\n"
        "    guard.lock();\n"
        "    condition.wait_for(guard, 0);\n"
        "    condition.wait_until(guard, 0);\n"
        "    guard.unlock();\n"
        "    return 42;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("std this_thread sleep_until accepts a past deadline") {
    const ProgramResult program = compile_and_run(
        "#include <thread>\n"
        "int main() {\n"
        "    std::this_thread::sleep_until(0);\n"
        "    return 42;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("std timed thread helpers accept zero waits") {
    const ProgramResult program = compile_and_run(
        "#include <thread>\n"
        "#include <mutex>\n"
        "int main() {\n"
        "    std::mutex guard;\n"
        "    guard.lock();\n"
        "    bool acquired = guard.try_lock_for(0);\n"
        "    guard.unlock();\n"
        "    std::this_thread::sleep_for(0);\n"
        "    return acquired ? 0 : 42;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);
}

TEST_CASE("std thread exposes a thread id type") {
    const ProgramResult program = compile_and_run(
        "#include <thread>\n"
        "int main() {\n"
        "    std::thread thread;\n"
        "    std::thread::id id = thread.get_id();\n"
        "    return id == 0 ? 42 : 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 42);

    const ProgramResult native = compile_and_run(
        "#include <kr32/thread.hpp>\n"
        "int main() {\n"
        "    kr32::thread thread;\n"
        "    kr32::thread::id id = thread.get_id();\n"
        "    return id == 0 ? 42 : 0;\n"
        "}\n");
    CHECK(native.ok);
    CHECK(native.result == 42);

    const ProgramResult current = compile_and_run(
        "#include <thread>\n"
        "int main() {\n"
        "    std::thread::id id = std::this_thread::get_id();\n"
        "    std::thread::id copy = id;\n"
        "    return copy == id ? 42 : 0;\n"
        "}\n");
    CHECK(current.ok);
    CHECK(current.result == 42);
}

TEST_CASE("std thread accepts capturing lambda closures") {
    const cxx::Result compiled = cxx::compile(
        "#include <thread>\n"
        "int main() {\n"
        "    int value = 42;\n"
        "    std::thread worker([value]() { return value; }, 0);\n"
        "    worker.join();\n"
        "    return 0;\n"
        "}\n");
    CHECK(compiled.ok);
}

TEST_CASE("captureless lambda thread runs on a cluster") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int main() {\n"
        "    std::thread worker([](int value) {\n"
        "        (void)__builtin_kr32_syscall(47, value, 0, 0);\n"
        "    }, 42);\n"
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

TEST_CASE("std::move transfers a thread handle") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int main() {\n"
        "    std::thread first([](int value) {\n"
        "        (void)__builtin_kr32_syscall(47, value, 0, 0);\n"
        "    }, 42);\n"
        "    std::thread second{std::move(first)};\n"
        "    second.join();\n"
        "    return first.joinable() ? 1 : 0;\n"
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
    CHECK(host.cluster().core(0).exit_code() == 0);
    CHECK(host.cluster().core(1).halted());
    CHECK(host.cluster().core(1).exit_code() == 42);
}

TEST_CASE("thread handles transfer with release and adopt") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int main() {\n"
        "    std::thread first([](int value) {\n"
        "        (void)__builtin_kr32_syscall(47, value, 0, 0);\n"
        "    }, 42);\n"
        "    std::thread second;\n"
        "    second.adopt(first.release());\n"
        "    second.join();\n"
        "    return first.joinable() ? 1 : 0;\n"
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
    CHECK(host.cluster().core(0).exit_code() == 0);
    CHECK(host.cluster().core(1).halted());
    CHECK(host.cluster().core(1).exit_code() == 42);
}

TEST_CASE("detached thread runs to completion on a cluster") {
    container::Image image;
    std::string error;
    REQUIRE(build_cluster_image(
        "#include <thread>\n"
        "int main() {\n"
        "    std::thread worker([](int value) {\n"
        "        (void)__builtin_kr32_syscall(47, value, 0, 0);\n"
        "    }, 42);\n"
        "    worker.detach();\n"
        "    return 0;\n"
        "}\n",
        image, error));
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    REQUIRE(host.load_image(image, error));
    host.start(image.entry);
    for (int step = 0; step < 2000000; ++step) {
        if (host.cluster().core(1).halted()) {
            break;
        }
        (void)host.step();
    }
    CHECK(host.cluster().core(1).halted());
    CHECK(host.cluster().core(1).exit_code() == 42);
}

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

