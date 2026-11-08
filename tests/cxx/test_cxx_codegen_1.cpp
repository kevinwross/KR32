#include "cxx_test_support.h"

TEST_CASE("member operator overloading supports chaining") {
    const ProgramResult program = compile_and_run(
        "class Box {\n"
        "public:\n"
        "    int value;\n"
        "    Box(int initial) : value(initial) {}\n"
        "    Box& operator<<(int amount);\n"
        "};\n"
        "Box& Box::operator<<(int amount) { value = value + amount; return *this; }\n"
        "int main() { Box box(0); box << 1 << 2; return box.value; }\n");
    CHECK(program.ok);
    CHECK(program.result == 3);
}

TEST_CASE("member index, compound, and unary operators run") {
    const ProgramResult program = compile_and_run(
        "class Number {\n"
        "public:\n"
        "    int data[2];\n"
        "    Number(int value) { data[0] = value; data[1] = 0; }\n"
        "    int& operator[](int index);\n"
        "    Number& operator+=(int amount);\n"
        "    bool operator!() const;\n"
        "};\n"
        "int& Number::operator[](int index) { return data[index]; }\n"
        "Number& Number::operator+=(int amount) { data[0] = data[0] + amount; return *this; }\n"
        "bool Number::operator!() const { return data[0] == 0; }\n"
        "int main() {\n"
        "    Number number(1);\n"
        "    number[1] = 7;\n"
        "    number += 2;\n"
        "    if (!number) return 1;\n"
        "    return number[0] + number[1] - 10;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("member operator() supports zero and one argument") {
    const ProgramResult program = compile_and_run(
        "class Adder {\n"
        "public:\n"
        "    int base;\n"
        "    Adder(int value) : base(value) {}\n"
        "    int operator()(int value);\n"
        "};\n"
        "int Adder::operator()(int value) { return base + value; }\n"
        "class Constant {\n"
        "public:\n"
        "    int operator()();\n"
        "};\n"
        "int Constant::operator()() { return 9; }\n"
        "int main() {\n"
        "    Adder adder(3);\n"
        "    Constant constant;\n"
        "    return adder(4) + constant() - 16;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("free binary operators participate in overload resolution") {
    const ProgramResult program = compile_and_run(
        "class Point {\n"
        "public:\n"
        "    int x;\n"
        "    Point(int value) : x(value) {}\n"
        "};\n"
        "int operator+(const Point& left, const Point& right) {\n"
        "    return left.x + right.x;\n"
        "}\n"
        "int main() {\n"
        "    Point a(1);\n"
        "    Point b(2);\n"
        "    return a + b - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("free stream operators support custom types") {
    const ProgramResult program = compile_and_run(
        "#include <iostream>\n"
        "class Widget {\n"
        "public:\n"
        "    int value;\n"
        "    Widget(int initial) : value(initial) {}\n"
        "};\n"
        "std::ostream& operator<<(std::ostream& out, const Widget& widget) {\n"
        "    return out << widget.value;\n"
        "}\n"
        "int main() {\n"
        "    Widget widget(42);\n"
        "    std::cout << \"value=\" << widget << std::endl;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
    CHECK(program.output == "value=42\n");
}

TEST_CASE("member operator() supports multiple arguments") {
    const ProgramResult program = compile_and_run(
        "class Combine {\n"
        "public:\n"
        "    int base;\n"
        "    Combine(int value) : base(value) {}\n"
        "    int operator()(int left, int right);\n"
        "};\n"
        "int Combine::operator()(int left, int right) {\n"
        "    return base + left + right;\n"
        "}\n"
        "int main() {\n"
        "    Combine combine(1);\n"
        "    return combine(2, 3) - 6;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("member postfix increment and decrement run") {
    const ProgramResult program = compile_and_run(
        "class Counter {\n"
        "public:\n"
        "    int value;\n"
        "    Counter(int initial) : value(initial) {}\n"
        "    Counter& operator++();\n"
        "    int operator++(int dummy);\n"
        "};\n"
        "Counter& Counter::operator++() { value = value + 1; return *this; }\n"
        "int Counter::operator++(int dummy) {\n"
        "    int old = value;\n"
        "    value = value + 1;\n"
        "    return old;\n"
        "}\n"
        "int main() {\n"
        "    Counter counter(1);\n"
        "    int old = counter++;\n"
        "    if (old != 1) return 1;\n"
        "    if (counter.value != 2) return 2;\n"
        "    ++counter;\n"
        "    return counter.value - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("member operators accept class-by-value parameters") {
    const ProgramResult program = compile_and_run(
        "class Point {\n"
        "public:\n"
        "    int x;\n"
        "    Point(int value) : x(value) {}\n"
        "    int operator+(Point other);\n"
        "};\n"
        "int Point::operator+(Point other) { return x + other.x; }\n"
        "int main() {\n"
        "    Point a(1);\n"
        "    Point b(2);\n"
        "    return a + b - 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("iostream formatting state supports base, width, and precision") {
    const ProgramResult program = compile_and_run(
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout.hex();\n"
        "    std::cout << 255 << std::endl;\n"
        "    std::cout.dec();\n"
        "    std::cout.width(4);\n"
        "    std::cout << 7 << std::endl;\n"
        "    std::cout.precision(2);\n"
        "    std::cout << 3.14159 << std::endl;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
    CHECK(program.output == "ff\n   7\n3.1\n");
}

TEST_CASE("punctuator-looking string literals do not confuse parsing") {
    const ProgramResult program = compile_and_run(
        "class Text {\n"
        "public:\n"
        "    const char* data;\n"
        "    Text(const char* value) { data = value; }\n"
        "    void append(const Text& other) { (void)other; }\n"
        "};\n"
        "int main() {\n"
        "    Text text(\"hello\");\n"
        "    Text suffix(\"!\");\n"
        "    text.append(suffix);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("operator[] returning a scalar by value is loaded correctly") {
    const ProgramResult program = compile_and_run(
        "class Text {\n"
        "    const char* data_;\n"
        "public:\n"
        "    Text(const char* text) { data_ = text; }\n"
        "    char operator[](int index) const { return data_[index]; }\n"
        "};\n"
        "int main() {\n"
        "    Text text(\"hello\");\n"
        "    return text[0] == 'h' ? 0 : 1;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("std::string subset supports construction, append, and assignment") {
    const ProgramResult program = compile_and_run(
        "#include <string>\n"
        "int main() {\n"
        "    std::string text(\"hello\");\n"
        "    if (text.size() != 5) return 1;\n"
        "    if (text[0] != 'h') return 2;\n"
        "    text.append(\"!\");\n"
        "    if (text.size() != 6) return 3;\n"
        "    text += \"?\";\n"
        "    if (text.size() != 7) return 4;\n"
        "    text = \"world\";\n"
        "    if (text.size() != 5) return 5;\n"
        "    if (text[0] != 'w') return 6;\n"
        "    std::string copy(text);\n"
        "    if (!(text == copy)) return 8;\n"
        "    std::string combined = text + copy;\n"
        "    if (combined.size() != 10) return 9;\n"
        "    const std::string constant(\"hello\");\n"
        "    if (constant[0] != 'h') return 10;\n"
        "    std::string number = std::to_string(42);\n"
        "    if (number != \"42\") return 11;\n"
        "    std::string resized(\"hello\");\n"
        "    resized.resize(3);\n"
        "    if (resized != \"hel\") return 12;\n"
        "    std::string replaced(\"hello world\");\n"
        "    replaced.replace(6, 5, \"there\");\n"
        "    if (replaced != \"hello there\") return 13;\n"
        "    std::string suffix(\"!!\");\n"
        "    replaced += suffix;\n"
        "    if (replaced != \"hello there!!\") return 14;\n"
        "    std::string needle(\"there\");\n"
        "    if (replaced.find(needle) != 6) return 15;\n"
        "    if (replaced.rfind(needle) != 6) return 16;\n"
        "    if (replaced.compare(\"abc\") <= 0) return 17;\n"
        "    std::string apple(\"apple\");\n"
        "    std::string banana(\"banana\");\n"
        "    std::string decorated = replaced + \"?\";\n"
        "    if (decorated != \"hello there!!?\") return 23;\n"
        "    std::string prefixed = \"<\" + apple;\n"
        "    if (prefixed != \"<apple\") return 24;\n"
        "    std::string edited(\"hello\");\n"
        "    edited.insert(5, \" world\");\n"
        "    if (edited != \"hello world\") return 25;\n"
        "    edited.erase(5, 6);\n"
        "    if (edited != \"hello\") return 26;\n"
        "    bool starts = replaced.starts_with(\"hello\");\n"
        "    if (!starts) return 27;\n"
        "    bool ends = replaced.ends_with(\"!!\");\n"
        "    if (!ends) return 28;\n"
        "    if (replaced.find_first_of(\"te\") != 1) return 29;\n"
        "    if (replaced.find_last_of(\"!t\") != 12) return 30;\n"
        "    std::string tail = replaced.substr(6);\n"
        "    if (tail != \"there!!\") return 31;\n"
        "    if (replaced.at(0) != 'h') return 32;\n"
        "    std::string appended(\"one\");\n"
        "    std::string other(\"two\");\n"
        "    appended.append(other);\n"
        "    if (appended != \"onetwo\") return 33;\n"
        "    appended.swap(other);\n"
        "    if (appended != \"two\") return 34;\n"
        "    if (other != \"onetwo\") return 35;\n"
        "    bool less = apple < banana;\n"
        "    if (!less) return 18;\n"
        "    if (!(apple < banana)) return 21;\n"
        "    if (!(banana > apple)) return 22;\n"
        "    bool c_string_less_equal = apple <= \"apple\";\n"
        "    if (!c_string_less_equal) return 19;\n"
        "    bool reverse = \"banana\" <= apple;\n"
        "    if (reverse) return 20;\n"
        "    text.clear();\n"
        "    if (!text.empty()) return 7;\n"
        "    std::string repeated(\"abcabc\");\n"
        "    if (repeated.find(\"bc\", 2) != 4) return 36;\n"
        "    if (repeated.find('b', 2) != 4) return 37;\n"
        "    if (repeated.find(needle, 2) != -1) return 38;\n"
        "    std::string replaced_with_string(\"abc\");\n"
        "    std::string replacement(\"XY\");\n"
        "    replaced_with_string.replace(1, 1, replacement);\n"
        "    if (replaced_with_string != \"aXYc\") return 39;\n"
        "    replaced_with_string.insert(1, replacement);\n"
        "    if (replaced_with_string != \"aXYXYc\") return 40;\n"
        "    std::string char_text(\"ab\");\n"
        "    std::string appended_char = char_text + 'c';\n"
        "    if (appended_char != \"abc\") return 41;\n"
        "    std::string prefixed_char = 'd' + char_text;\n"
        "    if (prefixed_char != \"dab\") return 42;\n"
        "    if (repeated.rfind(\"bc\", 3) != 1) return 43;\n"
        "    if (repeated.rfind('b', 2) != 1) return 44;\n"
        "    if (repeated.rfind(needle, 3) != -1) return 45;\n"
        "    if (repeated.find_first_of(\"cb\", 2) != 2) return 46;\n"
        "    if (repeated.find_last_of(\"cb\", 3) != 2) return 47;\n"
        "    if (repeated.find(\"zz\") != std::string::npos) return 48;\n"
        "    std::string iterated(\"ab\");\n"
        "    int char_total = 0;\n"
        "    for (char value : iterated) char_total = char_total + value;\n"
        "    if (char_total != 'a' + 'b') return 49;\n"
        "    for (char& value : iterated) value = value - 32;\n"
        "    if (iterated != \"AB\") return 50;\n"
        "    const std::string data_text(\"abc\");\n"
        "    const char* raw = data_text.data();\n"
        "    if (raw[1] != 'b') return 51;\n"
        "    std::string erased(\"abc\");\n"
        "    erased.erase(1);\n"
        "    if (erased != \"a\") return 52;\n"
        "    erased.insert(1, 'b');\n"
        "    if (erased != \"ab\") return 53;\n"
        "    if (repeated.compare(1, 2, \"bc\") != 0) return 54;\n"
        "    std::string bc(\"bc\");\n"
        "    if (repeated.compare(4, 2, bc) != 0) return 55;\n"
        "    std::string assigned(\"abc\");\n"
        "    assigned = 'z';\n"
        "    if (assigned != \"z\") return 56;\n"
        "    std::string repeated_chars;\n"
        "    repeated_chars.append(3, 'x');\n"
        "    if (repeated_chars != \"xxx\") return 57;\n"
        "    std::string inserted_chars(\"a\");\n"
        "    inserted_chars.insert(1, 2, 'y');\n"
        "    if (inserted_chars != \"ayy\") return 58;\n"
        "    std::string assigned_text(\"abc\");\n"
        "    assigned_text.assign(\"xy\");\n"
        "    if (assigned_text != \"xy\") return 59;\n"
        "    assigned_text.assign(\"hello\", 3);\n"
        "    if (assigned_text != \"hel\") return 60;\n"
        "    std::string spaced(\"  abc  \");\n"
        "    if (spaced.find_first_not_of(' ') != 2) return 61;\n"
        "    if (spaced.find_last_not_of(' ') != 4) return 62;\n"
        "    std::string spaces(\" \");\n"
        "    if (spaced.find_first_not_of(spaces) != 2) return 63;\n"
        "    std::string resized_fill(\"ab\");\n"
        "    resized_fill.resize(5, 'x');\n"
        "    if (resized_fill != \"abxxx\") return 64;\n"
        "    std::string prefix_append(\"a\");\n"
        "    prefix_append.append(\"bcdef\", 3);\n"
        "    if (prefix_append != \"abcd\") return 65;\n"
        "    std::string inserted_prefix(\"abcd\");\n"
        "    inserted_prefix.insert(1, \"XYZ\", 2);\n"
        "    if (inserted_prefix != \"aXYbcd\") return 66;\n"
        "    std::string other_prefix(\"bcdef\");\n"
        "    std::string prefixed_string(\"a\");\n"
        "    prefixed_string.append(other_prefix, 3);\n"
        "    if (prefixed_string != \"abcd\") return 67;\n"
        "    std::string replaced_prefix(\"abcd\");\n"
        "    replaced_prefix.replace(1, 2, \"XYZ\", 2);\n"
        "    if (replaced_prefix != \"aXYd\") return 68;\n"
        "    if (repeated.compare(1, 2, \"bcde\", 2) != 0) return 69;\n"
        "    std::string bcde(\"bcde\");\n"
        "    if (repeated.compare(4, 2, bcde, 2) != 0) return 70;\n"
        "    std::string copy_source(\"y\");\n"
        "    std::string copy_target(\"x\");\n"
        "    copy_target = copy_source;\n"
        "    if (copy_target != \"y\") return 71;\n"
        "    copy_target.append(\"z\");\n"
        "    if (copy_source != \"y\") return 72;\n"
        "    copy_target = copy_target;\n"
        "    if (copy_target != \"yz\") return 73;\n"
        "    if (repeated.compare(1, 2, \"XXbcde\", 2, 2) != 0) return 74;\n"
        "    std::string deep_compare(\"XXbcde\");\n"
        "    if (repeated.compare(4, 2, deep_compare, 2, 2) != 0) return 75;\n"
        "    if (spaced.find_first_not_of(' ', 1) != 2) return 76;\n"
        "    if (spaced.find_last_not_of(' ', 4) != 4) return 77;\n"
        "    if (spaced.find_first_not_of(spaces, 1) != 2) return 78;\n"
        "    if (spaced.find_last_not_of(spaces, 4) != 4) return 79;\n"
        "    std::string prefix_ctor(\"abcdef\", 3);\n"
        "    if (prefix_ctor != \"abc\") return 80;\n"
        "    std::string reserved(\"abc\");\n"
        "    reserved.reserve(16);\n"
        "    if (reserved.size() != 3 || reserved.capacity() < 3) return 81;\n"
        "    char copied_buffer[4] = {0, 0, 0, 0};\n"
        "    if (repeated.copy(copied_buffer, 2, 1) != 2) return 82;\n"
        "    if (copied_buffer[0] != 'b' || copied_buffer[1] != 'c') return 83;\n"
        "    if (repeated.find_first_of('a') != 0) return 84;\n"
        "    if (repeated.find_last_of('c') != 5) return 85;\n"
        "    if (repeated.find_first_of('a', 1) != 3) return 86;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("user-defined assignment operators are selected") {
    const ProgramResult program = compile_and_run(
        "class Box {\n"
        "public:\n"
        "    int value;\n"
        "    Box(int initial) : value(initial) {}\n"
        "    Box& operator=(const char* text) { value = text[0]; return *this; }\n"
        "};\n"
        "int main() {\n"
        "    Box box(0);\n"
        "    box = \"A\";\n"
        "    return box.value - 65;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("class temporary calls and by-value returns run") {
    const ProgramResult program = compile_and_run(
        "struct A { int value; A(int v) : value(v) {} };\n"
        "A create() { return A(7); }\n"
        "int main() {\n"
        "    A a = create();\n"
        "    return a.value - 7;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("const member overloads are distinct and selectable") {
    const ProgramResult program = compile_and_run(
        "class A {\n"
        "public:\n"
        "    int value;\n"
        "    A(int v) : value(v) {}\n"
        "    int get() { return value; }\n"
        "    int get() const { return value + 1; }\n"
        "};\n"
        "int main() {\n"
        "    A a(7);\n"
        "    const A b(7);\n"
        "    return (a.get() - 7) + (b.get() - 8);\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("conversion operators work in conditions and casts") {
    const ProgramResult program = compile_and_run(
        "class Flag {\n"
        "public:\n"
        "    int value;\n"
        "    Flag(int input) : value(input) {}\n"
        "    operator bool() const { return value != 0; }\n"
        "};\n"
        "class Number {\n"
        "public:\n"
        "    int value;\n"
        "    Number(int input) : value(input) {}\n"
        "    operator int() const { return value; }\n"
        "};\n"
        "Number global_number = 9;\n"
        "int take(int input) { return input; }\n"
        "bool truth(Flag flag) { return flag; }\n"
        "int main() {\n"
        "    Flag zero(0);\n"
        "    if (zero) return 1;\n"
        "    Flag one(1);\n"
        "    if (!(bool)one) return 2;\n"
        "    if (truth(zero)) return 3;\n"
        "    if (!truth(one)) return 4;\n"
        "    if (!(zero == 0)) return 5;\n"
        "    if (one == 0) return 6;\n"
        "    if (!(one != 0)) return 7;\n"
        "    if (!(0 == zero)) return 10;\n"
        "    if (!(1 == one)) return 11;\n"
        "    if (!(zero < 1)) return 12;\n"
        "    if (!(one > 0)) return 13;\n"
        "    Number number(7);\n"
        "    int converted = number;\n"
        "    if (converted != 7) return 8;\n"
        "    if (take(number) != 7) return 9;\n"
        "    if (number + 1 != 8) return 14;\n"
        "    if (1 + number != 8) return 15;\n"
        "    if (number - 1 != 6) return 16;\n"
        "    if (number * 2 != 14) return 17;\n"
        "    if (number / 2 != 3) return 18;\n"
        "    if (number % 4 != 3) return 19;\n"
        "    if ((number & 1) != 1) return 20;\n"
        "    if ((number * 2) != 14) return 21;\n"
        "    Number implicit = 5;\n"
        "    if (implicit.value != 5) return 22;\n"
        "    if (take(8) != 8) return 23;\n"
        "    if (global_number.value != 9) return 24;\n"
        "    return (int)number - 7;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("alignas raises class size and alignment") {
    const ProgramResult program = compile_and_run(
        "struct alignas(16) First { char value; };\n"
        "alignas(32) struct Second { char value; };\n"
        "int main() {\n"
        "    if (sizeof(First) != 16) return 1;\n"
        "    if (sizeof(Second) != 32) return 2;\n"
        "    if (alignof(First) != 16) return 3;\n"
        "    return alignof(Second) == 32 ? 0 : 4;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("C-style casts convert between class types") {
    const ProgramResult program = compile_and_run(
        "struct Source { int value; };\n"
        "struct Target {\n"
        "    int value;\n"
        "    Target(const Source& source) : value(source.value + 1) {}\n"
        "};\n"
        "int take(Target target) { return target.value; }\n"
        "int main() {\n"
        "    Source source;\n"
        "    source.value = 4;\n"
        "    Target target = (Target)source;\n"
        "    Target copied = source;\n"
        "    if (copied.value != 5) return 1;\n"
        "    if (take(source) != 5) return 2;\n"
        "    return target.value - 5;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("predefined source macros expand") {
    const ProgramResult program = compile_and_run(
        "int main() {\n"
        "    int line = __LINE__;\n"
        "    if (line != 2) return 1;\n"
        "    int first = __COUNTER__;\n"
        "    int second = __COUNTER__;\n"
        "    if (__DATE__[0] == 0) return 3;\n"
        "    if (__TIME__[2] != ':') return 4;\n"
        "    return first == 0 && second == 1 ? 0 : 2;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<utility> provides std::swap") {
    const ProgramResult program = compile_and_run(
        "#include <utility>\n"
        "#include <utility>\n"
        "int main() {\n"
        "    int a = 1, b = 2;\n"
        "    std::swap(a, b);\n"
        "    if (a != 2 || b != 1) return 1;\n"
        "    double x = 1.5, y = 2.5;\n"
        "    std::swap(x, y);\n"
        "    if (x != 2.5 || y != 1.5) return 2;\n"
        "    char c = 'a', d = 'b';\n"
        "    std::swap(c, d);\n"
        "    return c == 'b' && d == 'a' ? 0 : 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cfloat> provides floating limits") {
    const ProgramResult program = compile_and_run(
        "#include <cfloat>\n"
        "#include <cfloat>\n"
        "int main() {\n"
        "    if (FLT_DIG != 6 || DBL_DIG != 15) return 1;\n"
        "    if (FLT_MANT_DIG != 24 || DBL_MANT_DIG != 53) return 2;\n"
        "    float max = FLT_MAX;\n"
        "    double dmax = DBL_MAX;\n"
        "    if (!(max > 0.0f)) return 3;\n"
        "    if (!(dmax > 1.0)) return 4;\n"
        "    if (!(FLT_MIN > 0.0f)) return 5;\n"
        "    if (!(DBL_MIN > 0.0)) return 6;\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<climits> provides integer limits") {
    const ProgramResult program = compile_and_run(
        "#include <climits>\n"
        "#include <climits>\n"
        "int main() {\n"
        "    if (CHAR_BIT != 8) return 1;\n"
        "    if (SCHAR_MIN != -128 || SCHAR_MAX != 127) return 2;\n"
        "    if (UCHAR_MAX != 255) return 3;\n"
        "    if (SHRT_MIN != -32768 || SHRT_MAX != 32767) return 4;\n"
        "    if (USHRT_MAX != 65535) return 5;\n"
        "    if (INT_MIN != -2147483647 - 1) return 6;\n"
        "    if (INT_MAX != 2147483647) return 7;\n"
        "    if (UINT_MAX != 4294967295) return 8;\n"
        "    return LONG_MAX == 2147483647 ? 0 : 9;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("short types have two-byte layout") {
    const ProgramResult program = compile_and_run(
        "struct Pair { char tag; short value; int next; };\n"
        "int sum(short* values, int count) {\n"
        "    int total = 0;\n"
        "    for (int index = 0; index < count; ++index) total += values[index];\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    if (sizeof(short) != 2) return 1;\n"
        "    if (sizeof(unsigned short) != 2) return 2;\n"
        "    if (sizeof(struct Pair) != 8) return 3;\n"
        "    short values[3];\n"
        "    values[0] = 10; values[1] = 20; values[2] = 30;\n"
        "    if (sum(values, 3) != 60) return 4;\n"
        "    struct Pair pair;\n"
        "    pair.tag = 'a'; pair.value = -5; pair.next = 9;\n"
        "    if (pair.value != -5) return 5;\n"
        "    unsigned short wrapped = 70000;\n"
        "    return wrapped == 4464 ? 0 : 6;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstdint> provides fixed-width types") {
    const ProgramResult program = compile_and_run(
        "#include <cstdint>\n"
        "#include <cstdint>\n"
        "int main() {\n"
        "    if (sizeof(std::uint8_t) != 1) return 1;\n"
        "    if (sizeof(std::uint16_t) != 2) return 2;\n"
        "    if (sizeof(std::uint32_t) != 4) return 3;\n"
        "    if (sizeof(std::uint64_t) != 8) return 4;\n"
        "    std::int64_t value = 123456789;\n"
        "    std::uint64_t wide = 987654321;\n"
        "    return value + (std::int64_t)wide == 1111111110 ? 0 : 5;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cstddef> provides size types") {
    const ProgramResult program = compile_and_run(
        "#include <cstddef>\n"
        "#include <cstddef>\n"
        "int main() {\n"
        "    if (sizeof(std::size_t) != 4) return 1;\n"
        "    if (sizeof(std::ptrdiff_t) != 4) return 2;\n"
        "    std::size_t size = 9;\n"
        "    std::ptrdiff_t delta = -4;\n"
        "    return size + delta == 5 ? 0 : 3;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
}

TEST_CASE("<cassert> provides assert") {
    const ProgramResult program = compile_and_run(
        "#include <cassert>\n"
        "int main() {\n"
        "    assert(1 == 1);\n"
        "    int value = 3;\n"
        "    assert(value == 3);\n"
        "    return 0;\n"
        "}\n");
    CHECK(program.ok);
    CHECK(program.result == 0);
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

TEST_CASE("<cstdlib> converts C strings") {
    const ProgramResult program = compile_and_run(
        "#include <cstdlib>\n"
        "#include <cstdlib>\n"
        "int main() {\n"
        "    if (std::atoi(\"42\") != 42) return 1;\n"
        "    if (std::atoi(\"-7\") != -7) return 2;\n"
        "    if (std::atof(\"2.5\") != 2.5) return 3;\n"
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

