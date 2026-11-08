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

