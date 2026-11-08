#include "cxx_test_support.h"

TEST_CASE("<cerrno> exposes errno") {
    const ProgramResult program = compile_and_run(
        "#include <cerrno>\n"
        "int main() {\n"
        "    std::errno = EINVAL;\n"
        "    if (std::errno != EINVAL) return 1;\n"
        "    const char* text = std::strerror(std::errno);\n"
        "    if (text == 0 || text[0] == 0) return 2;\n"
        "    if (EPERM != 1 || ENOENT != 2) return 3;\n"
        "    return EACCES == 13 && ENOSPC == 28 ? 0 : 4;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<ctime> provides time functions") {
    const ProgramResult program = compile_and_run(
        "#include <ctime>\n"
        "#include <ctime>\n"
        "int main() {\n"
        "    return sizeof(std::time_t) == 4 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstdlib> allocates memory") {
    const ProgramResult program = compile_and_run(
        "#include <cstdlib>\n"
        "int main() {\n"
        "    int* values = (int*)std::calloc(4, sizeof(int));\n"
        "    if (values == 0) return 1;\n"
        "    for (int index = 0; index < 4; ++index) {\n"
        "        if (values[index] != 0) return 2;\n"
        "    }\n"
        "    values[0] = 5;\n"
        "    values = (int*)std::realloc(values, 8 * sizeof(int));\n"
        "    if (values == 0 || values[0] != 5) return 3;\n"
        "    std::free(values);\n"
        "    void* memory = std::malloc(16);\n"
        "    std::free(memory);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstdio> provides formatted output") {
    const ProgramResult program = compile_and_run(
        "#include <cstdio>\n"
        "#include <cstdio>\n"
        "int main() {\n"
        "    char buffer[32];\n"
        "    std::sprintf(buffer, \"%d-%s\", 5, \"x\");\n"
        "    if (buffer[0] != '5') return 1;\n"
        "    char other[8];\n"
        "    int written = std::snprintf(other, 4, \"%s\", \"abcdef\");\n"
        "    if (written != 6) return 2;\n"
        "    if (other[0] != 'a' || other[2] != 'c' || other[3] != 0) return 3;\n"
        "    if (sizeof(std::FILE) == 0) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstring> exposes extended helpers") {
    const ProgramResult program = compile_and_run(
        "#include <cstring>\n"
        "int main() {\n"
        "    char buffer[32];\n"
        "    std::memset(buffer, 0, 32);\n"
        "    std::strcpy(buffer, \"hello\");\n"
        "    std::strncat(buffer, \" world\", 3);\n"
        "    if (std::strcmp(buffer, \"hello wo\") != 0) return 1;\n"
        "    if (std::strrchr(buffer, 'o') == 0) return 2;\n"
        "    char copy[32];\n"
        "    std::memmove(copy, buffer, 9);\n"
        "    if (std::memcmp(copy, buffer, 9) != 0) return 3;\n"
        "    if (std::memchr(buffer, 'w', 9) == 0) return 4;\n"
        "    char text[16];\n"
        "    std::strcpy(text, \"a,b,c\");\n"
        "    char* token = std::strtok(text, \",\");\n"
        "    if (token == 0 || token[0] != 'a') return 5;\n"
        "    char* duplicate = std::strdup(\"copy\");\n"
        "    if (std::strcmp(duplicate, \"copy\") != 0) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cctype> exposes character classification") {
    const ProgramResult program = compile_and_run(
        "#include <cctype>\n"
        "#include <cctype>\n"
        "int main() {\n"
        "    if (!std::isdigit('5')) return 1;\n"
        "    if (!std::isalpha('a')) return 2;\n"
        "    if (!std::isalnum('7')) return 3;\n"
        "    if (!std::isspace(' ')) return 4;\n"
        "    if (!std::isupper('A')) return 5;\n"
        "    if (!std::islower('a')) return 6;\n"
        "    if (std::toupper('a') != 'A') return 7;\n"
        "    if (std::tolower('A') != 'a') return 8;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstring> exposes C string helpers") {
    const ProgramResult program = compile_and_run(
        "#include <cstring>\n"
        "#include <cstring>\n"
        "int main() {\n"
        "    char buffer[16];\n"
        "    std::memset(buffer, 0, 16);\n"
        "    std::strcpy(buffer, \"hello\");\n"
        "    if (std::strlen(buffer) != 5) return 1;\n"
        "    if (std::strcmp(buffer, \"hello\") != 0) return 2;\n"
        "    if (std::strncmp(buffer, \"help\", 3) != 0) return 3;\n"
        "    if (std::strchr(buffer, 'l') == 0) return 4;\n"
        "    if (std::strstr(buffer, \"ell\") == 0) return 5;\n"
        "    std::strcat(buffer, \"!\");\n"
        "    if (std::strcmp(buffer, \"hello!\") != 0) return 6;\n"
        "    char copy[16];\n"
        "    std::memcpy(copy, buffer, 7);\n"
        "    if (std::memcmp(copy, buffer, 7) != 0) return 7;\n"
        "    char partial[16];\n"
        "    std::strncpy(partial, \"abcdef\", 3);\n"
        "    partial[3] = 0;\n"
        "    return std::strcmp(partial, \"abc\") == 0 ? 0 : 8;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function pointers can be called directly") {
    const ProgramResult program = compile_and_run(
        "int add(int left, int right) { return left + right; }\n"
        "int main() {\n"
        "    int (*operation)(int, int) = add;\n"
        "    if (operation(2, 3) != 5) return 1;\n"
        "    operation = add;\n"
        "    return operation(4, 5) == 9 ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstdlib> converts C strings") {
    const ProgramResult program = compile_and_run(
        "#include <cstdlib>\n"
        "#include <cstdlib>\n"
        "int compare_ints(const void* left, const void* right) {\n"
        "    int a = *(const int*)left;\n"
        "    int b = *(const int*)right;\n"
        "    return a < b ? -1 : (a > b ? 1 : 0);\n"
        "}\n"
        "int main() {\n"
        "    if (std::atoi(\"42\") != 42) return 1;\n"
        "    if (std::atoi(\"-7\") != -7) return 2;\n"
        "    if (std::atof(\"2.5\") != 2.5) return 3;\n"
        "    char* end;\n"
        "    if (std::strtol(\"123x\", &end, 10) != 123 || end[0] != 'x') return 4;\n"
        "    if (std::strtoul(\"ffz\", &end, 16) != 255 || end[0] != 'z') return 5;\n"
        "    if (std::labs(-7) != 7) return 6;\n"
        "    int values[5];\n"
        "    values[0] = 4; values[1] = 1; values[2] = 5;\n"
        "    values[3] = 2; values[4] = 3;\n"
        "    std::qsort(values, 5, sizeof(int), compare_ints);\n"
        "    for (int index = 0; index < 5; ++index) {\n"
        "        if (values[index] != index + 1) return 7;\n"
        "    }\n"
        "    int key = 3;\n"
        "    int* found = (int*)std::bsearch(&key, values, 5, sizeof(int), compare_ints);\n"
        "    if (found == 0 || *found != 3) return 8;\n"
        "    if (std::strtod(\"2.5x\", &end) != 2.5 || end[0] != 'x') return 9;\n"
        "    if (std::strtod(\"1.5e2x\", &end) != 150.0 || end[0] != 'x') return 10;\n"
        "    if (std::atof(\"1e2\") != 100.0) return 11;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cmath> provides scalar math") {
    const ProgramResult program = compile_and_run(
        "#include <cmath>\n"
        "#include <cmath>\n"
        "int main() {\n"
        "    if (std::abs(-3) != 3) return 1;\n"
        "    if (std::abs(-2.5) != 2.5) return 2;\n"
        "    if (std::sqrt(9.0) != 3.0) return 3;\n"
        "    if (std::fabs(-2.0) != 2.0) return 4;\n"
        "    if (std::sqrt(4.0f) != 2.0f) return 5;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<algorithm> provides std::min and std::max") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "#include <algorithm>\n"
        "int main() {\n"
        "    if (std::min(3, 5) != 3) return 1;\n"
        "    if (std::max(3, 5) != 5) return 2;\n"
        "    if (std::min(2.5, 1.5) != 1.5) return 3;\n"
        "    if (std::max(2.5, 1.5) != 2.5) return 4;\n"
        "    if (std::min('b', 'a') != 'a') return 5;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C++ #line updates source identity") {
    const ProgramResult program = compile_and_run(
        "#line 10 \"custom.cpp\"\n"
        "int main() {\n"
        "    if (__LINE__ != 11) return 1;\n"
        "    return __FILE__[0] == 'c' ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C++ #warning directives are accepted") {
    const ProgramResult program = compile_and_run(
        "#warning this should not stop compilation\n"
        "int main() { return 0; }\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("__func__ names the current function") {
    const ProgramResult program = compile_and_run(
        "const char* name() { return __func__; }\n"
        "struct Widget {\n"
        "    const char* method() const { return __func__; }\n"
        "};\n"
        "namespace tools { const char* helper() { return __func__; } }\n"
        "int main() {\n"
        "    const char* function = name();\n"
        "    if (function[0] != 'n' || function[3] != 'e') return 1;\n"
        "    Widget widget;\n"
        "    const char* method = widget.method();\n"
        "    if (method[0] != 'm' || method[5] != 'd') return 2;\n"
        "    const char* helper = tools::helper();\n"
        "    return helper[0] == 'h' ? 0 : 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("assignments convert between class types") {
    const ProgramResult program = compile_and_run(
        "struct Source { int value; };\n"
        "struct Target {\n"
        "    int value;\n"
        "    Target() : value(0) {}\n"
        "    Target(const Source& source) : value(source.value + 1) {}\n"
        "};\n"
        "int main() {\n"
        "    Source source;\n"
        "    source.value = 4;\n"
        "    Target target;\n"
        "    target = source;\n"
        "    return target.value - 5;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("returns convert between class types") {
    const ProgramResult program = compile_and_run(
        "struct Source { int value; };\n"
        "struct Target {\n"
        "    int first;\n"
        "    int second;\n"
        "    Target(const Source& source)\n"
        "        : first(source.value + 1), second(7) {}\n"
        "};\n"
        "Target make(Source source) { return source; }\n"
        "int main() {\n"
        "    Source source;\n"
        "    source.value = 4;\n"
        "    Target target = make(source);\n"
        "    if (target.first != 5) return 1;\n"
        "    return target.second - 7;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("constructors accept class arguments by value") {
    const ProgramResult program = compile_and_run(
        "struct Source { int value; };\n"
        "struct Target {\n"
        "    int value;\n"
        "    Target(Source source) : value(source.value + 1) {}\n"
        "};\n"
        "int take(Target target) { return target.value; }\n"
        "int main() {\n"
        "    Source source;\n"
        "    source.value = 4;\n"
        "    Target direct(source);\n"
        "    if (direct.value != 5) return 1;\n"
        "    Target copied = source;\n"
        "    if (copied.value != 5) return 2;\n"
        "    return take(source) - 5;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C-style casts construct class temporaries") {
    const ProgramResult program = compile_and_run(
        "class Number {\n"
        "public:\n"
        "    int value;\n"
        "    explicit Number(int input) : value(input) {}\n"
        "};\n"
        "int take(Number value) { return value.value; }\n"
        "int main() {\n"
        "    if (take((Number)5) != 5) return 1;\n"
        "    Number source(3);\n"
        "    Number copy = (Number)source;\n"
        "    if (copy.value != 3) return 2;\n"
        "    Number direct = (Number)6;\n"
        "    return direct.value - 6;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("explicit constructors reject implicit conversions") {
    const cxx::Result implicit = cxx::compile(
        "class Number {\n"
        "public:\n"
        "    int value;\n"
        "    explicit Number(int input) : value(input) {}\n"
        "};\n"
        "int main() { Number value = 5; return value.value; }\n");
    CHECK_FALSE(implicit.ok);

    const ProgramResult direct = compile_and_run(
        "class Number {\n"
        "public:\n"
        "    int value;\n"
        "    explicit Number(int input) : value(input) {}\n"
        "};\n"
        "int take(Number value) { return value.value; }\n"
        "int main() {\n"
        "    Number direct(5);\n"
        "    if (direct.value != 5) return 1;\n"
        "    return take(Number(6)) - 6;\n"
        "}\n");
    CHECK(direct.ok);
    CHECK(direct.result == 0);
}

TEST_CASE("default arguments fill missing call parameters") {
    const ProgramResult program = compile_and_run(
        "int sum(int a, int b = 2, int c = 3) { return a + b + c; }\n"
        "int value(int a = 4) { return a; }\n"
        "class Box {\n"
        "public:\n"
        "    int total;\n"
        "    Box() : total(0) {}\n"
        "    int add(int amount = 3) { total = total + amount; return total; }\n"
        "};\n"
        "class Defaulted {\n"
        "public:\n"
        "    int value;\n"
        "    Defaulted(int input = 5) : value(input) {}\n"
        "};\n"
        "class Pair {\n"
        "public:\n"
        "    int value;\n"
        "    Pair(int a, int b = 2) : value(a + b) {}\n"
        "};\n"
        "Defaulted defaulted_global;\n"
        "int main() {\n"
        "    if (sum(1) != 6) return 1;\n"
        "    if (sum(1, 5) != 9) return 2;\n"
        "    if (sum(1, 5, 6) != 12) return 3;\n"
        "    if (value() != 4) return 4;\n"
        "    Box box;\n"
        "    if (box.add() != 3) return 5;\n"
        "    if (box.add(4) != 7) return 6;\n"
        "    Defaulted local_defaulted;\n"
        "    if (local_defaulted.value != 5) return 7;\n"
        "    if (defaulted_global.value != 5) return 8;\n"
        "    Defaulted copy_defaulted = 4;\n"
        "    if (copy_defaulted.value != 4) return 9;\n"
        "    Pair pair(3);\n"
        "    if (pair.value != 5) return 10;\n"
        "    Pair pair_copy = 4;\n"
        "    if (pair_copy.value != 6) return 11;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("operator arrow forwards member access") {
    const ProgramResult program = compile_and_run(
        "struct Point { int x; int y; };\n"
        "class Box {\n"
        "public:\n"
        "    Point* pointer;\n"
        "    Box(Point* value) : pointer(value) {}\n"
        "    Point* operator->() { return pointer; }\n"
        "};\n"
        "int main() {\n"
        "    Point point;\n"
        "    point.x = 7;\n"
        "    point.y = 8;\n"
        "    Box box(&point);\n"
        "    if (box->x != 7) return 1;\n"
        "    if (box->y != 8) return 2;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("this pointer supports compound assignment") {
    const ProgramResult program = compile_and_run(
        "class Counter {\n"
        "public:\n"
        "    int value;\n"
        "    Counter(int input) : value(input) {}\n"
        "    Counter& operator+=(const Counter& other) {\n"
        "        value = value + other.value;\n"
        "        return *this;\n"
        "    }\n"
        "    Counter& add(const Counter& other) {\n"
        "        (*this) += other;\n"
        "        return *this;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Counter left(1);\n"
        "    Counter right(2);\n"
        "    left.add(right);\n"
        "    return left.value - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("namespace classes resolve unqualified self parameters") {
    const ProgramResult program = compile_and_run(
        "namespace outer {\n"
        "class Number {\n"
        "public:\n"
        "    int value;\n"
        "    Number(int input) : value(input) {}\n"
        "    Number& operator+=(const Number& other) {\n"
        "        value = value + other.value;\n"
        "        return *this;\n"
        "    }\n"
        "};\n"
        "}\n"
        "int main() {\n"
        "    outer::Number left(1);\n"
        "    outer::Number right(2);\n"
        "    left += right;\n"
        "    return left.value - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("standard iostream manipulators set the output base") {
    const ProgramResult program = compile_and_run(
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout << std::hex << 255 << std::endl;\n"
        "    std::cout << std::dec << 255 << std::endl;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
    CHECK(program.output == "ff\n255\n");
}

TEST_CASE("std::string supports find, substr, front, and back") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello world\");\n"
        "    if (text.find(\"world\") != 6) return 1;\n"
        "    std::string part = text.substr(6, 5);\n"
        "    if (part != \"world\") return 2;\n"
        "    if (text.front() != 'h') return 3;\n"
        "    if (text.back() != 'd') return 4;\n"
        "    if (text.rfind(\"l\") != 9) return 5;\n"
        "    if (text.compare(\"hello world\") != 0) return 6;\n"
        "    text.push_back('!');\n"
        "    if (text != \"hello world!\") return 7;\n"
        "    text.pop_back();\n"
        "    if (text.find('l') != 2) return 8;\n"
        "    if (text.rfind('l') != 9) return 9;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C++ can use the C FILE type") {
    const ProgramResult program = compile_and_run(
        "#include <stdio.h>\n"
        "int main() { FILE* stream = 0; return stream == 0 ? 0 : 1; }\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C++ typedef names resolve for struct types") {
    const ProgramResult program = compile_and_run(
        "typedef struct Node { int value; } Node;\n"
        "int main() { Node node; node.value = 9; return node.value; }\n");
    CHECK(program.ok);
    CHECK(program.result == 9);
}

