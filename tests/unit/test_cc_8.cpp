#include "cc_test_support.h"

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
