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
        "    std::string wide = std::to_string(-1234567890123LL);\n"
        "    if (wide != \"-1234567890123\") return 40;\n"
        "    if (std::stoi(\"-42\") != -42) return 50;\n"
        "    if (std::stol(\"123\") != 123) return 51;\n"
        "    std::string numeric(\"-42\");\n"
        "    if (std::stoi(numeric) != -42) return 52;\n"
        "    if (std::stod(\"2.5y\") != 2.5) return 53;\n"
        "    if (std::stof(\"1.5\") != 1.5f) return 54;\n"
        "    if (std::stold(\"2.5\") != 2.5L) return 55;\n"
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

