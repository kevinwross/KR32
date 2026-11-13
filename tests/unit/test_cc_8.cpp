#include "cc_test_support.h"

TEST_CASE("c constant folding") {
    const cc::Result result = cc::compile(
        "int math(void) { return 1 + 2 * 3; }\n"
        "int branch(void) { if (0) return 1; return 2; }\n"
        "int loop(void) { int x = 0; while (0) x = x + 1; return x; }\n"
        "int is_one(int value) { return value == 1; }\n"
        "int read(void) { int value = 3; return value; }\n"
        "int assign(int value) { int result = 0; result = value + 2; return result; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("MOV R0, #7") != std::string::npos);
    CHECK(result.assembly.find("MUL") == std::string::npos);
    CHECK(result.assembly.find("SLTU R0, R0, #1") != std::string::npos);
    CHECK(result.assembly.find("LOAD R0, [R7 + 0]") != std::string::npos);
    CHECK(result.assembly.find("STORE R0, [R7 + 4]") != std::string::npos);
    CHECK(result.assembly.find("LEA R0, [R7 + 0]") == std::string::npos);
}

TEST_CASE("c comparisons branch directly") {
    const cc::Result result = cc::compile(
        "int f(int x) { if (x == 1) return 1; return 2; }\n"
        "int g(int a, int b) { if (a < b) return 1; return 2; }\n"
        "int d(int x) { do { x = x - 1; } while (x > 0); return x; }\n"
        "int t(int x) { return x == 1 ? 10 : 20; }\n"
        "int add(int a, int b) { return a + b; }\n"
        "int inc(int value) { ++value; return value; }\n"
        "int addto(int a, int b) { a += b; return a; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("CMP R0, #1") != std::string::npos);
    CHECK(result.assembly.find("JNZ .Lelse") != std::string::npos);
    CHECK(result.assembly.find("cmpend") == std::string::npos);
}

TEST_CASE("c tail calls use jumps") {
    const cc::Result result = cc::compile(
        "int leaf(int x) { return x + 1; }\n"
        "int tail(int x) { return leaf(x); }\n"
        "int main(void) { return tail(4) == 5 ? 0 : 1; }\n",
        "<input>", 0, true);
    CHECK(result.ok);
    CHECK(result.assembly.find("CALL leaf") == std::string::npos);
    CHECK(result.assembly.find("JMP leaf") != std::string::npos);
    const ProgramResult program = compile_and_run(
        "int leaf(int x) { return x + 1; }\n"
        "int tail(int x) { return leaf(x); }\n"
        "int main(void) { return tail(4) == 5 ? 0 : 1; }\n",
        true);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c trivial calls inline") {
    const cc::Result result = cc::compile(
        "int five(void) { return 5; }\n"
        "int g = 7;\n"
        "int getg(void) { return g; }\n"
        "int main(void) { return five() + getg() == 12 ? 0 : 1; }\n",
        "<input>", 0, true);
    CHECK(result.ok);
    CHECK(result.assembly.find("CALL five") == std::string::npos);
    CHECK(result.assembly.find("CALL getg") == std::string::npos);
    const ProgramResult program = compile_and_run(
        "int five(void) { return 5; }\n"
        "int g = 7;\n"
        "int getg(void) { return g; }\n"
        "int main(void) { return five() + getg() == 12 ? 0 : 1; }\n",
        true);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c leaf locals stay in registers") {
    const ProgramResult program = compile_and_run(
        "int leaf(int a) { int x = a + 1; return x + x + x; }\n"
        "int main(void) { return leaf(2) == 9 ? 0 : 1; }\n",
        true);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c logical not uses SLTU") {
    const cc::Result result = cc::compile(
        "int f(int x) { return !x; }\n"
        "long long g(long long y) { return !y; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("SLTU R0, R0, #1") != std::string::npos);
    CHECK(result.assembly.find("OR R0, R0, R1") != std::string::npos);
}

TEST_CASE("c local comparisons avoid stack temporaries") {
    const cc::Result result = cc::compile(
        "int cmp(int a, int b) { if (a < b) return 1; return 2; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("PUSH R0") == std::string::npos);
    CHECK(result.assembly.find("CMP R0, R1") != std::string::npos);
}

TEST_CASE("c equality values avoid branches") {
    const cc::Result result = cc::compile(
        "int eq(int a, int b) { return a == b; }\n"
        "int ne(int a, int b) { return a != b; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("XOR R0, R1, R0") != std::string::npos);
    CHECK(result.assembly.find("SLTU R0, R0, #1") != std::string::npos);
}

TEST_CASE("c comparison values use SLT") {
    const cc::Result result = cc::compile(
        "int lt(int a, int b) { return a < b; }\n"
        "int ge(int a, int b) { return a >= b; }\n"
        "int z(int x) { return x == 0; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("SLT R0, R1, R0") != std::string::npos);
    CHECK(result.assembly.find("XOR R0, R0, #0") == std::string::npos);
}

TEST_CASE("c constant locals propagate") {
    const cc::Result result = cc::compile(
        "int f(void) { const int N = 5; return N; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("LOAD R0, [R7 + 0]") == std::string::npos);
    CHECK(result.assembly.find("MOV R0, #5") != std::string::npos);
}

TEST_CASE("c simple calls avoid argument temporaries") {
    const cc::Result result = cc::compile(
        "int add(int a, int b) { return a + b; }\n"
        "int call(int x, int y) { return add(x, y); }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("STORE R0, [R7 + 8]") == std::string::npos);
    CHECK(result.assembly.find("CALL add") != std::string::npos);
}

TEST_CASE("c optimized branches fold boolean tests") {
    const cc::Result result = cc::compile(
        "int f(int x) { if (x == 1) return 1; return 2; }\n",
        "<input>", 0, true);
    CHECK(result.ok);
    CHECK(result.assembly.find("CMP R0, #1") != std::string::npos);
    CHECK(result.assembly.find("JNZ .Lelse") != std::string::npos);
    CHECK(result.assembly.find("cmpend") == std::string::npos);
}

TEST_CASE("optimized c programs preserve behavior") {
    const ProgramResult program = compile_and_run(
        "int add(int left, int right) { return left + right; }\n"
        "int is_one(int value) { return value == 1; }\n"
        "int main(void) {\n"
        "    int total = 0;\n"
        "    for (int index = 0; index < 5; ++index) total += index;\n"
        "    if (total != 10) return 1;\n"
        "    if (!is_one(1) || is_one(2)) return 2;\n"
        "    int value = 3;\n"
        "    int *pointer = &value;\n"
        "    *pointer = add(value, 4);\n"
        "    return value == 7 ? 0 : 3;\n"
        "}\n",
        true);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c atomic read modify write operations") {
    const ProgramResult program = compile_and_run(
        "_Atomic int counter = 3;\n"
        "int main(void) {\n"
        "    int old = counter++;\n"
        "    if (old != 3 || counter != 4) return 1;\n"
        "    int now = ++counter;\n"
        "    if (now != 5) return 2;\n"
        "    counter += 4;\n"
        "    if (counter != 9) return 3;\n"
        "    counter -= 2;\n"
        "    if (counter != 7) return 4;\n"
        "    counter |= 8;\n"
        "    if (counter != 15) return 5;\n"
        "    counter &= 12;\n"
        "    if (counter != 12) return 6;\n"
        "    counter ^= 5;\n"
        "    return counter == 9 ? 0 : 7;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c atomic declarations use atomic loads and stores") {
    const ProgramResult program = compile_and_run(
        "_Atomic int counter = 3;\n"
        "_Atomic(int) total = 1;\n"
        "_Atomic char flag = 'a';\n"
        "int main(void) {\n"
        "    if (counter != 3) return 1;\n"
        "    counter = 7;\n"
        "    if (counter != 7) return 2;\n"
        "    total = 9;\n"
        "    if (total != 9) return 3;\n"
        "    flag = 'z';\n"
        "    return flag == 'z' ? 0 : 4;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c thread_local globals use tls storage") {
    const ProgramResult program = compile_and_run(
        "_Thread_local int first = 3;\n"
        "_Thread_local int second;\n"
        "_Thread_local double ratio = 1.5;\n"
        "static _Thread_local char text[8];\n"
        "int main(void) {\n"
        "    if (first != 3) return 1;\n"
        "    if (second != 0) return 2;\n"
        "    if (ratio != 1.5) return 3;\n"
        "    text[0] = 'a';\n"
        "    text[7] = 'z';\n"
        "    if (text[0] != 'a' || text[7] != 'z') return 4;\n"
        "    first = 10;\n"
        "    return first == 10 ? 0 : 5;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c atomic builtins use ll and sc") {
    const ProgramResult program = compile_and_run(
        "int main(void) {\n"
        "    int value = 5;\n"
        "    if (__atomic_load_n(&value, 0) != 5) return 1;\n"
        "    __atomic_store_n(&value, 7, 0);\n"
        "    if (value != 7) return 2;\n"
        "    if (__atomic_fetch_add(&value, 3, 0) != 7) return 3;\n"
        "    if (value != 10) return 4;\n"
        "    int expected = 10;\n"
        "    if (!__atomic_compare_exchange_n(&value, &expected, 20, 0, 0, 0)) return 5;\n"
        "    if (value != 20) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("stdatomic header wraps atomic builtins") {
    const ProgramResult program = compile_and_run(
        "#include <stdatomic.h>\n"
        "int main(void) {\n"
        "    atomic_int value;\n"
        "    value = 5;\n"
        "    if (atomic_load(&value) != 5) return 1;\n"
        "    atomic_store(&value, 7);\n"
        "    if (atomic_load(&value) != 7) return 2;\n"
        "    if (atomic_fetch_add(&value, 3) != 7) return 3;\n"
        "    int expected = 10;\n"
        "    if (!atomic_compare_exchange_strong(&value, &expected, 20)) return 4;\n"
        "    return atomic_load(&value) == 20 ? 0 : 5;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("heap allocation remains consistent") {
    const ProgramResult program = compile_and_run(
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "int main(void) {\n"
        "    char *text = (char *)malloc(16);\n"
        "    if (text == 0) return 1;\n"
        "    strcpy(text, \"hello\");\n"
        "    char *bigger = (char *)realloc(text, 64);\n"
        "    if (bigger == 0) return 2;\n"
        "    if (strcmp(bigger, \"hello\") != 0) return 3;\n"
        "    free(bigger);\n"
        "    int *values = (int *)calloc(4, sizeof(int));\n"
        "    if (values == 0) return 4;\n"
        "    if (values[0] != 0 || values[3] != 0) return 5;\n"
        "    free(values);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("packed attributes remove struct padding") {
    const ProgramResult program = compile_and_run(
        "struct __attribute__((packed)) Packed { char tag; int value; };\n"
        "struct Plain { char tag; int value; };\n"
        "struct Trailing { char tag; int value; } __attribute__((packed));\n"
        "int main(void) {\n"
        "    if (sizeof(struct Packed) != 5) return 1;\n"
        "    if (__builtin_offsetof(struct Packed, value) != 1) return 2;\n"
        "    if (sizeof(struct Plain) != 8) return 3;\n"
        "    if (__builtin_offsetof(struct Plain, value) != 4) return 4;\n"
        "    if (sizeof(struct Trailing) != 5) return 5;\n"
        "    if (__builtin_offsetof(struct Trailing, value) != 1) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("aligned attributes raise struct alignment") {
    const ProgramResult program = compile_and_run(
        "struct __attribute__((aligned(16))) Before { char value; };\n"
        "struct After { char value; } __attribute__((aligned(8)));\n"
        "struct __attribute__((packed, aligned(8))) Mixed { char tag; int value; };\n"
        "int main(void) {\n"
        "    if (sizeof(struct Before) != 16) return 1;\n"
        "    if (_Alignof(struct Before) != 16) return 2;\n"
        "    if (sizeof(struct After) != 8) return 3;\n"
        "    if (_Alignof(struct After) != 8) return 4;\n"
        "    if (__builtin_offsetof(struct Mixed, value) != 1) return 5;\n"
        "    if (sizeof(struct Mixed) != 8) return 6;\n"
        "    if (_Alignof(struct Mixed) != 8) return 7;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("attribute specifiers are skipped") {
    const ProgramResult program = compile_and_run(
        "int value __attribute__((unused)) = 3;\n"
        "int add(int a, int b) __attribute__((unused));\n"
        "int add(int a, int b) { return a + b; }\n"
        "int main(void) {\n"
        "    int other __attribute__((unused)) = 4;\n"
        "    int declared __attribute__((unused));\n"
        "    declared = 2;\n"
        "    return value + other + add(1, 2) + declared - 12;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("the preprocessor reports error and warning directives") {
    const cc::Result warning = cc::compile(
        "#warning check this\n"
        "int main(void) { return 0; }\n");
    CHECK(warning.ok);
    REQUIRE_FALSE(warning.diagnostics.empty());
    CHECK(warning.diagnostics.front().message.find("warning: check this") !=
          std::string::npos);
    CHECK(warning.diagnostics.front().line == 1);

    const cc::Result error = cc::compile(
        "#error stop here\n"
        "int main(void) { return 0; }\n");
    CHECK_FALSE(error.ok);
    REQUIRE_FALSE(error.diagnostics.empty());
    CHECK(error.diagnostics.front().message.find("error: stop here") !=
          std::string::npos);
}

TEST_CASE("inactive preprocessor errors are ignored") {
    const cc::Result result = cc::compile(
        "#if 0\n"
        "#error not reached\n"
        "#endif\n"
        "int main(void) { return 0; }\n");
    CHECK(result.ok);
    CHECK(result.diagnostics.empty());
}

TEST_CASE("the C compiler reports syntax errors with positions") {
    const cc::Result result = cc::compile("int main(void) { return 1 + ; }\n");
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().line >= 1);
}

TEST_CASE("quoted user headers are expanded into the translation unit") {
    const auto directory = std::filesystem::current_path() / "kr32-cc-header-test";
    std::filesystem::create_directories(directory);
    {
        std::ofstream header(directory / "pair.h", std::ios::binary);
        header << "#pragma once\n"
                  "struct pair { int left; int right; };\n";
    }
    const auto source_path = directory / "main.c";
    {
        std::ofstream source(source_path, std::ios::binary);
        source << "#include \"pair.h\"\n"
                  "int main(void) {\n"
                  "    struct pair value;\n"
                  "    value.left = 3;\n"
                  "    value.right = 4;\n"
                  "    return value.left + value.right;\n"
                  "}\n";
    }
    const cc::Result result = cc::compile(
        "#include \"pair.h\"\n"
        "int main(void) {\n"
        "    struct pair value;\n"
        "    value.left = 3;\n"
        "    value.right = 4;\n"
        "    return value.left + value.right;\n"
        "}\n",
        source_path.string());
    CHECK(result.ok);
    CHECK(result.diagnostics.empty());
    std::filesystem::remove_all(directory);
}

TEST_CASE("C supports long double as a double-width type") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long double value = 2.5L;\n"
        "    if (sizeof(long double) != 8) return 1;\n"
        "    return value == 2.5L ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C accepts _Atomic scalar declarations") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    _Atomic int value = 1;\n"
        "    value = value + 1;\n"
        "    return value == 2 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C accepts _Atomic(type) declarations") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    _Atomic(int) value = 3;\n"
        "    return value == 3 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C accepts _Noreturn function declarations") {
    const ProgramResult program = compile_and_run(
        "_Noreturn void stop(void);\n"
        "void stop(void) { for (;;) { } }\n"
        "int main() { return 0; }\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C accepts _Thread_local declarations") {
    const ProgramResult program = compile_and_run(
        "_Thread_local int value = 5;\n"
        "int main() { return value == 5 ? 0 : 1; }\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C accepts _Complex scalar declarations") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    _Complex double value = 1.0;\n"
        "    return value == 1.0 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C accepts _Imaginary scalar declarations") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    _Imaginary double value = 0.0;\n"
        "    return value == 0.0 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("c parenthesised constants compare without a stack save") {
    const cc::Result result = cc::compile(
        "int f(int x) { if (x < (8)) return 1; return 0; }\n"
        "int g(int x) { return x < (8); }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("PUSH R0") == std::string::npos);
    CHECK(result.assembly.find("MOV R1, #8") != std::string::npos);
}
