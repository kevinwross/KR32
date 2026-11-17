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

TEST_CASE("leaf locals stay in registers") {
    const ProgramResult program = compile_and_run(
        "int leaf(int a) { int x = a + 1; return x + x + x; }\n"
        "int main() { return leaf(2) == 9 ? 0 : 1; }\n",
        true);
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("logical not uses SLTU") {
    const cxx::Result result = cxx::compile(
        "int f(int x) { return !x; }\n"
        "long long g(long long y) { return !y; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("SLTU R0, R0, #1") != std::string::npos);
    CHECK(result.assembly.find("OR R0, R0, R1") != std::string::npos);
}

TEST_CASE("local comparisons avoid stack temporaries") {
    const cxx::Result result = cxx::compile(
        "int cmp(int a, int b) { if (a < b) return 1; return 2; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("LOAD R1, [R7 + 4]") != std::string::npos);
    CHECK(result.assembly.find("CMP R0, R1") != std::string::npos);
}

TEST_CASE("equality values avoid branches") {
    const cxx::Result result = cxx::compile(
        "int eq(int a, int b) { return a == b; }\n"
        "int ne(int a, int b) { return a != b; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("XOR R0, R0, R1") != std::string::npos);
    CHECK(result.assembly.find("SLTU R0, R0, #1") != std::string::npos);
}

TEST_CASE("comparison values use SLT") {
    const cxx::Result result = cxx::compile(
        "int lt(int a, int b) { return a < b; }\n"
        "int ge(int a, int b) { return a >= b; }\n"
        "int z(int x) { return x == 0; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("SLT R0, R0, R1") != std::string::npos);
    CHECK(result.assembly.find("XOR R0, R0, #0") == std::string::npos);
}

TEST_CASE("constant locals propagate") {
    const cxx::Result result = cxx::compile(
        "int f() { const int N = 5; return N; }\n");
    CHECK(result.ok);
    CHECK(result.assembly.find("LOAD R0, [R7 + 0]") == std::string::npos);
    CHECK(result.assembly.find("MOV R0, #5") != std::string::npos);
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

TEST_CASE("a call needs a landing pad only when the frame can clean up") {
    // The pad exists to run destructors during unwinding. With nothing to clean
    // up, the exception propagates through the frame unaided - the same way it
    // crosses a C function, which carries no descriptor at all.
    const cxx::Result noexcept_result = cxx::compile(
        "struct S { int f() const noexcept { return 1; } };\n"
        "struct D { ~D(); };\n"
        "int g(S* s) { D d; return s->f(); }\n");
    CHECK(noexcept_result.ok);
    CHECK(noexcept_result.assembly.find("_landing") == std::string::npos);

    const cxx::Result throwing_result = cxx::compile(
        "struct S { int f() const { return 1; } };\n"
        "struct D { ~D(); };\n"
        "int g(S* s) { D d; return s->f(); }\n");
    CHECK(throwing_result.ok);
    CHECK(throwing_result.assembly.find("_landing") != std::string::npos);

    const cxx::Result clean_result = cxx::compile(
        "struct S { int f() const { return 1; } };\n"
        "int g(S* s) { return s->f(); }\n");
    CHECK(clean_result.ok);
    CHECK(clean_result.assembly.find("_landing") == std::string::npos);
}

TEST_CASE("multi-statement functions inline at O1") {
    cxx::Options options;
    options.optimize = true;
    const cxx::Result result = cxx::compile(
        "int adjust(int a) { a *= 3; return a + 1; }\n"
        "struct Counter { int value; int bump() { value += 2; return value; } };\n"
        "int call_adjust(int x) { return adjust(x); }\n"
        "int call_bump(Counter* c) { return c->bump(); }\n",
        options);
    CHECK(result.ok);
    CHECK(result.assembly.find("CALL _Z6adjusti") == std::string::npos);
    CHECK(result.assembly.find("CALL _ZN7Counter4bumpEv") ==
          std::string::npos);
}

TEST_CASE("multi-statement inline behavior") {
    const ProgramResult program = compile_and_run(
        "struct Counter { int value; Counter(int v) : value(v) {} "
        "int bump() { value += 2; return value; } };\n"
        "int adjust(int a) { a *= 3; return a + 1; }\n"
        "int main() { Counter c(1); if (c.bump() != 3) return 1; "
        "if (c.bump() != 5) return 2; return adjust(2) == 7 ? 0 : 3; }\n",
        true);
    CHECK(program.ok);
    CHECK(program.result == 0);
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
    CHECK(result.assembly.find("SLTU R0, R0, #1") != std::string::npos);
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

