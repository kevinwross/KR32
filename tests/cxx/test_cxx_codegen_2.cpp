#include "cxx_test_support.h"

TEST_CASE("constant folding and dead branches") {
    const cxx::Result result = cxx::compile(
        "int math() { return 1 + 2 * 3; }\n"
        "int branch() { if (0) return 1; return 2; }\n"
        "int loop() { while (0) return 1; return 2; }\n"
        "int unreachable() { return 1; return 2; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("MOV R0, #7") != std::string::npos);
    CHECK(result.assembly.find("MUL R0") == std::string::npos);
    CHECK(result.assembly.find("ifelse") == std::string::npos);
    CHECK(result.assembly.find(".Lwhile") == std::string::npos);
}

TEST_CASE("local scalar reads load directly") {
    const cxx::Result result = cxx::compile(
        "int read() { int value = 3; return value; }\n"
        "int assign(int value) { int result = 0; result = value + 2; return result; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("LOAD R0, [R7 + 0]") != std::string::npos);
    CHECK(result.assembly.find("STORE R0, [R7 + 4]") != std::string::npos);
}

TEST_CASE("comparison conditions branch directly") {
    const cxx::Result result = cxx::compile(
        "int cond(int x) { if (x == 1) return 1; return 2; }\n"
        "int d(int x) { do { x = x - 1; } while (x > 0); return x; }\n"
        "int t(int x) { return x == 1 ? 10 : 20; }\n"
        "int add(int a, int b) { return a + b; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("CMP R0, #1") != std::string::npos);
    CHECK(result.assembly.find("JNZ .Lifelse") != std::string::npos);
    CHECK(result.assembly.find("cmpend") == std::string::npos);
    CHECK(result.assembly.find("JGT .Ldo_") != std::string::npos);
    CHECK(result.assembly.find("JNZ .Lelse") != std::string::npos);
    CHECK(result.assembly.find("STORE R0, [R7 + 8]") == std::string::npos);
}

TEST_CASE("zero-argument member calls avoid this temporaries") {
    const cxx::Result result = cxx::compile(
        "struct Box { int value; int get() { return value; } int add(int x) { return value + x; } };\n"
        "int f(Box* box) { return box->get(); }\n"
        "int g(Box* box, int y) { return box->add(y); }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("STORE R0, [R7 + 4]") == std::string::npos);
    CHECK(result.assembly.find("CALL _ZN3Box3getEv") != std::string::npos);
}

TEST_CASE("simple calls avoid argument temporaries") {
    const cxx::Result result = cxx::compile(
        "int add(int a, int b) { return a + b; }\n"
        "int call(int x, int y) { return add(x, y); }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("STORE R0, [R7 + 8]") == std::string::npos);
    CHECK(result.assembly.find("CALL _Z3addii") != std::string::npos);
}

TEST_CASE("trivial functions inline at O1") {
    cxx::Options options;
    options.optimize = true;
    const cxx::Result result = cxx::compile(
        "struct N { int h; N* l; N* r; };\n"
        "int heightOf(N* n) { return n == 0 ? 0 : n->h; }\n"
        "int maximum(int a, int b) { return a > b ? a : b; }\n"
        "int f(N* n) { return maximum(heightOf(n->l), heightOf(n->r)); }\n"
        "struct Box { int value; int get() { return value; } };\n"
        "int g(Box* b) { return b->get(); }\n",
        options);
    CHECK(result.ok);
    CHECK(result.assembly.find("CALL _Z8heightOf") == std::string::npos);
    CHECK(result.assembly.find("CALL _Z7maximum") == std::string::npos);
    CHECK(result.assembly.find("CALL _ZN3Box3getEv") == std::string::npos);
}

TEST_CASE("local compound assignments stay in registers") {
    const cxx::Result result = cxx::compile(
        "int addto(int a, int b) { a += b; return a; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("ADD R0, R0, R2") != std::string::npos);
    CHECK(result.assembly.find("LEA R1, [R7 + 0]") == std::string::npos);
}

TEST_CASE("local increments store directly") {
    const cxx::Result result = cxx::compile(
        "int inc(int value) { ++value; return value; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("ADD R0, R0, #1") != std::string::npos);
    CHECK(result.assembly.find("LEA R1, [R7 + 0]") == std::string::npos);
}

TEST_CASE("scalar returns avoid spills") {
    const cxx::Result result = cxx::compile(
        "int ret() { int value = 2; return value; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("STORE R0, [R7 + 4]") == std::string::npos);
}

TEST_CASE("immediate comparisons avoid spills") {
    const cxx::Result result = cxx::compile(
        "int is_one(int value) { return value == 1; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("CMP R0, #1") != std::string::npos);
}

TEST_CASE("optimized programs preserve behavior") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "int add(int left, int right) { return left + right; }\n"
        "int main() {\n"
        "    int total = 0;\n"
        "    for (int index = 0; index < 5; ++index) total = total + index;\n"
        "    if (total != 10) return 1;\n"
        "    total = add(total, 5);\n"
        "    if (total != 15) return 2;\n"
        "    int value = 3;\n"
        "    int* pointer = &value;\n"
        "    *pointer = 7;\n"
        "    if (value != 7) return 3;\n"
        "    std::vector<int> values;\n"
        "    values.push_back(2);\n"
        "    values.push_back(4);\n"
        "    return values[0] + values[1] == 6 ? 0 : 4;\n"
        "}\n",
        true);
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("optimized assembly folds immediate compares") {
    cxx::Options options;
    options.optimize = true;
    const cxx::Result result = cxx::compile(
        "int is_one(int value) { if (value == 1) return 1; return 0; }\n",
        options);
    CHECK(result.ok);
    CHECK(result.assembly.find("CMP R0, #1") != std::string::npos);
    CHECK(result.assembly.find("MOV R4, #1") == std::string::npos);
}

TEST_CASE("call diagnostics name the target") {
    const cxx::Result result =
        cxx::compile("int main() { missing(1); return 0; }\n");
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("missing") !=
          std::string::npos);
}

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

TEST_CASE("function statics call non-constant initializers once") {
    const ProgramResult program = compile_and_run(
        "int calls = 0;\n"
        "int initialize() { calls = calls + 1; return 7; }\n"
        "int get() { static int value = initialize(); return value; }\n"
        "int main() {\n"
        "    if (get() != 7) return 1;\n"
        "    if (get() != 7) return 2;\n"
        "    if (calls != 1) return 3;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function-local class statics initialize once") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int check() {\n"
        "    static std::string text(\"hello\");\n"
        "    text.push_back('!');\n"
        "    return text.size();\n"
        "}\n"
        "int main() {\n"
        "    if (check() != 6) return 1;\n"
        "    if (check() != 7) return 2;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function statics support scalar widths") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    static double real = 2.5;\n"
        "    static char letter = 'x';\n"
        "    static long long big = 1234567890123LL;\n"
        "    if (real != 2.5) return 1;\n"
        "    if (letter != 'x') return 2;\n"
        "    if (big != 1234567890123LL) return 3;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("scalar function statics initialize once") {
    const ProgramResult program = compile_and_run(
        "int next() {\n"
        "    static int value = 40;\n"
        "    value = value + 1;\n"
        "    return value;\n"
        "}\n"
        "int main() {\n"
        "    if (next() != 41) return 1;\n"
        "    if (next() != 42) return 2;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string counted character-set searches") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello world\");\n"
        "    if (text.find_first_of(\"XXoYY\", 4, 5) != 4) return 1;\n"
        "    if (text.find_last_of(\"XXoYY\", 9, 5) != 7) return 2;\n"
        "    if (text.find_first_not_of(\"XXhYY\", 0, 5) != 1) return 3;\n"
        "    if (text.find_last_not_of(\"XXdYY\", 10, 5) != 9) return 4;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string reverse-finds counted C-string ranges") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello world world\");\n"
        "    if (text.rfind(\"worldXX\", 16, 5) != 12) return 1;\n"
        "    if (text.rfind(\"worldXX\", 11, 5) != 6) return 2;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string finds counted C-string ranges") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello world\");\n"
        "    if (text.find(\"worldYY\", 0, 5) != 6) return 1;\n"
        "    if (text.find(\"worldYY\", 7, 5) != -1) return 2;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string replaces substrings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"abXXef\");\n"
        "    std::string part(\"yycdzz\");\n"
        "    text.replace(2, 2, part, 2, 2);\n"
        "    if (text != \"abcdef\") return 1;\n"
        "    text.replace(0, 2, \"QQabRR\", 2, 2);\n"
        "    return text == \"abcdef\" ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string inserts substrings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"abef\");\n"
        "    std::string part(\"xxcdyy\");\n"
        "    text.insert(2, part, 2, 2);\n"
        "    if (text != \"abcdef\") return 1;\n"
        "    text.insert(6, \"QQghRR\", 2, 2);\n"
        "    return text == \"abcdefgh\" ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm pushes heap elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 5, 3, 4, 1, 9 };\n"
        "    std::make_heap(values, values + 4);\n"
        "    std::push_heap(values, values + 5);\n"
        "    if (!std::is_heap(values, values + 5)) return 1;\n"
        "    if (values[0] != 9) return 2;\n"
        "    char text[3] = \"ba\";\n"
        "    std::make_heap(text, text + 2);\n"
        "    text[2] = 'c';\n"
        "    std::push_heap(text, text + 3);\n"
        "    if (text[0] != 'c') return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    std::make_heap(reals, reals + 2);\n"
        "    std::push_heap(reals, reals + 3);\n"
        "    return std::is_heap(reals, reals + 3) && reals[0] == 3.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm sorts heaps") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 3, 1, 4, 1, 5 };\n"
        "    std::make_heap(values, values + 5);\n"
        "    std::sort_heap(values, values + 5);\n"
        "    if (values[0] != 1 || values[1] != 1 || values[4] != 5) return 1;\n"
        "    char text[3] = \"cab\";\n"
        "    std::make_heap(text, text + 3);\n"
        "    std::sort_heap(text, text + 3);\n"
        "    if (text[0] != 'a' || text[2] != 'c') return 2;\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    std::make_heap(reals, reals + 3);\n"
        "    std::sort_heap(reals, reals + 3);\n"
        "    return reals[0] == 1.5 && reals[2] == 3.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm builds heaps") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 3, 1, 4, 1, 5 };\n"
        "    std::make_heap(values, values + 5);\n"
        "    if (!std::is_heap(values, values + 5)) return 1;\n"
        "    if (values[0] != 5) return 2;\n"
        "    char text[3] = \"cab\";\n"
        "    std::make_heap(text, text + 3);\n"
        "    if (!std::is_heap(text, text + 3)) return 3;\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    std::make_heap(reals, reals + 3);\n"
        "    return std::is_heap(reals, reals + 3) ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm reports heap prefixes") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 9, 7, 3, 8, 4 };\n"
        "    if (std::is_heap_until(values, values + 5) != values + 3) return 1;\n"
        "    char text[4] = \"zyxw\";\n"
        "    if (std::is_heap_until(text, text + 4) != text + 4) return 2;\n"
        "    char broken[4] = \"ayzw\";\n"
        "    if (std::is_heap_until(broken, broken + 4) != broken + 1) return 3;\n"
        "    double reals[3] = { 3.5, 1.5, 2.5 };\n"
        "    if (std::is_heap_until(reals, reals + 3) != reals + 3) return 4;\n"
        "    int empty[1] = { 0 };\n"
        "    return std::is_heap_until(empty, empty) == empty ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm detects heaps") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int heap[5] = { 9, 7, 8, 3, 4 };\n"
        "    if (!std::is_heap(heap, heap + 5)) return 1;\n"
        "    int broken[3] = { 1, 9, 8 };\n"
        "    if (std::is_heap(broken, broken + 3)) return 2;\n"
        "    char text[4] = \"zyxw\";\n"
        "    if (!std::is_heap(text, text + 4)) return 3;\n"
        "    double reals[3] = { 3.5, 1.5, 2.5 };\n"
        "    if (!std::is_heap(reals, reals + 3)) return 4;\n"
        "    int empty[1] = { 0 };\n"
        "    return std::is_heap(empty, empty) ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm detects permutations") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[4] = { 1, 2, 3, 4 };\n"
        "    int right[4] = { 4, 3, 2, 1 };\n"
        "    if (!std::is_permutation(left, left + 4, right)) return 1;\n"
        "    int other[4] = { 1, 2, 3, 5 };\n"
        "    if (std::is_permutation(left, left + 4, other)) return 2;\n"
        "    char text[5] = \"aabc\";\n"
        "    char letters[5] = \"cbaa\";\n"
        "    if (!std::is_permutation(text, text + 4, letters)) return 3;\n"
        "    char mismatch[5] = \"abcc\";\n"
        "    if (std::is_permutation(text, text + 4, mismatch)) return 4;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 2.5, 1.5 };\n"
        "    return std::is_permutation(x, x + 2, y) ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm retreats permutations") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[3] = { 1, 3, 2 };\n"
        "    if (!std::prev_permutation(values, values + 3)) return 1;\n"
        "    if (values[0] != 1 || values[1] != 2 || values[2] != 3) return 2;\n"
        "    int ascending[3] = { 1, 2, 3 };\n"
        "    if (std::prev_permutation(ascending, ascending + 3)) return 3;\n"
        "    if (ascending[0] != 3 || ascending[2] != 1) return 4;\n"
        "    char text[4] = \"acb\";\n"
        "    if (!std::prev_permutation(text, text + 3)) return 5;\n"
        "    if (text[1] != 'b' || text[2] != 'c') return 6;\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    std::prev_permutation(reals, reals + 3);\n"
        "    return reals[1] == 2.5 && reals[2] == 3.5 ? 0 : 7;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm advances permutations") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[3] = { 1, 2, 3 };\n"
        "    if (!std::next_permutation(values, values + 3)) return 1;\n"
        "    if (values[0] != 1 || values[1] != 3 || values[2] != 2) return 2;\n"
        "    int descending[3] = { 3, 2, 1 };\n"
        "    if (std::next_permutation(descending, descending + 3)) return 3;\n"
        "    if (descending[0] != 1 || descending[2] != 3) return 4;\n"
        "    char text[4] = \"abc\";\n"
        "    if (!std::next_permutation(text, text + 3)) return 5;\n"
        "    if (text[1] != 'c') return 6;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    std::next_permutation(reals, reals + 3);\n"
        "    return reals[1] == 3.5 ? 0 : 7;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm swaps individual elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[2] = { 1, 2 };\n"
        "    std::iter_swap(values, values + 1);\n"
        "    if (values[0] != 2 || values[1] != 1) return 1;\n"
        "    char text[3] = \"ab\";\n"
        "    std::iter_swap(text, text + 1);\n"
        "    if (text[0] != 'b' || text[1] != 'a') return 2;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    std::iter_swap(reals, reals + 1);\n"
        "    return reals[0] == 2.5 && reals[1] == 1.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies ranges backward") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 3 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::copy_backward(source, source + 3, target + 3);\n"
        "    if (end != target) return 1;\n"
        "    if (target[0] != 1 || target[2] != 3) return 2;\n"
        "    char text[6] = \"abc\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::copy_backward(text, text + 3, letters + 5) != letters + 2) return 3;\n"
        "    if (letters[2] != 'a' || letters[4] != 'c') return 4;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    double copied[2] = { 0.0, 0.0 };\n"
        "    std::copy_backward(reals, reals + 2, copied + 2);\n"
        "    if (copied[0] != 1.5 || copied[1] != 2.5) return 5;\n"
        "    int values[4] = { 1, 2, 3, 0 };\n"
        "    std::copy_backward(values, values + 3, values + 4);\n"
        "    return values[1] == 1 && values[3] == 3 ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies ranges without values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[5] = { 1, 2, 1, 3, 1 };\n"
        "    int target[5] = { 0, 0, 0, 0, 0 };\n"
        "    int* end = std::remove_copy(source, source + 5, target, 1);\n"
        "    if (end != target + 2) return 1;\n"
        "    if (target[0] != 2 || target[1] != 3) return 2;\n"
        "    char text[6] = \"abaca\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::remove_copy(text, text + 5, letters, 'a') != letters + 2) return 3;\n"
        "    if (letters[0] != 'b' || letters[1] != 'c') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    double copied[3] = { 0.0, 0.0, 0.0 };\n"
        "    return std::remove_copy(reals, reals + 3, copied, 1.5) == copied + 1 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies unique values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[6] = { 1, 1, 2, 3, 3, 4 };\n"
        "    int target[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    int* end = std::unique_copy(source, source + 6, target);\n"
        "    if (end != target + 4) return 1;\n"
        "    if (target[0] != 1 || target[1] != 2 || target[2] != 3 || target[3] != 4) return 2;\n"
        "    char text[7] = \"aabbcc\";\n"
        "    char letters[7] = { 0, 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::unique_copy(text, text + 6, letters) != letters + 3) return 3;\n"
        "    if (letters[0] != 'a' || letters[2] != 'c') return 4;\n"
        "    double reals[4] = { 1.5, 1.5, 2.5, 2.5 };\n"
        "    double copied[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::unique_copy(reals, reals + 4, copied) == copied + 2 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies ranges with replacement") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 1 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::replace_copy(source, source + 3, target, 1, 9);\n"
        "    if (end != target + 3) return 1;\n"
        "    if (target[0] != 9 || target[1] != 2 || target[2] != 9) return 2;\n"
        "    char text[5] = \"abaca\";\n"
        "    char letters[5] = { 0, 0, 0, 0, 0 };\n"
        "    if (std::replace_copy(text, text + 5, letters, 'a', 'z') != letters + 5) return 3;\n"
        "    if (letters[0] != 'z' || letters[1] != 'b' || letters[4] != 'z') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    double copied[3] = { 0.0, 0.0, 0.0 };\n"
        "    std::replace_copy(reals, reals + 3, copied, 1.5, 9.0);\n"
        "    return copied[0] == 9.0 && copied[1] == 2.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies rotated ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[5] = { 1, 2, 3, 4, 5 };\n"
        "    int target[5] = { 0, 0, 0, 0, 0 };\n"
        "    int* end = std::rotate_copy(source, source + 2, source + 5, target);\n"
        "    if (end != target + 5) return 1;\n"
        "    if (target[0] != 3 || target[2] != 5 || target[3] != 1) return 2;\n"
        "    char text[5] = \"abcde\";\n"
        "    char letters[5] = { 0, 0, 0, 0, 0 };\n"
        "    if (std::rotate_copy(text, text + 1, text + 5, letters) != letters + 5) return 3;\n"
        "    if (letters[0] != 'b' || letters[4] != 'a') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    double copied[3] = { 0.0, 0.0, 0.0 };\n"
        "    std::rotate_copy(reals, reals + 2, reals + 3, copied);\n"
        "    return copied[0] == 3.5 && copied[1] == 1.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copies ranges in reverse") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 3 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::reverse_copy(source, source + 3, target);\n"
        "    if (end != target + 3) return 1;\n"
        "    if (target[0] != 3 || target[2] != 1) return 2;\n"
        "    char text[4] = \"abc\";\n"
        "    char letters[4] = { 0, 0, 0, 0 };\n"
        "    if (std::reverse_copy(text, text + 3, letters) != letters + 3) return 3;\n"
        "    if (letters[0] != 'c' || letters[2] != 'a') return 4;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    double copied[2] = { 0.0, 0.0 };\n"
        "    std::reverse_copy(reals, reals + 2, copied);\n"
        "    return copied[0] == 2.5 && copied[1] == 1.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm orders partial and nth elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 5, 1, 4, 2, 3 };\n"
        "    std::nth_element(values, values + 2, values + 5);\n"
        "    if (values[2] != 3) return 1;\n"
        "    int sorted[5] = { 5, 1, 4, 2, 3 };\n"
        "    std::partial_sort(sorted, sorted + 3, sorted + 5);\n"
        "    if (sorted[0] != 1 || sorted[1] != 2 || sorted[2] != 3) return 2;\n"
        "    char text[4] = \"dcba\";\n"
        "    std::nth_element(text, text + 2, text + 4);\n"
        "    if (text[2] != 'c') return 3;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    std::nth_element(reals, reals + 1, reals + 3);\n"
        "    return reals[1] == 1.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm stable sorts arrays") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 3, 1, 4, 2 };\n"
        "    std::stable_sort(values, values + 4);\n"
        "    if (values[0] != 1 || values[3] != 4) return 1;\n"
        "    char text[5] = \"dcba\";\n"
        "    std::stable_sort(text, text + 4);\n"
        "    if (text[0] != 'a' || text[3] != 'd') return 2;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    std::stable_sort(reals, reals + 3);\n"
        "    return reals[0] == 0.5 && reals[2] == 2.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm merges ranges in place") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[6] = { 1, 3, 5, 2, 4, 6 };\n"
        "    std::inplace_merge(values, values + 3, values + 6);\n"
        "    if (values[0] != 1 || values[1] != 2 || values[2] != 3 || values[5] != 6) return 1;\n"
        "    char text[6] = \"acebdf\";\n"
        "    std::inplace_merge(text, text + 3, text + 6);\n"
        "    if (text[0] != 'a' || text[1] != 'b' || text[5] != 'f') return 2;\n"
        "    double reals[4] = { 1.5, 3.5, 2.5, 4.5 };\n"
        "    std::inplace_merge(reals, reals + 2, reals + 4);\n"
        "    return reals[0] == 1.5 && reals[1] == 2.5 && reals[3] == 4.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm merges sorted ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 4, 6 };\n"
        "    int output[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    int* end = std::merge(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 6) return 1;\n"
        "    if (output[0] != 1 || output[1] != 2 || output[2] != 3 || output[5] != 6) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bdf\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::merge(a, a + 3, b, b + 3, letters) != letters + 6) return 3;\n"
        "    if (letters[0] != 'a' || letters[1] != 'b' || letters[5] != 'f') return 4;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 4.5 };\n"
        "    double z[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::merge(x, x + 2, y, y + 2, z) == z + 4 && z[1] == 2.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes symmetric difference") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 3, 6 };\n"
        "    int output[4] = { 0, 0, 0, 0 };\n"
        "    int* end = std::set_symmetric_difference(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 4) return 1;\n"
        "    if (output[0] != 1 || output[1] != 2 || output[2] != 5 || output[3] != 6) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::set_symmetric_difference(a, a + 3, b, b + 3, letters) != letters + 4) return 3;\n"
        "    if (letters[0] != 'a' || letters[1] != 'b' || letters[2] != 'd' || letters[3] != 'e') return 4;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 3.5 };\n"
        "    double z[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::set_symmetric_difference(x, x + 2, y, y + 2, z) == z + 2 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes sorted difference") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[2] = { 3, 6 };\n"
        "    int output[3] = { 0, 0, 0 };\n"
        "    int* end = std::set_difference(left, left + 3, right, right + 2, output);\n"
        "    if (end != output + 2) return 1;\n"
        "    if (output[0] != 1 || output[1] != 5) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[3] = { 0, 0, 0 };\n"
        "    if (std::set_difference(a, a + 3, b, b + 3, letters) != letters + 2) return 3;\n"
        "    if (letters[0] != 'a' || letters[1] != 'e') return 4;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 1.5, 3.5 };\n"
        "    double z[2] = { 0.0, 0.0 };\n"
        "    return std::set_difference(x, x + 2, y, y + 2, z) == z + 1 && z[0] == 2.5 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes sorted intersection") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 3, 6 };\n"
        "    int output[3] = { 0, 0, 0 };\n"
        "    int* end = std::set_intersection(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 1 || output[0] != 3) return 1;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[3] = { 0, 0, 0 };\n"
        "    if (std::set_intersection(a, a + 3, b, b + 3, letters) != letters + 1) return 2;\n"
        "    if (letters[0] != 'c') return 3;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 3.5 };\n"
        "    double z[2] = { 0.0, 0.0 };\n"
        "    return std::set_intersection(x, x + 2, y, y + 2, z) == z + 1 && z[0] == 3.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm computes sorted union") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 3, 5 };\n"
        "    int right[3] = { 2, 3, 6 };\n"
        "    int output[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    int* end = std::set_union(left, left + 3, right, right + 3, output);\n"
        "    if (end != output + 5) return 1;\n"
        "    if (output[0] != 1 || output[1] != 2 || output[2] != 3 || output[3] != 5 || output[4] != 6) return 2;\n"
        "    char a[3] = \"ace\";\n"
        "    char b[3] = \"bcd\";\n"
        "    char letters[6] = { 0, 0, 0, 0, 0, 0 };\n"
        "    if (std::set_union(a, a + 3, b, b + 3, letters) != letters + 5) return 3;\n"
        "    if (letters[0] != 'a' || letters[2] != 'c' || letters[4] != 'e') return 4;\n"
        "    double x[2] = { 1.5, 3.5 };\n"
        "    double y[2] = { 2.5, 3.5 };\n"
        "    double z[4] = { 0.0, 0.0, 0.0, 0.0 };\n"
        "    return std::set_union(x, x + 2, y, y + 2, z) == z + 3 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm checks sorted inclusion") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int big[5] = { 1, 2, 3, 4, 5 };\n"
        "    int small[3] = { 2, 3, 5 };\n"
        "    if (!std::includes(big, big + 5, small, small + 3)) return 1;\n"
        "    int missing[2] = { 2, 6 };\n"
        "    if (std::includes(big, big + 5, missing, missing + 2)) return 2;\n"
        "    char text[7] = \"abcdef\";\n"
        "    char part[4] = \"bde\";\n"
        "    if (!std::includes(text, text + 6, part, part + 3)) return 3;\n"
        "    double all[4] = { 1.5, 2.5, 3.5, 4.5 };\n"
        "    double some[2] = { 2.5, 4.5 };\n"
        "    if (!std::includes(all, all + 4, some, some + 2)) return 4;\n"
        "    return std::includes(text, text + 6, part, part) ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm compares ranges lexicographically") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    char left[3] = \"abc\";\n"
        "    char right[3] = \"abd\";\n"
        "    if (!std::lexicographical_compare(left, left + 3, right, right + 3)) return 1;\n"
        "    if (std::lexicographical_compare(right, right + 3, left, left + 3)) return 2;\n"
        "    if (std::lexicographical_compare(left, left + 3, left, left + 3)) return 3;\n"
        "    int shorter[2] = { 1, 2 };\n"
        "    int longer[3] = { 1, 2, 3 };\n"
        "    if (!std::lexicographical_compare(shorter, shorter + 2, longer, longer + 3)) return 4;\n"
        "    double a[2] = { 1.5, 2.5 };\n"
        "    double b[2] = { 1.5, 3.5 };\n"
        "    return std::lexicographical_compare(a, a + 2, b, b + 2) ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds first matching element") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int text[6] = { 1, 2, 3, 4, 5, 6 };\n"
        "    int set[2] = { 4, 6 };\n"
        "    if (std::find_first_of(text, text + 6, set, set + 2) != text + 3) return 1;\n"
        "    int missing[2] = { 8, 9 };\n"
        "    if (std::find_first_of(text, text + 6, missing, missing + 2) != text + 6) return 2;\n"
        "    char letters[7] = \"abcdef\";\n"
        "    char chars[3] = \"xd\";\n"
        "    if (std::find_first_of(letters, letters + 6, chars, chars + 2) != letters + 3) return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    double wanted[2] = { 9.0, 2.5 };\n"
        "    if (std::find_first_of(reals, reals + 3, wanted, wanted + 2) != reals + 1) return 4;\n"
        "    return std::find_first_of(letters, letters + 6, chars, chars) == letters + 6 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm reports sorted prefixes") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 4, 3, 5 };\n"
        "    if (std::is_sorted_until(values, values + 5) != values + 3) return 1;\n"
        "    char text[5] = \"abdc\";\n"
        "    if (std::is_sorted_until(text, text + 4) != text + 3) return 2;\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    if (std::is_sorted_until(reals, reals + 3) != reals + 2) return 3;\n"
        "    int sorted[3] = { 1, 2, 3 };\n"
        "    if (std::is_sorted_until(sorted, sorted + 3) != sorted + 3) return 4;\n"
        "    return std::is_sorted_until(sorted, sorted) == sorted ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm detects sorted ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int sorted[4] = { 1, 2, 3, 4 };\n"
        "    int unsorted[4] = { 1, 3, 2, 4 };\n"
        "    if (!std::is_sorted(sorted, sorted + 4)) return 1;\n"
        "    if (std::is_sorted(unsorted, unsorted + 4)) return 2;\n"
        "    char text[5] = \"abcd\";\n"
        "    if (!std::is_sorted(text, text + 4)) return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    if (!std::is_sorted(reals, reals + 3)) return 4;\n"
        "    int equal[3] = { 2, 2, 2 };\n"
        "    if (!std::is_sorted(equal, equal + 3)) return 5;\n"
        "    return std::is_sorted(text, text) ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm equal compares two bounded ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 2, 3 };\n"
        "    int right[3] = { 1, 2, 3 };\n"
        "    if (!std::equal(left, left + 3, right, right + 3)) return 1;\n"
        "    int shorter[2] = { 1, 2 };\n"
        "    if (std::equal(left, left + 3, shorter, shorter + 2)) return 2;\n"
        "    char a[3] = \"ab\";\n"
        "    char b[2] = \"ab\";\n"
        "    if (!std::equal(a, a + 2, b, b + 2)) return 3;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 1.5, 2.5 };\n"
        "    return std::equal(x, x + 2, y, y + 2) ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds the last subsequence") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int text[7] = { 1, 2, 3, 1, 2, 3, 4 };\n"
        "    int needle[2] = { 1, 2 };\n"
        "    if (std::find_end(text, text + 7, needle, needle + 2) != text + 3) return 1;\n"
        "    char letters[8] = \"abcabcx\";\n"
        "    char part[3] = \"abc\";\n"
        "    if (std::find_end(letters, letters + 7, part, part + 3) != letters + 3) return 2;\n"
        "    char missing[2] = \"zz\";\n"
        "    if (std::find_end(letters, letters + 7, missing, missing + 2) != letters + 7) return 3;\n"
        "    double reals[4] = { 1.5, 2.5, 1.5, 2.5 };\n"
        "    double pair[2] = { 1.5, 2.5 };\n"
        "    if (std::find_end(reals, reals + 4, pair, pair + 2) != reals + 2) return 4;\n"
        "    return std::find_end(letters, letters + 7, part, part) == letters + 7 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm searches repeated values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[6] = { 1, 2, 2, 2, 3, 4 };\n"
        "    if (std::search_n(values, values + 6, 3, 2) != values + 1) return 1;\n"
        "    if (std::search_n(values, values + 6, 4, 2) != values + 6) return 2;\n"
        "    char text[8] = \"abbcccd\";\n"
        "    if (std::search_n(text, text + 7, 3, 'c') != text + 3) return 3;\n"
        "    double reals[4] = { 1.5, 2.5, 2.5, 3.5 };\n"
        "    if (std::search_n(reals, reals + 4, 2, 2.5) != reals + 1) return 4;\n"
        "    return std::search_n(text, text + 7, 0, 'z') == text ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds adjacent duplicates") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 2, 3, 3 };\n"
        "    if (std::adjacent_find(values, values + 5) != values + 1) return 1;\n"
        "    char text[6] = \"abcde\";\n"
        "    if (std::adjacent_find(text, text + 5) != text + 5) return 2;\n"
        "    char doubled[6] = \"abbcd\";\n"
        "    if (std::adjacent_find(doubled, doubled + 5) != doubled + 1) return 3;\n"
        "    double reals[3] = { 1.5, 2.5, 2.5 };\n"
        "    if (std::adjacent_find(reals, reals + 3) != reals + 1) return 4;\n"
        "    return std::adjacent_find(text, text) == text ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm searches subsequences") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int text[6] = { 1, 2, 3, 4, 5, 6 };\n"
        "    int needle[2] = { 3, 4 };\n"
        "    if (std::search(text, text + 6, needle, needle + 2) != text + 2) return 1;\n"
        "    int missing[2] = { 8, 9 };\n"
        "    if (std::search(text, text + 6, missing, missing + 2) != text + 6) return 2;\n"
        "    char letters[7] = \"abcdef\";\n"
        "    char pair[2] = \"cd\";\n"
        "    if (std::search(letters, letters + 6, pair, pair + 2) != letters + 2) return 3;\n"
        "    double reals[4] = { 1.5, 2.5, 3.5, 4.5 };\n"
        "    double tail[2] = { 3.5, 4.5 };\n"
        "    if (std::search(reals, reals + 4, tail, tail + 2) != reals + 2) return 4;\n"
        "    return std::search(letters, letters + 6, pair, pair) == letters ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm rotates ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 3, 4, 5 };\n"
        "    int* middle = std::rotate(values, values + 2, values + 5);\n"
        "    if (middle != values + 3) return 1;\n"
        "    if (values[0] != 3 || values[2] != 5 || values[3] != 1) return 2;\n"
        "    char text[5] = \"abcde\";\n"
        "    if (std::rotate(text, text + 1, text + 5) != text + 4) return 3;\n"
        "    if (text[0] != 'b' || text[4] != 'a') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    std::rotate(reals, reals + 2, reals + 3);\n"
        "    if (reals[0] != 3.5 || reals[1] != 1.5) return 5;\n"
        "    if (std::rotate(values, values, values + 3) != values + 3) return 6;\n"
        "    return std::rotate(values, values + 3, values + 3) == values ? 0 : 7;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm swaps ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 2, 3 };\n"
        "    int right[3] = { 4, 5, 6 };\n"
        "    int* end = std::swap_ranges(left, left + 3, right);\n"
        "    if (end != right + 3) return 1;\n"
        "    if (left[0] != 4 || right[2] != 3) return 2;\n"
        "    char a[3] = \"abc\";\n"
        "    char b[3] = \"xyz\";\n"
        "    std::swap_ranges(a, a + 3, b);\n"
        "    if (a[0] != 'x' || b[2] != 'c') return 3;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 3.5, 4.5 };\n"
        "    std::swap_ranges(x, x + 2, y);\n"
        "    return x[0] == 3.5 && y[1] == 2.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm unique compacts duplicates") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[6] = { 1, 1, 2, 3, 3, 3 };\n"
        "    int* end = std::unique(values, values + 6);\n"
        "    if (end != values + 3) return 1;\n"
        "    if (values[0] != 1 || values[1] != 2 || values[2] != 3) return 2;\n"
        "    char text[7] = \"aabbcc\";\n"
        "    char* tail = std::unique(text, text + 6);\n"
        "    if (tail != text + 3) return 3;\n"
        "    if (text[0] != 'a' || text[1] != 'b' || text[2] != 'c') return 4;\n"
        "    double reals[4] = { 1.5, 1.5, 2.5, 2.5 };\n"
        "    if (std::unique(reals, reals + 4) != reals + 2) return 5;\n"
        "    return std::unique(values, values) == values ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm binary searches sorted ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 3, 3, 7, 9 };\n"
        "    if (!std::binary_search(values, values + 5, 3)) return 1;\n"
        "    if (std::binary_search(values, values + 5, 4)) return 2;\n"
        "    if (std::lower_bound(values, values + 5, 3) != values + 1) return 3;\n"
        "    if (std::upper_bound(values, values + 5, 3) != values + 3) return 4;\n"
        "    char text[5] = \"acegi\";\n"
        "    if (!std::binary_search(text, text + 5, 'e')) return 5;\n"
        "    double reals[4] = { 0.5, 1.5, 2.5, 3.5 };\n"
        "    return std::binary_search(reals, reals + 4, 2.5) ? 0 : 6;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("template value initialization") {
    const ProgramResult program = compile_and_run(
        "#include <utility>\n"
        "template<typename T> T make_value() { return T(); }\n"
        "int main() {\n"
        "    if (make_value<int>() != 0) return 1;\n"
        "    if (make_value<double>() != 0.0) return 2;\n"
        "    std::pair<int, int> pair = make_value<std::pair<int, int> >();\n"
        "    return pair.first == 0 && pair.second == 0 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("indirect calls pass floating point arguments") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "double scale(int factor, double value) { return value * factor; }\n"
        "int main() {\n"
        "    double reals[3] = { 1.5, 3.5, 2.5 };\n"
        "    std::sort(reals, reals + 3,\n"
        "              [](double left, double right) { return right < left; });\n"
        "    if (reals[0] != 3.5 || reals[2] != 1.5) return 1;\n"
        "    double (*operation)(int, double) = scale;\n"
        "    if (operation(3, 1.5) != 4.5) return 2;\n"
        "    double (*square)(double) = [](double value) { return value * value; };\n"
        "    return square(2.5) == 6.25 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("class methods bind const reference temporaries") {
    const ProgramResult program = compile_and_run(
        "struct Accumulator {\n"
        "    int add(const int& value) { return value + 1; }\n"
        "    double scale(const double& value) { return value * 2.0; }\n"
        "};\n"
        "template<typename T> struct Holder {\n"
        "    T echo(const T& value) const { return value; }\n"
        "};\n"
        "int main() {\n"
        "    Accumulator accumulator;\n"
        "    if (accumulator.add(3) != 4) return 1;\n"
        "    if (accumulator.scale(1.5) != 3.0) return 2;\n"
        "    Holder<int> holder;\n"
        "    return holder.echo(5) == 5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("class methods accept string temporaries") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "struct Reader {\n"
        "    int length(const std::string& text) const { return text.size(); }\n"
        "};\n"
        "int main() {\n"
        "    Reader reader;\n"
        "    return reader.length(\"hello\") == 5 ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std map inserts updates and counts") {
    const ProgramResult program = compile_and_run(
        "#include <map>\n"
        "int main() {\n"
        "    std::map<int, int> values;\n"
        "    values[3] = 30;\n"
        "    values[1] = 10;\n"
        "    values[3] = 33;\n"
        "    if (values.size() != 2) return 1;\n"
        "    if (values[1] != 10 || values[3] != 33) return 2;\n"
        "    if (values.count(3) != 1 || values.count(2) != 0) return 3;\n"
        "    values[2] = 20;\n"
        "    if (values.size() != 3) return 4;\n"
        "    values.clear();\n"
        "    return values.empty() ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector copies and assigns") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::vector<int> first;\n"
        "    first.push_back(1);\n"
        "    first.push_back(2);\n"
        "    std::vector<int> second = first;\n"
        "    if (second.size() != 2 || second[1] != 2) return 1;\n"
        "    std::vector<int> third;\n"
        "    third = first;\n"
        "    if (third.size() != 2 || third[0] != 1) return 2;\n"
        "    third.push_back(3);\n"
        "    if (third.size() != 3 || second.size() != 2) return 3;\n"
        "    std::vector<std::pair<int, int> > pairs;\n"
        "    std::pair<int, int> value;\n"
        "    value.first = 4;\n"
        "    value.second = 5;\n"
        "    pairs.push_back(value);\n"
        "    std::vector<std::pair<int, int> > copied = pairs;\n"
        "    return copied[0].first == 4 && copied[0].second == 5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector resize preserves and zero fills") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::vector<int> values;\n"
        "    values.push_back(4);\n"
        "    values.resize(3);\n"
        "    if (values.size() != 3 || values[0] != 4) return 1;\n"
        "    if (values[2] != 0) return 2;\n"
        "    values.resize(1);\n"
        "    if (values.size() != 1 || values[0] != 4) return 3;\n"
        "    std::vector<std::pair<int, int> > pairs;\n"
        "    pairs.resize(2);\n"
        "    if (pairs.size() != 2) return 4;\n"
        "    return pairs[1].first == 0 && pairs[1].second == 0 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector resize insert and erase") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "int main() {\n"
        "    std::vector<int> values;\n"
        "    values.push_back(1);\n"
        "    values.push_back(3);\n"
        "    values.insert(1, 2);\n"
        "    if (values.size() != 3 || values[1] != 2) return 1;\n"
        "    values.erase(1);\n"
        "    return values.size() == 2 && values[0] == 1 && values[1] == 3 ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std optional and string view") {
    const ProgramResult program = compile_and_run(
        "#include <optional>\n"
        "#include <string_view>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::optional<int> item;\n"
        "    if (item.has_value() || item) return 1;\n"
        "    item.set(7);\n"
        "    if (!item.has_value() || item.value() != 7) return 2;\n"
        "    if (*item != 7 || item.value_or(3) != 7) return 3;\n"
        "    *item = 9;\n"
        "    if (item.value() != 9) return 4;\n"
        "    item.reset();\n"
        "    if (item.value_or(3) != 3) return 5;\n"
        "    item = 11;\n"
        "    if (item.value() != 11) return 6;\n"
        "    item.emplace();\n"
        "    if (item.value() != 0) return 7;\n"
        "    item.emplace(13);\n"
        "    if (item.value() != 13) return 8;\n"
        "    std::optional<std::pair<int, int> > pair;\n"
        "    std::pair<int, int> value;\n"
        "    value.first = 4;\n"
        "    value.second = 5;\n"
        "    pair = value;\n"
        "    if (pair->first != 4 || pair->second != 5) return 9;\n"
        "    pair.emplace();\n"
        "    if (pair.value().second != 0) return 10;\n"
        "    std::string_view view(\"hello\");\n"
        "    if (view.size() != 5 || view.empty()) return 5;\n"
        "    if (view[0] != 'h' || view.back() != 'o') return 6;\n"
        "    if (!view.starts_with(\"hell\") || !view.starts_with('h')) return 7;\n"
        "    if (!view.ends_with(\"llo\") || !view.ends_with('o')) return 8;\n"
        "    std::string_view tail = view.substr(1, 3);\n"
        "    if (tail.size() != 3 || tail[0] != 'e') return 7;\n"
        "    view.remove_prefix(1);\n"
        "    view.remove_suffix(1);\n"
        "    return view.size() == 3 &&\n"
        "                   view.compare(std::string_view(\"ell\")) == 0 ? 0 : 8;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std vector grows and preserves values") {
    const ProgramResult program = compile_and_run(
        "#include <vector>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::vector<int> values;\n"
        "    values.push_back(3);\n"
        "    values.push_back(5);\n"
        "    values.push_back(7);\n"
        "    if (values.size() != 3 || values.empty()) return 1;\n"
        "    if (values[0] != 3 || values.back() != 7) return 2;\n"
        "    if (values.end() - values.begin() != 3) return 3;\n"
        "    values.pop_back();\n"
        "    if (values.size() != 2 || values.back() != 5) return 4;\n"
        "    values.clear();\n"
        "    if (!values.empty()) return 5;\n"
        "    values.reserve(8);\n"
        "    values.push_back(11);\n"
        "    if (values.capacity() < 8 || values[0] != 11) return 6;\n"
        "    std::vector<double> reals;\n"
        "    reals.push_back(1.5);\n"
        "    reals.push_back(2.5);\n"
        "    if (reals[0] != 1.5 || reals.back() != 2.5) return 7;\n"
        "    std::vector<std::pair<int, int> > pairs;\n"
        "    std::pair<int, int> first;\n"
        "    first.first = 1;\n"
        "    first.second = 2;\n"
        "    pairs.push_back(first);\n"
        "    return pairs[0].first == 1 && pairs[0].second == 2 ? 0 : 8;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std atomic uses ll/sc operations") {
    const ProgramResult program = compile_and_run(
        "#include <atomic>\n"
        "int main() {\n"
        "    std::atomic<int> counter;\n"
        "    counter.store(5);\n"
        "    if (counter.load() != 5) return 1;\n"
        "    counter = 7;\n"
        "    if (counter.load() != 7) return 2;\n"
        "    if (counter.fetch_add(3) != 7) return 3;\n"
        "    if (counter.load() != 10) return 4;\n"
        "    int expected = 10;\n"
        "    if (!counter.compare_exchange_weak(expected, 20)) return 5;\n"
        "    if (counter.load() != 20) return 6;\n"
        "    if (counter.exchange(30) != 20) return 7;\n"
        "    return counter.load() == 30 ? 0 : 8;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("qualified class templates instantiate") {
    const ProgramResult program = compile_and_run(
        "namespace N {\n"
        "template<typename A, typename B> class Box {\n"
        "public:\n"
        "    A first;\n"
        "    B second;\n"
        "};\n"
        "}\n"
        "int main() {\n"
        "    N::Box<int, char> box;\n"
        "    box.first = 7;\n"
        "    box.second = 'x';\n"
        "    return box.first == 7 && box.second == 'x' ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std array and pair containers") {
    const ProgramResult program = compile_and_run(
        "#include <array>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    std::array<int, 3> values;\n"
        "    values[0] = 1;\n"
        "    values[1] = 2;\n"
        "    values[2] = 3;\n"
        "    if (values.size() != 3 || values.front() != 1) return 1;\n"
        "    if (values.back() != 3) return 2;\n"
        "    if (values.end() - values.begin() != 3) return 3;\n"
        "    std::array<double, 2> reals;\n"
        "    reals[0] = 1.5;\n"
        "    reals[1] = 2.5;\n"
        "    if (reals[0] != 1.5 || reals.back() != 2.5) return 4;\n"
        "    std::pair<int, int> pair;\n"
        "    pair.first = 4;\n"
        "    pair.second = 5;\n"
        "    return pair.first + pair.second == 9 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function templates overload non-template names") {
    const ProgramResult program = compile_and_run(
        "namespace std {\n"
        "inline void arrange(int* first, int* last) { }\n"
        "template<typename Compare>\n"
        "inline void arrange(int* first, int* last, Compare compare) {\n"
        "    int* current = first;\n"
        "    if (current != last) current = current + 1;\n"
        "    while (current < last) {\n"
        "        int value = *current;\n"
        "        int* hole = current;\n"
        "        while (hole > first && compare(value, *(hole - 1))) {\n"
        "            *hole = *(hole - 1);\n"
        "            hole = hole - 1;\n"
        "        }\n"
        "        *hole = value;\n"
        "        current = current + 1;\n"
        "    }\n"
        "}\n"
        "}\n"
        "int main() {\n"
        "    int values[4] = { 3, 1, 4, 2 };\n"
        "    std::arrange(values, values + 4,\n"
        "                 [](int left, int right) { return right < left; });\n"
        "    return values[0] == 4 && values[3] == 1 ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("function templates substitute pointer element types") {
    const ProgramResult program = compile_and_run(
        "template<typename T> void sort_values(T* first, T* last) {\n"
        "    T* current = first;\n"
        "    if (current != last) current = current + 1;\n"
        "    while (current < last) {\n"
        "        T value = *current;\n"
        "        T* hole = current;\n"
        "        while (hole > first && value < *(hole - 1)) {\n"
        "            *hole = *(hole - 1);\n"
        "            hole = hole - 1;\n"
        "        }\n"
        "        *hole = value;\n"
        "        current = current + 1;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    char text[4] = \"dcba\";\n"
        "    sort_values(text, text + 4);\n"
        "    if (text[0] != 'a' || text[3] != 'd') return 1;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    sort_values(reals, reals + 3);\n"
        "    return reals[0] == 0.5 && reals[2] == 2.5 ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm predicate callbacks") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "bool positive(int value) { return value > 0; }\n"
        "int main() {\n"
        "    int values[5] = { -1, 2, -3, 4, 5 };\n"
        "    if (!std::any_of(values, values + 5, positive)) return 1;\n"
        "    if (std::all_of(values, values + 5, positive)) return 2;\n"
        "    if (!std::none_of(values, values + 5,\n"
        "                      [](int value) { return value == 0; })) return 3;\n"
        "    if (std::find_if(values, values + 5, positive) != values + 1) return 4;\n"
        "    if (std::find_if_not(values, values + 5, positive) != values) return 5;\n"
        "    if (std::count_if(values, values + 5, positive) != 3) return 6;\n"
        "    int output[5] = { 0, 0, 0, 0, 0 };\n"
        "    std::transform(values, values + 5, output,\n"
        "                   [](int value) { return value * 2; });\n"
        "    if (output[1] != 4 || output[4] != 10) return 7;\n"
        "    int* end = std::remove_if(values, values + 5, positive);\n"
        "    if (end != values + 2 || values[0] != -1 || values[1] != -3) return 8;\n"
        "    std::replace_if(values, values + 5,\n"
        "                    [](int value) { return value < 0; }, 0);\n"
        "    if (values[0] != 0 || values[1] != 0) return 9;\n"
        "    std::sort(values, values + 5,\n"
        "              [](int left, int right) { return right < left; });\n"
        "    return values[0] == 5 && values[4] == 0 ? 0 : 10;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds unsigned and float extremes") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    unsigned int values[3] = { 9, 2, 7 };\n"
        "    if (std::min_element(values, values + 3) != values + 1) return 1;\n"
        "    if (std::max_element(values, values + 3) != values) return 2;\n"
        "    float reals[3] = { 1.5f, 3.5f, 2.5f };\n"
        "    if (std::min_element(reals, reals + 3) != reals) return 3;\n"
        "    return std::max_element(reals, reals + 3) == reals + 1 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm finds minimum and maximum elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 3, 9, 2, 7 };\n"
        "    if (std::min_element(values, values + 4) != values + 2) return 1;\n"
        "    if (std::max_element(values, values + 4) != values + 1) return 2;\n"
        "    char text[4] = \"dcba\";\n"
        "    if (std::min_element(text, text + 4) != text + 3) return 3;\n"
        "    double reals[3] = { 1.5, 0.5, 2.5 };\n"
        "    if (std::max_element(reals, reals + 3) != reals + 2) return 4;\n"
        "    return std::max_element(values, values) == values ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("pointer differences count elements") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int values[5] = { 1, 2, 3, 4, 5 };\n"
        "    int* end = values + 3;\n"
        "    if (end - values != 3) return 1;\n"
        "    char text[6] = \"abcde\";\n"
        "    char* pointer = text + 4;\n"
        "    if (pointer - text != 4) return 2;\n"
        "    double reals[3] = { 0.0, 0.0, 0.0 };\n"
        "    double* last = reals + 2;\n"
        "    return last - reals == 2 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm remove compacts ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 1, 3, 1 };\n"
        "    int* end = std::remove(values, values + 5, 1);\n"
        "    if (end != values + 2) return 1;\n"
        "    if (values[0] != 2 || values[1] != 3) return 2;\n"
        "    char text[6] = \"abaca\";\n"
        "    char* tail = std::remove(text, text + 5, 'a');\n"
        "    if (tail != text + 2) return 3;\n"
        "    if (text[0] != 'b' || text[1] != 'c') return 4;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    return std::remove(reals, reals + 3, 1.5) == reals + 1 ? 0 : 5;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm replace substitutes values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 1, 3 };\n"
        "    std::replace(values, values + 4, 1, 9);\n"
        "    if (values[0] != 9 || values[1] != 2 || values[2] != 9) return 1;\n"
        "    char text[5] = \"aabaa\";\n"
        "    std::replace(text, text + 5, 'a', 'z');\n"
        "    if (text[0] != 'z' || text[2] != 'b' || text[4] != 'z') return 2;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    std::replace(reals, reals + 3, 1.5, 9.0);\n"
        "    return reals[0] == 9.0 && reals[1] == 2.5 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copy_n copies counted elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 3 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::copy_n(source, 2, target);\n"
        "    if (end != target + 2) return 1;\n"
        "    if (target[1] != 2 || target[2] != 0) return 2;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    double out[2] = { 0.0, 0.0 };\n"
        "    std::copy_n(reals, 2, out);\n"
        "    if (out[0] != 1.5 || out[1] != 2.5) return 3;\n"
        "    return std::copy_n(source, 0, target) == target ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm fill_n writes counted elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 0, 0, 0, 0 };\n"
        "    int* end = std::fill_n(values, 3, 5);\n"
        "    if (end != values + 3) return 1;\n"
        "    if (values[2] != 5 || values[3] != 0) return 2;\n"
        "    double reals[2] = { 0.0, 0.0 };\n"
        "    std::fill_n(reals, 2, 1.5);\n"
        "    if (reals[0] != 1.5 || reals[1] != 1.5) return 3;\n"
        "    return std::fill_n(values, 0, 9) == values ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("numeric accumulates and fills sequences") {
    const ProgramResult program = compile_and_run(
        "#include <numeric>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 3, 4 };\n"
        "    if (std::accumulate(values, values + 4, 0) != 10) return 1;\n"
        "    double reals[3] = { 1.5, 2.5, 3.0 };\n"
        "    if (std::accumulate(reals, reals + 3, 0.0) != 7.0) return 2;\n"
        "    int sequence[4] = { 0, 0, 0, 0 };\n"
        "    std::iota(sequence, sequence + 4, 5);\n"
        "    return sequence[0] == 5 && sequence[3] == 8 ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm equal compares ranges") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int left[3] = { 1, 2, 3 };\n"
        "    int right[3] = { 1, 2, 3 };\n"
        "    if (!std::equal(left, left + 3, right)) return 1;\n"
        "    right[1] = 9;\n"
        "    if (std::equal(left, left + 3, right)) return 2;\n"
        "    char a[4] = \"abc\";\n"
        "    char b[4] = \"abc\";\n"
        "    if (!std::equal(a, a + 3, b)) return 3;\n"
        "    double x[2] = { 1.5, 2.5 };\n"
        "    double y[2] = { 1.5, 2.5 };\n"
        "    return std::equal(x, x + 2, y) ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm count tallies elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[5] = { 1, 2, 2, 3, 2 };\n"
        "    if (std::count(values, values + 5, 2) != 3) return 1;\n"
        "    char text[6] = \"abaca\";\n"
        "    if (std::count(text, text + 5, 'a') != 3) return 2;\n"
        "    double reals[3] = { 1.5, 2.5, 1.5 };\n"
        "    if (std::count(reals, reals + 3, 1.5) != 2) return 3;\n"
        "    return std::count(reals, reals + 3, 9.0) == 0 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm copy duplicates arrays") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int source[3] = { 1, 2, 3 };\n"
        "    int target[3] = { 0, 0, 0 };\n"
        "    int* end = std::copy(source, source + 3, target);\n"
        "    if (end != target + 3) return 1;\n"
        "    if (target[0] != 1 || target[2] != 3) return 2;\n"
        "    char text[4] = \"abc\";\n"
        "    char output[4] = \"xxx\";\n"
        "    std::copy(text, text + 3, output);\n"
        "    if (output[0] != 'a' || output[2] != 'c') return 3;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    double copied[2] = { 0.0, 0.0 };\n"
        "    std::copy(reals, reals + 2, copied);\n"
        "    return copied[0] == 1.5 && copied[1] == 2.5 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm find locates elements") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 3, 4 };\n"
        "    if (std::find(values, values + 4, 3) != values + 2) return 1;\n"
        "    if (std::find(values, values + 4, 9) != values + 4) return 2;\n"
        "    char text[4] = \"abc\";\n"
        "    if (std::find(text, text + 3, 'b') != text + 1) return 3;\n"
        "    double reals[2] = { 1.5, 2.5 };\n"
        "    return std::find(reals, reals + 2, 2.5) == reals + 1 ? 0 : 4;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm fill sets arrays") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 3, 4 };\n"
        "    std::fill(values, values + 4, 7);\n"
        "    if (values[0] != 7 || values[3] != 7) return 1;\n"
        "    double reals[3] = { 0.0, 0.0, 0.0 };\n"
        "    std::fill(reals, reals + 3, 1.5);\n"
        "    if (reals[0] != 1.5 || reals[2] != 1.5) return 2;\n"
        "    char text[4] = \"abc\";\n"
        "    std::fill(text, text + 3, 'z');\n"
        "    return text[0] == 'z' && text[2] == 'z' ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm reverse orders arrays") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 1, 2, 3, 4 };\n"
        "    std::reverse(values, values + 4);\n"
        "    if (values[0] != 4 || values[3] != 1) return 1;\n"
        "    double reals[3] = { 1.5, 2.5, 3.5 };\n"
        "    std::reverse(reals, reals + 3);\n"
        "    if (reals[0] != 3.5 || reals[2] != 1.5) return 2;\n"
        "    char text[4] = \"abc\";\n"
        "    std::reverse(text, text + 3);\n"
        "    return text[0] == 'c' && text[2] == 'a' ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm sort orders arrays") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    int values[4] = { 3, 1, 4, 2 };\n"
        "    std::sort(values, values + 4);\n"
        "    if (values[0] != 1 || values[3] != 4) return 1;\n"
        "    double reals[3] = { 2.5, 0.5, 1.5 };\n"
        "    std::sort(reals, reals + 3);\n"
        "    if (reals[0] != 0.5 || reals[2] != 2.5) return 2;\n"
        "    char text[4] = \"dcba\";\n"
        "    std::sort(text, text + 4);\n"
        "    return text[0] == 'a' && text[3] == 'd' ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("algorithm clamp bounds values") {
    const ProgramResult program = compile_and_run(
        "#include <algorithm>\n"
        "int main() {\n"
        "    if (std::clamp(9, 1, 4) != 4) return 1;\n"
        "    if (std::clamp(-2, 0, 5) != 0) return 2;\n"
        "    if (std::clamp(3, 1, 4) != 3) return 3;\n"
        "    if (std::clamp(2.5, 1.0, 2.0) != 2.0) return 4;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string starts and ends with strings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    std::string prefix(\"he\");\n"
        "    std::string suffix(\"lo\");\n"
        "    if (!text.starts_with(prefix)) return 1;\n"
        "    if (!text.ends_with(suffix)) return 2;\n"
        "    if (text.starts_with(\"xy\")) return 3;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string contains substrings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    if (!text.contains(\"ell\")) return 1;\n"
        "    if (text.contains(\"xyz\")) return 2;\n"
        "    std::string needle(\"lo\");\n"
        "    return text.contains(needle) ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("char assignments preserve neighboring bytes") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    char text[4] = \"abc\";\n"
        "    text[0] = 'x';\n"
        "    if (text[1] != 'b') return 1;\n"
        "    char& reference = text[2];\n"
        "    reference = 'z';\n"
        "    if (text[2] != 'z') return 2;\n"
        "    std::string value(\"abc\");\n"
        "    value.front() = 'x';\n"
        "    value.back() = 'z';\n"
        "    return value == \"xbz\" ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("const strings expose front and back") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    const std::string text(\"abc\");\n"
        "    if (text.front() != 'a') return 1;\n"
        "    if (text.back() != 'c') return 2;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string assigns repeated characters") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    text.assign(3, 'x');\n"
        "    return text == \"xxx\" ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string constructor overloads rank by conversion") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string repeated(3, 'x');\n"
        "    if (repeated != \"xxx\") return 1;\n"
        "    std::string prefix(\"abcdef\", 3);\n"
        "    return prefix == \"abc\" ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string replaces with repeated characters") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"abc\");\n"
        "    text.replace(1, 1, 2, 'x');\n"
        "    return text == \"axxc\" ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string erases all characters") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    text.erase();\n"
        "    return text.empty() ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string constructs from substrings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    std::string part(text, 1, 3);\n"
        "    if (part != \"ell\") return 1;\n"
        "    std::string tail(text, 3, 100);\n"
        "    if (tail != \"lo\") return 2;\n"
        "    std::string direct(\"hello\", 1, 3);\n"
        "    return direct == \"ell\" ? 0 : 3;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("reference-returning calls are lvalues") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "std::string& identity(std::string& text) { return text; }\n"
        "int main() {\n"
        "    std::string text(\"a\");\n"
        "    identity(text) += \"b\";\n"
        "    return text == \"ab\" ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string reserves capacity") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"ab\");\n"
        "    text.reserve(100);\n"
        "    if (text.capacity() < 100) return 1;\n"
        "    if (text != \"ab\") return 2;\n"
        "    return 0;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string shrinks to fit") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    text.reserve(100);\n"
        "    text.resize(2);\n"
        "    text.shrink_to_fit();\n"
        "    return text == \"he\" ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string append supports chaining") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"a\");\n"
        "    text.append(\"b\").append(\"c\");\n"
        "    return text == \"abc\" ? 0 : 1;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string assigns substrings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    std::string part(\"xxworldyy\");\n"
        "    text.assign(part, 2, 5);\n"
        "    if (text != \"world\") return 1;\n"
        "    text.assign(\"QQdoneRR\", 2, 4);\n"
        "    return text == \"done\" ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string appends substrings") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"ab\");\n"
        "    std::string part(\"xxcdyy\");\n"
        "    text.append(part, 2, 2);\n"
        "    if (text != \"abcd\") return 1;\n"
        "    text.append(\"QQefRR\", 2, 2);\n"
        "    return text == \"abcdef\" ? 0 : 2;\n"
        "}\n");
    INFO(program.diagnostics);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("string repeated-character constructor compiles") {
    const cxx::Result compiled = cxx::compile(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(3, 'x');\n"
        "    return text.size() == 3 ? 0 : 1;\n"
        "}\n");
    CHECK(compiled.ok);
}

TEST_CASE("enums expose integer constants and scoped names") {
    const ProgramResult program = compile_and_run(
        "enum Color { Red, Green = 4, Blue };\n"
        "enum class Mode { Off, On };\n"
        "namespace N { enum Flags { None = 0, One = 1, Two = 2 }; }\n"
        "class Box { public: enum Kind { Small = 1, Large = 2 }; };\n"
        "int main() {\n"
        "    if (Red != 0 || Green != 4 || Blue != 5) return 1;\n"
        "    if (N::One != 1 || Box::Large != 2) return 5;\n"
        "    int values[N::Two];\n"
        "    if (sizeof(values) != 8) return 6;\n"
        "    Color color = Blue;\n"
        "    if (color != Blue) return 2;\n"
        "    Mode mode = Mode::On;\n"
        "    switch (mode) {\n"
        "    case Mode::Off: return 3;\n"
        "    case Mode::On: break;\n"
        "    default: return 4;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("global unsized arrays infer length and initialize") {
    const ProgramResult program = compile_and_run(
        "char text[] = \"abc\";\n"
        "int values[] = {5, 6, 7};\n"
        "int main() {\n"
        "    if (sizeof(text) != 4) return 1;\n"
        "    if (text[2] != 'c' || text[3] != 0) return 2;\n"
        "    if (sizeof(values) != 12) return 3;\n"
        "    if (values[0] != 5 || values[2] != 7) return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("unsized arrays infer length from initializer lists") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int values[] = {1, 2, 3, 4};\n"
        "    if (sizeof(values) != 16) return 1;\n"
        "    return values[3] == 4 ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("local char arrays initialize from string literals") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    char buffer[8] = \"abc\";\n"
        "    if (buffer[0] != 'a' || buffer[1] != 'b' || buffer[2] != 'c') {\n"
        "        return 1;\n"
        "    }\n"
        "    if (buffer[3] != 0) return 2;\n"
        "    char inferred[] = \"abc\";\n"
        "    if (sizeof(inferred) != 4) return 3;\n"
        "    if (inferred[2] != 'c') return 4;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("namespace class references match unqualified parameter types") {
    const ProgramResult program = compile_and_run(
        "namespace N {\n"
        "class A { public: int x; };\n"
        "int read(A& value) { return value.x; }\n"
        "}\n"
        "int main() {\n"
        "    N::A a;\n"
        "    a.x = 7;\n"
        "    return N::read(a) == 7 ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("64-bit unary minus carries into the high word") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    long long value = 1234567890123LL;\n"
        "    if (-value != -1234567890123LL) return 1;\n"
        "    if ((int)((-value) >> 32) != -288) return 2;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("64-bit returns preserve the high word") {
    const ProgramResult program = compile_and_run(
        "long long make() { return -123; }\n"
        "int main() {\n"
        "    long long value = make();\n"
        "    if (value != -123) return 1;\n"
        "    if ((int)(value >> 32) != -1) return 2;\n"
        "    return 0;\n"
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
        "    if (std::strtoll(\"-123z\", &end, 10) != -123) return 12;\n"
        "    if (std::strtoull(\"123z\", &end, 10) != 123) return 13;\n"
        "    if (std::llabs(-1234567890123LL) != 1234567890123LL) return 14;\n"
        "    if (std::strtof(\"1.5x\", &end) != 1.5f || end[0] != 'x') return 15;\n"
        "    if (std::strtold(\"2.5x\", &end) != 2.5L || end[0] != 'x') return 16;\n"
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

