#include "cxx/preprocessor.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::cxx {

namespace {

struct Macro {
    bool function_like = false;
    std::vector<std::string> parameters;
    std::string body;
};

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool identifier_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Translation-phase 2/3 subset: splice continued lines and replace comments
// with spaces while preserving newlines and literals.
std::string clean_source(std::string_view source) {
    std::string result;
    result.reserve(source.size());
    std::size_t index = 0;
    enum class State { Normal, LineComment, BlockComment, String, Character };
    State state = State::Normal;
    while (index < source.size()) {
        const char c = source[index];
        if (state == State::Normal) {
            if (c == '\\' && index + 1 < source.size() && source[index + 1] == '\n') {
                // Joining lines shifts later diagnostics; keep a newline so the
                // source line count is retained, and let the directive parser
                // treat the continuation as whitespace.
                result += " \n";
                index += 2;
                continue;
            }
            if (c == '/' && index + 1 < source.size() && source[index + 1] == '/') {
                state = State::LineComment;
                ++index;
                continue;
            }
            if (c == '/' && index + 1 < source.size() && source[index + 1] == '*') {
                state = State::BlockComment;
                result += ' ';
                ++index;
                continue;
            }
            if (c == '"') {
                state = State::String;
                result += c;
                ++index;
                continue;
            }
            if (c == '\'') {
                state = State::Character;
                result += c;
                ++index;
                continue;
            }
            result += c;
            ++index;
            continue;
        }
        if (state == State::LineComment) {
            if (c == '\n') {
                result += '\n';
                state = State::Normal;
            }
            ++index;
            continue;
        }
        if (state == State::BlockComment) {
            if (c == '*' && index + 1 < source.size() && source[index + 1] == '/') {
                state = State::Normal;
                index += 2;
                continue;
            }
            if (c == '\n') {
                result += '\n';
            }
            ++index;
            continue;
        }
        if (state == State::String || state == State::Character) {
            result += c;
            if (c == '\\' && index + 1 < source.size()) {
                result += source[index + 1];
                index += 2;
                continue;
            }
            if ((state == State::String && c == '"') ||
                (state == State::Character && c == '\'')) {
                state = State::Normal;
            }
            ++index;
            continue;
        }
    }
    return result;
}

class IfExpression {
public:
    IfExpression(std::string_view text, std::string_view source_name,
                 std::vector<Diagnostic>& diagnostics, int line)
        : text_(text), source_name_(source_name), diagnostics_(diagnostics), line_(line) {
        tokenize();
    }

    std::int64_t evaluate() {
        position_ = 0;
        const std::int64_t value = parse_or();
        if (position_ != tokens_.size()) {
            std::string joined;
            for (const std::string& token : tokens_) {
                joined += token;
                joined += ' ';
            }
            error("unexpected token in preprocessor condition: " + tokens_[position_] +
                  " [" + joined + "]");
        }
        return value;
    }

private:
    void tokenize() {
        std::size_t index = 0;
        while (index < text_.size()) {
            const char c = text_[index];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++index;
                continue;
            }
            if (identifier_start(c)) {
                const std::size_t start = index;
                while (index < text_.size() && identifier_continue(text_[index])) {
                    ++index;
                }
                tokens_.push_back(std::string(text_.substr(start, index - start)));
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                const std::size_t start = index;
                while (index < text_.size() &&
                       (std::isalnum(static_cast<unsigned char>(text_[index])) != 0 ||
                        text_[index] == '\'')) {
                    ++index;
                }
                tokens_.push_back(std::string(text_.substr(start, index - start)));
                continue;
            }
            static const std::vector<std::string> two = {
                "&&", "||", "==", "!=", "<=", ">=", "<<", ">>"};
            bool matched = false;
            for (const std::string& op : two) {
                if (text_.substr(index, op.size()) == op) {
                    tokens_.push_back(op);
                    index += op.size();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                tokens_.push_back(std::string(1, c));
                ++index;
            }
        }
    }

    void error(std::string message) {
        diagnostics_.push_back({line_, 1, std::move(message)});
    }

    const std::string& peek() const {
        static const std::string empty;
        return position_ < tokens_.size() ? tokens_[position_] : empty;
    }

    bool match(std::string_view text) {
        if (peek() == text) {
            ++position_;
            return true;
        }
        return false;
    }

    std::int64_t parse_or() {
        std::int64_t value = parse_and();
        while (match("||")) {
            const std::int64_t right = parse_and();
            value = (value != 0 || right != 0) ? 1 : 0;
        }
        return value;
    }

    std::int64_t parse_and() {
        std::int64_t value = parse_bitor();
        while (match("&&")) {
            const std::int64_t right = parse_bitor();
            value = (value != 0 && right != 0) ? 1 : 0;
        }
        return value;
    }

    std::int64_t parse_bitor() {
        std::int64_t value = parse_bitxor();
        while (match("|")) {
            value |= parse_bitxor();
        }
        return value;
    }

    std::int64_t parse_bitxor() {
        std::int64_t value = parse_bitand();
        while (match("^")) {
            value ^= parse_bitand();
        }
        return value;
    }

    std::int64_t parse_bitand() {
        std::int64_t value = parse_equality();
        while (match("&")) {
            value &= parse_equality();
        }
        return value;
    }

    std::int64_t parse_equality() {
        std::int64_t value = parse_relational();
        while (true) {
            if (match("==")) {
                value = value == parse_relational() ? 1 : 0;
            } else if (match("!=")) {
                value = value != parse_relational() ? 1 : 0;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parse_relational() {
        std::int64_t value = parse_shift();
        while (true) {
            if (match("<=")) {
                value = value <= parse_shift() ? 1 : 0;
            } else if (match(">=")) {
                value = value >= parse_shift() ? 1 : 0;
            } else if (match("<")) {
                value = value < parse_shift() ? 1 : 0;
            } else if (match(">")) {
                value = value > parse_shift() ? 1 : 0;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parse_shift() {
        std::int64_t value = parse_additive();
        while (true) {
            if (match("<<")) {
                value <<= parse_additive();
            } else if (match(">>")) {
                value >>= parse_additive();
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parse_additive() {
        std::int64_t value = parse_multiplicative();
        while (true) {
            if (match("+")) {
                value += parse_multiplicative();
            } else if (match("-")) {
                value -= parse_multiplicative();
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parse_multiplicative() {
        std::int64_t value = parse_unary();
        while (true) {
            if (match("*")) {
                value *= parse_unary();
            } else if (match("/")) {
                const std::int64_t right = parse_unary();
                value = right == 0 ? 0 : value / right;
            } else if (match("%")) {
                const std::int64_t right = parse_unary();
                value = right == 0 ? 0 : value % right;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parse_unary() {
        if (match("!")) {
            return parse_unary() == 0 ? 1 : 0;
        }
        if (match("~")) {
            return ~parse_unary();
        }
        if (match("-")) {
            return -parse_unary();
        }
        if (match("+")) {
            return parse_unary();
        }
        if (match("(")) {
            const std::int64_t value = parse_or();
            if (!match(")")) {
                error("missing ')' in preprocessor condition");
            }
            return value;
        }
        if (peek().empty()) {
            error("unexpected end of preprocessor condition");
            return 0;
        }
        const std::string token = peek();
        ++position_;
        if (std::isdigit(static_cast<unsigned char>(token[0])) != 0) {
            return static_cast<std::int64_t>(std::strtoll(token.c_str(), nullptr, 0));
        }
        // Identifiers that survived macro expansion evaluate to zero.
        (void)source_name_;
        return 0;
    }

    std::string_view text_;
    std::string_view source_name_;
    std::vector<Diagnostic>& diagnostics_;
    int line_ = 0;
    std::vector<std::string> tokens_;
    std::size_t position_ = 0;
};

class Preprocessor {
public:
    Preprocessor(std::string_view source, std::string_view source_name,
                 std::vector<Diagnostic>& diagnostics)
        : source_name_(source_name), diagnostics_(diagnostics) {
        source_ = clean_source(source);
    }

    std::string run() {
        macros_["__cplusplus"] = Macro{false, {}, "201103L"};
        macros_["__KR32__"] = Macro{false, {}, "1"};
        std::istringstream lines(source_);
        std::string line;
        int line_number = 0;
        while (std::getline(lines, line)) {
            ++line_number;
            process_line(line, line_number);
        }
        while (!conditions_.empty()) {
            diagnostics_.push_back({line_number, 1, "unterminated preprocessor conditional"});
            conditions_.pop_back();
        }
        return result_;
    }

private:
    bool active() const { return conditions_.empty() || conditions_.back().active; }

    void append_line(const std::string& text) {
        result_ += text;
        result_ += '\n';
    }

    void error(int line, int column, std::string message) {
        diagnostics_.push_back({line, column, std::move(message)});
    }

    void process_line(const std::string& raw_line, int line_number) {
        std::string_view line = trim(raw_line);
        if (line.empty() || line.front() != '#') {
            if (active()) {
                append_line(expand(raw_line, line_number, 0));
            } else {
                append_line("");
            }
            return;
        }
        line.remove_prefix(1);
        line = trim(line);
        const std::size_t name_end = line.find_first_of(" \t(");
        const std::string directive(line.substr(0, name_end));
        std::string_view rest =
            name_end == std::string_view::npos ? std::string_view() : trim(line.substr(name_end));
        const bool include_directive = directive == "include" && active();
        handle_directive(directive, rest, line_number);
        if (include_directive) {
            // Built-in headers expand to several declarations. Reset the
            // preprocessed line counter so the following blank line and the
            // next source line keep their original line numbers.
            append_line("#line " + std::to_string(line_number));
        }
        append_line("");
    }

    void handle_directive(const std::string& directive, std::string_view rest,
                          int line_number) {
        if (directive == "define") {
            if (!active()) {
                return;
            }
            define_macro(rest, line_number);
            return;
        }
        if (directive == "undef") {
            if (!active()) {
                return;
            }
            macros_.erase(std::string(trim(rest)));
            return;
        }
        if (directive == "ifdef" || directive == "ifndef") {
            const std::string name(trim(rest));
            const bool defined = macros_.find(name) != macros_.end();
            start_conditional(directive == "ifdef" ? defined : !defined, line_number);
            return;
        }
        if (directive == "if") {
            const bool condition = evaluate_condition(std::string(rest), line_number);
            start_conditional(condition, line_number);
            return;
        }
        if (directive == "elif") {
            if (conditions_.empty()) {
                error(line_number, 1, "#elif without #if");
                return;
            }
            Conditional& current = conditions_.back();
            if (current.saw_else) {
                error(line_number, 1, "#elif after #else");
            }
            if (!current.taken && current.parent_active) {
                const bool condition = evaluate_condition(std::string(rest), line_number);
                current.active = condition;
                current.taken = condition;
            } else {
                current.active = false;
            }
            return;
        }
        if (directive == "else") {
            if (conditions_.empty()) {
                error(line_number, 1, "#else without #if");
                return;
            }
            Conditional& current = conditions_.back();
            if (current.saw_else) {
                error(line_number, 1, "duplicate #else");
            }
            current.saw_else = true;
            current.active = current.parent_active && !current.taken;
            current.taken = current.taken || current.active;
            return;
        }
        if (directive == "endif") {
            if (conditions_.empty()) {
                error(line_number, 1, "#endif without #if");
                return;
            }
            conditions_.pop_back();
            return;
        }
        if (!active()) {
            return;
        }
        if (directive == "include") {
            const std::string header(trim(rest));
            if (header.find("<initializer_list>") != std::string::npos ||
                header.find("initializer_list") != std::string::npos) {
                if (!initializer_list_header_injected_) {
                    initializer_list_header_injected_ = true;
                    append_line(
                        "namespace std {\n"
                        "template <typename T> class initializer_list {\n"
                        "public:\n"
                        "    const T* data_;\n"
                        "    int size_;\n"
                        "};\n"
                        "}\n");
                }
                return;
            }
            if (header.find("kr32/thread.hpp") != std::string::npos ||                header.find("thread.hpp") != std::string::npos ||
                header.find("<thread>") != std::string::npos ||
                header.find("<mutex>") != std::string::npos ||
                header.find("<condition_variable>") != std::string::npos ||
                header.find("<atomic>") != std::string::npos) {
                if (thread_header_injected_) {
                    return;
                }
                thread_header_injected_ = true;
                append_line(
                    "namespace kr32 {\n"
                    "inline int thread_create(int entry, int arg, int stack_size = 0) { return __builtin_kr32_syscall(44, entry, arg, stack_size); }\n"
                    "inline int thread_join(int handle) { return __builtin_kr32_syscall(45, handle, 0, 0); }\n"
                    "inline void thread_yield() { (void)__builtin_kr32_syscall(46, 0, 0, 0); }\n"
                    "inline void thread_exit(int code) { (void)__builtin_kr32_syscall(47, code, 0, 0); }\n"
                    "inline int thread_self() { return __builtin_kr32_syscall(50, 0, 0, 0); }\n"
                    "inline void sleep_for(int milliseconds) { if (milliseconds <= 0) { return; } const int start = (int)__builtin_kr32_syscall(40, 0, 0, 0); const int seconds = (milliseconds + 999) / 1000; while ((int)__builtin_kr32_syscall(40, 0, 0, 0) - start < seconds) { thread_yield(); } }\ninline void sleep_until(int seconds) { if (seconds <= 0) { return; } while ((int)__builtin_kr32_syscall(40, 0, 0, 0) < seconds) { thread_yield(); } }\n"
                    "inline int futex_wait(int* address, int expected) { return __builtin_kr32_syscall(48, (int)address, expected, 0); }\n"
                    "inline int futex_wake(int* address, int count) { return __builtin_kr32_syscall(49, (int)address, count, 0); }\n"
                    "struct atomic_int {\n"
                    "    int value;\n"
                    "    atomic_int() { value = 0; }\n"
                    "    explicit atomic_int(int initial) { value = initial; }\n"
                    "    int load() { return __atomic_load_n(&value, 0); }\n"
                    "    void store(int next) { __atomic_store_n(&value, next, 0); }\n"
                    "    int fetch_add(int delta) { return __atomic_fetch_add(&value, delta, 0); }\n"
                    "    bool compare_exchange(int* expected, int desired) { return __atomic_compare_exchange_n(&value, expected, desired, 0, 0, 0); }\n"
                    "};\n"
                    "struct mutex {\n"
                    "    int state;\n"
                    "    mutex() { state = 0; }\n"
                    "    void lock() { int expected = 0; while (!__atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0)) { expected = 0; futex_wait(&state, 1); } }\n"
                    "    bool try_lock() { int expected = 0; return __atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0); }\n"
                    "    bool try_lock_for(int milliseconds) { if (try_lock()) { return true; } if (milliseconds <= 0) { return false; } const int start = (int)__builtin_kr32_syscall(40, 0, 0, 0); const int seconds = (milliseconds + 999) / 1000; while ((int)__builtin_kr32_syscall(40, 0, 0, 0) - start < seconds) { thread_yield(); if (try_lock()) { return true; } } return false; }\n"
                    "    void unlock() { __atomic_store_n(&state, 0, 0); futex_wake(&state, 1); }\n"
                    "};\n"
                    "struct recursive_mutex {\n"
                    "    int state; int owner; int count;\n"
                    "    recursive_mutex() { state = 0; owner = 0; count = 0; }\n"
                    "    void lock() { const int self = thread_self(); if (owner == self) { count = count + 1; return; } int expected = 0; while (!__atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0)) { expected = 0; futex_wait(&state, 1); } owner = self; count = 1; }\n"
                    "    bool try_lock() { const int self = thread_self(); if (owner == self) { count = count + 1; return true; } int expected = 0; if (!__atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0)) { return false; } owner = self; count = 1; return true; }\n"
                    "    bool try_lock_for(int milliseconds) { if (try_lock()) { return true; } if (milliseconds <= 0) { return false; } const int start = (int)__builtin_kr32_syscall(40, 0, 0, 0); const int seconds = (milliseconds + 999) / 1000; while ((int)__builtin_kr32_syscall(40, 0, 0, 0) - start < seconds) { thread_yield(); if (try_lock()) { return true; } } return false; }\n"
                    "    void unlock() { if (owner != thread_self()) { return; } count = count - 1; if (count == 0) { owner = 0; __atomic_store_n(&state, 0, 0); futex_wake(&state, 1); } }\n"
                    "};\n"
                    "struct condition_variable {\n"
                    "    int sequence;\n"
                    "    condition_variable() { sequence = 0; }\n"
                    "    void wait(kr32::mutex& guard) { int observed = sequence; guard.unlock(); futex_wait(&sequence, observed); guard.lock(); }\n"                     "    void wait_for(kr32::mutex& guard, int milliseconds) { int observed = sequence; guard.unlock(); if (milliseconds > 0) { const int start = (int)__builtin_kr32_syscall(40, 0, 0, 0); const int seconds = (milliseconds + 999) / 1000; while (sequence == observed && (int)__builtin_kr32_syscall(40, 0, 0, 0) - start < seconds) { thread_yield(); } } guard.lock(); }\n"                     "    void wait_until(kr32::mutex& guard, int seconds) { int observed = sequence; guard.unlock(); if (seconds > 0) { while (sequence == observed && (int)__builtin_kr32_syscall(40, 0, 0, 0) < seconds) { thread_yield(); } } guard.lock(); }\n"
                    "    void notify_one() { __atomic_fetch_add(&sequence, 1, 0); futex_wake(&sequence, 1); }\n"
                    "    void notify_all() { __atomic_fetch_add(&sequence, 1, 0); futex_wake(&sequence, 2147483647); }\n"
                    "};\n"
                    "struct thread {\n"
                    "    typedef unsigned int id;\n"
                    "    int handle;\n"
                    "    thread() { handle = 0; }\n"
                    "    thread(int entry, int arg) { handle = thread_create(entry, arg, 0); }\n"
                    "    thread(void* entry, int arg) { handle = thread_create((int)entry, arg, 0); }\n"
                    "    bool joinable() { return handle != 0; }\n"
                    "    static int hardware_concurrency() { return 1; }\n"
                    "    unsigned int get_id() { return (unsigned int)handle; }\n"
                    "    void join() { if (handle != 0) { thread_join(handle); handle = 0; } }\n"
                    "    void detach() { handle = 0; }\n"
                    "    void yield() { thread_yield(); }\n"
                    "};\n"
                    "namespace this_thread {\n"
                    "inline void yield() { thread_yield(); }\n"
                    "inline int get_id() { return thread_self(); }\n"
                    "inline void sleep_for(int milliseconds) { kr32::sleep_for(milliseconds); } inline void sleep_until(int seconds) { kr32::sleep_until(seconds); }\n"
                    "}\n"
                    "}\n"
                    "namespace std {\n"
                    "typedef kr32::atomic_int atomic_int;\n"
                    "typedef kr32::condition_variable condition_variable;\n"
                    "typedef kr32::mutex mutex;\n"
                    "typedef kr32::recursive_mutex recursive_mutex;\n"
                    "struct thread {\n"
                    "    typedef unsigned int id;\n"
                    "    int handle;\n"
                    "    thread() { handle = 0; }\n"
                    "    thread(int entry, int arg) { handle = kr32::thread_create(entry, arg, 0); }\n"
                    "    thread(void* entry, int arg) { handle = kr32::thread_create((int)entry, arg, 0); }\n"
                    "    bool joinable() { return handle != 0; }\n"
                    "    static int hardware_concurrency() { return 1; }\n"
                    "    unsigned int get_id() { return (unsigned int)handle; }\n"
                    "    void join() { if (handle != 0) { kr32::thread_join(handle); handle = 0; } }\n"
                    "    void detach() { handle = 0; }\n"
                    "    int release() { int released = handle; handle = 0; return released; }\n"
                    "    void adopt(int other) { handle = other; }\n"
                    "    thread(std::thread& other) { handle = other.handle; other.handle = 0; }\n"
                    "    void yield() { kr32::thread_yield(); }\n"
                    "};\n"
                    "namespace this_thread { inline void yield() { kr32::thread_yield(); } inline std::thread::id get_id() { return (std::thread::id)kr32::thread_self(); } inline void sleep_for(int milliseconds) { kr32::sleep_for(milliseconds); } inline void sleep_until(int seconds) { kr32::sleep_until(seconds); } }\n"
                    "}\n");
                return;
            }
            if (header.find("kr32/runtime.hpp") != std::string::npos ||
                header.find("runtime.hpp") != std::string::npos) {
                append_line(
                    "void exit(int code);\n"
                    "void* malloc(int size);\n"
                    "void free(void* pointer);\n"
                    "void* memcpy(void* destination, const void* source, int count);\n"
                    "void* memset(void* destination, int value, int count);\n"
                    "int strlen(const char* text);\n");
                return;
            }
            if (header.find("<new>") != std::string::npos ||
                header.find("new>") != std::string::npos) {
                // std::bad_alloc is synthesized by codegen; std::nothrow is
                // recognized directly by the new-expression parser.
                return;
            }
            if (header.find("kr32/matrix.hpp") != std::string::npos ||
                header.find("matrix.hpp") != std::string::npos) {
                append_line(
                    "namespace kr32 {\n"
                    "struct vec4 { float x; float y; float z; float w; };\n"
                    "struct mat4 { float m[16]; };\n"
                    "mat4 matrix_mul(const mat4& a, const mat4& b);\n"
                    "mat4 matrix_mul_add(const mat4& a, const mat4& b, const mat4& c);\n"
                    "mat4 transpose(const mat4& a);\n"
                    "vec4 transform(const mat4& m, const vec4& v);\n"
                    "float dot(const vec4& a, const vec4& b);\n"
                    "void push(const mat4& m);\n"
                    "void pop(mat4& m);\n"
                    "void drop();\n"
                    "}\n");
                return;
            }
            if (header.find("unistd.h") != std::string::npos) {
                append_line(
                    "extern \"C\" {\n"
                    "int access(const char* path, int mode);\n"
                    "int isatty(int descriptor);\n"
                    "int truncate(const char* path, int length);\n"
                    "int fsync(int descriptor);\n"
                    "int fdatasync(int descriptor);\n"
                    "char* getcwd(char* buffer, int size);\n"
                    "int chdir(const char* path);\n"
                    "int dup(int descriptor);\n"
                    "int dup2(int source, int target);\n"
                    "}\n");
                macros_["F_OK"] = Macro{false, {}, "0"};
                macros_["X_OK"] = Macro{false, {}, "1"};
                macros_["W_OK"] = Macro{false, {}, "2"};
                macros_["R_OK"] = Macro{false, {}, "4"};
                return;
            }
            if (header.find("sys/stat.h") != std::string::npos) {
                append_line(
                    "struct stat { unsigned int st_size; unsigned int st_mode; "
                    "unsigned int st_mtime; unsigned int st_reserved; };\n"
                    "extern \"C\" {\n"
                    "int stat(const char* path, struct stat* status);\n"
                    "int fstat(int descriptor, struct stat* status);\n"
                    "int remove(const char* path);\n"
                    "int rename(const char* old_path, const char* new_path);\n"
                    "}\n");
                macros_["S_IFMT"] = Macro{false, {}, "0xF000"};
                macros_["S_IFREG"] = Macro{false, {}, "0x8000"};
                macros_["S_IFDIR"] = Macro{false, {}, "0x4000"};
                return;
            }
            if (header.find("errno.h") != std::string::npos ||
                header.find("cerrno") != std::string::npos) {
                append_line(
                    "extern \"C\" {\n"
                    "extern int errno;\n"
                    "const char* strerror(int code);\n"
                    "void perror(const char* prefix);\n"
                    "}\n");
                macros_["EPERM"] = Macro{false, {}, "1"};
                macros_["ENOENT"] = Macro{false, {}, "2"};
                macros_["EIO"] = Macro{false, {}, "5"};
                macros_["EBADF"] = Macro{false, {}, "9"};
                macros_["ENOMEM"] = Macro{false, {}, "12"};
                macros_["EACCES"] = Macro{false, {}, "13"};
                macros_["EEXIST"] = Macro{false, {}, "17"};
                macros_["EINVAL"] = Macro{false, {}, "22"};
                macros_["ENOSPC"] = Macro{false, {}, "28"};
                macros_["ENOSYS"] = Macro{false, {}, "38"};
                return;
            }
            if (header.find("<string>") != std::string::npos) {
                append_line(
                    "extern \"C\" {\n"
                    "void* malloc(int size);\n"
                    "void* realloc(void* memory, int size);\n"
                    "void free(void* memory);\n"
                    "int snprintf(char* buffer, int size, const char* format, ...);\n"
                    "}\n"
                    "namespace std {\n"
                    "class string {\n"
                    "    char* data_;\n"
                    "    int size_;\n"
                    "    void assign(const char* text, int length) {\n"
                    "        char* next = (char*)malloc(length + 1);\n"
                    "        if (next == 0) return;\n"
                    "        int index = 0;\n"
                    "        while (index < length) { next[index] = text[index]; index = index + 1; }\n"
                    "        next[length] = 0;\n"
                    "        if (data_ != 0) free(data_);\n"
                    "        data_ = next;\n"
                    "        size_ = length;\n"
                    "    }\n"
                    "public:\n"
                    "    string() { data_ = (char*)malloc(1); size_ = 0; if (data_ != 0) data_[0] = 0; }\n"
                    "    string(const char* text) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign(text != 0 ? text : \"\", length);\n"
                    "    }\n"
                    "    string(const string& other) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        assign(other.data_ != 0 ? other.data_ : \"\", other.size_);\n"
                    "    }\n"
                    "    ~string() { if (data_ != 0) free(data_); }\n"
                    "    const char* c_str() const { return data_ != 0 ? data_ : \"\"; }\n"
                    "    int size() const { return size_; }\n"
                    "    int length() const { return size_; }\n"
                    "    bool empty() const { return size_ == 0; }\n"
                    "    char& operator[](int index) { return data_[index]; }\n"
                    "    char operator[](int index) const { return data_[index]; }\n"
                    "    string& operator=(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign(text != 0 ? text : \"\", length);\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& operator+=(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        char* next = (char*)realloc(data_, size_ + length + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        data_ = next;\n"
                    "        int index = 0;\n"
                    "        while (index < length) { data_[size_ + index] = text[index]; index = index + 1; }\n"
                    "        size_ = size_ + length;\n"
                    "        data_[size_] = 0;\n"
                    "        return *this;\n"
                    "    }\n"

                    "    char& front() { return data_[0]; }\n"
                    "    char& back() { return data_[size_ - 1]; }\n"
                    "    int find(const char* text) const {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        if (length == 0) return 0;\n"
                    "        int index = 0;\n"
                    "        while (index + length <= size_) {\n"
                    "            int matched = 1;\n"
                    "            int offset = 0;\n"
                    "            while (offset < length) {\n"
                    "                if (data_[index + offset] != text[offset]) { matched = 0; break; }\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            if (matched) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int rfind(const char* text) const {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        if (length == 0) return size_;\n"
                    "        int index = size_ - length;\n"
                    "        while (index >= 0) {\n"
                    "            int matched = 1;\n"
                    "            int offset = 0;\n"
                    "            while (offset < length) {\n"
                    "                if (data_[index + offset] != text[offset]) { matched = 0; break; }\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            if (matched) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int compare(const char* text) const {\n"
                    "        int index = 0;\n"
                    "        while (data_[index] != 0 && text[index] != 0) {\n"
                    "            if (data_[index] < text[index]) return -1;\n"
                    "            if (data_[index] > text[index]) return 1;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        if (data_[index] < text[index]) return -1;\n"
                    "        if (data_[index] > text[index]) return 1;\n"
                    "        return 0;\n"
                    "    }\n"
                    "    string substr(int position, int count) const {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > size_) position = size_;\n"
                    "        int length = count;\n"
                    "        if (length < 0) length = 0;\n"
                    "        if (position + length > size_) length = size_ - position;\n"
                    "        char* buffer = (char*)malloc(length + 1);\n"
                    "        int index = 0;\n"
                    "        while (index < length) { buffer[index] = data_[position + index]; index = index + 1; }\n"
                    "        buffer[length] = 0;\n"
                    "        string result(buffer);\n"
                    "        free(buffer);\n"
                    "        return result;\n"
                    "    }\n"
                    "    string& operator+=(char value) {\n"
                    "        char* next = (char*)realloc(data_, size_ + 2);\n"
                    "        if (next == 0) return *this;\n"
                    "        data_ = next;\n"
                    "        data_[size_] = value;\n"
                    "        size_ = size_ + 1;\n"
                    "        data_[size_] = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    void set(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign(text != 0 ? text : \"\", length);\n"
                    "    }\n"
                    "    void append(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        char* next = (char*)realloc(data_, size_ + length + 1);\n"
                    "        if (next == 0) return;\n"
                    "        data_ = next;\n"
                    "        int index = 0;\n"
                    "        while (index < length) { data_[size_ + index] = text[index]; index = index + 1; }\n"
                    "        size_ = size_ + length;\n"
                    "        data_[size_] = 0;\n"
                    "    }\n"
                    "    void resize(int count) {\n"
                    "        if (count < 0) return;\n"
                    "        char* next = (char*)realloc(data_, count + 1);\n"
                    "        if (next == 0) return;\n"
                    "        data_ = next;\n"
                    "        if (count > size_) {\n"
                    "            int index = size_;\n"
                    "            while (index < count) { data_[index] = 0; index = index + 1; }\n"
                    "        }\n"
                    "        size_ = count;\n"
                    "        data_[size_] = 0;\n"
                    "    }\n"
                    "    void clear() { size_ = 0; if (data_ != 0) data_[0] = 0; }\n"
                    "};\n"
                    "string operator+(const string& left, const string& right) {\n"
                    "    string result(left);\n"
                    "    result.append(right.c_str());\n"
                    "    return result;\n"
                    "}\n"
                    "bool operator==(const string& left, const string& right) {\n"
                    "    if (left.size() != right.size()) return false;\n"
                    "    int index = 0;\n"
                    "    while (index < left.size()) {\n"
                    "        if (left.c_str()[index] != right.c_str()[index]) return false;\n"
                    "        index = index + 1;\n"
                    "    }\n"
                    "    return true;\n"
                    "}\n"
                    "bool operator!=(const string& left, const string& right) {\n"
                    "    return !(left == right);\n"
                    "}\n"
                    "bool operator==(const string& left, const char* right) {\n"
                    "    int index = 0;\n"
                    "    while (left.c_str()[index] != 0 && right[index] != 0) {\n"
                    "        if (left.c_str()[index] != right[index]) return false;\n"
                    "        index = index + 1;\n"
                    "    }\n"
                    "    return left.c_str()[index] == right[index];\n"
                    "}\n"
                    "bool operator!=(const string& left, const char* right) {\n"
                    "    return !(left == right);\n"
                    "}\n"
                    "bool operator==(const char* left, const string& right) {\n"
                    "    return right == left;\n"
                    "}\n"
                    "bool operator!=(const char* left, const string& right) {\n"
                    "    return !(right == left);\n"
                    "}\n"
                    "string to_string(int value) {\n"
                    "    char buffer[32];\n"
                    "    snprintf(buffer, 32, \"%d\", value);\n"
                    "    return string(buffer);\n"
                    "}\n"
                    "string to_string(unsigned int value) {\n"
                    "    char buffer[32];\n"
                    "    snprintf(buffer, 32, \"%u\", value);\n"
                    "    return string(buffer);\n"
                    "}\n"
                    "string to_string(double value) {\n"
                    "    char buffer[64];\n"
                    "    snprintf(buffer, 64, \"%g\", value);\n"
                    "    return string(buffer);\n"
                    "}\n"
                    "}\n");
                return;
            }
            if (header.find("fstream") != std::string::npos ||
                header.find("iostream") != std::string::npos) {
                append_line(
                    "struct kr32_file { int descriptor; int flags; int ungot; int owns_descriptor; const char* memory; int memory_position; int memory_mode; unsigned char* buffer; int buffer_capacity; int buffer_length; int buffer_mode; int buffer_owned; unsigned char* input_buffer; int input_capacity; int input_begin; int input_end; int input_owned; };\n"
                    "typedef struct kr32_file FILE;\n"
                    "typedef int fpos_t;\n"
                    "extern \"C\" {\n"
                    "extern FILE* stdin;\n"
                    "extern FILE* stdout;\n"
                    "extern FILE* stderr;\n"
                    "FILE* fopen(const char* path, const char* mode);\n"
                    "int fclose(FILE* stream);\n"
                    "int fflush(FILE* stream);\n"
                    "int fgetc(FILE* stream);\n"
                    "int fputc(int c, FILE* stream);\n"
                    "char* fgets(char* buffer, int size, FILE* stream);\n"
                    "int fputs(const char* text, FILE* stream);\n"
                    "int fread(void* buffer, int size, int count, FILE* stream);\n"
                    "int fwrite(const void* buffer, int size, int count, FILE* stream);\n"
                    "int fseek(FILE* stream, int offset, int whence);\n"
                    "int ftell(FILE* stream);\n"
                    "int feof(FILE* stream);\n"
                    "int ferror(FILE* stream);\n"
                    "int fscanf(FILE* stream, const char* format, ...);\n"
                    "int scanf(const char* format, ...);\n"
                    "int sscanf(const char* text, const char* format, ...);\n"
                    "int getline(char** line, int* capacity, FILE* stream);\n"
                    "int getdelim(char** line, int* capacity, int delimiter, FILE* stream);\n"
                    "int fprintf(FILE* stream, const char* format, ...);\n"
                    "int snprintf(char* buffer, int size, const char* format, ...);\n"
                    "}\n"
                    "namespace std {\n"
                    "struct ios { enum openmode { in = 1, out = 2, binary = 4, app = 8, trunc = 16 }; };\n"
                    "inline int __kr32_has_bit(int value, int bit) {\n"
                    "    int divisor = 1; int index = 0;\n"
                    "    while (index < bit) { divisor = divisor * 2; index = index + 1; }\n"
                    "    return (value / divisor) % 2;\n"
                    "}\n"
                    "inline const char* __kr32_open_mode(int mode) {\n"
                    "    if (__kr32_has_bit(mode, 3)) return \"a+\";\n"
                    "    if (__kr32_has_bit(mode, 4)) return __kr32_has_bit(mode, 0) ? \"w+\" : \"w\";\n"
                    "    if (__kr32_has_bit(mode, 0)) return __kr32_has_bit(mode, 1) ? \"r+\" : \"r\";\n"
                    "    return \"w\";\n"
                    "}\n"
                    "class ostream {\n"
                    "protected: FILE* file_;\n"
                    "    int base_;\n"
                    "    int width_;\n"
                    "    int precision_;\n"
                    "public:\n"
                    "    ostream() : file_(0), base_(10), width_(0), precision_(6) {}\n"
                    "    explicit ostream(FILE* file) : file_(file), base_(10), width_(0), precision_(6) {}\n"
                    "    ostream& hex() { base_ = 16; return *this; }\n"
                    "    ostream& dec() { base_ = 10; return *this; }\n"
                    "    ostream& oct() { base_ = 8; return *this; }\n"
                    "    ostream& width(int value) { width_ = value; return *this; }\n"
                    "    ostream& precision(int value) { precision_ = value; return *this; }\n"
                    "    bool is_open() const { return file_ != 0; }\n"
                    "    bool good() const { return file_ != 0; }\n"
                    "    bool fail() const { return file_ == 0; }\n"
                    "    ostream& put(char value) { if (file_ != 0) fputc((int)value, file_); return *this; }\n"
                    "    ostream& write(const char* text, int count) { if (file_ != 0) fwrite(text, 1, count, file_); return *this; }\n"
                    "    ostream& flush() { if (file_ != 0) fflush(file_); return *this; }\n"
                    "    ostream& operator<<(const char* text) {\n"
                    "        if (file_ != 0) {\n"
                    "            int length = 0;\n"
                    "            if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "            if (text == 0) text = \"(null)\";\n"
                    "            int pad = width_ > length ? width_ - length : 0;\n"
                    "            while (pad > 0) { fputc(' ', file_); pad = pad - 1; }\n"
                    "            fputs(text, file_);\n"
                    "        }\n"
                    "        width_ = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    ostream& operator<<(char value) { return put(value); }\n"
                    "    ostream& operator<<(int value) {\n"
                    "        char b[32];\n"
                    "        if (base_ == 16) snprintf(b, 32, \"%*x\", width_, value);\n"
                    "        else if (base_ == 8) snprintf(b, 32, \"%*o\", width_, value);\n"
                    "        else snprintf(b, 32, \"%*d\", width_, value);\n"
                    "        width_ = 0;\n"
                    "        return (*this) << b;\n"
                    "    }\n"
                    "    ostream& operator<<(unsigned int value) {\n"
                    "        char b[32];\n"
                    "        if (base_ == 16) snprintf(b, 32, \"%*x\", width_, value);\n"
                    "        else if (base_ == 8) snprintf(b, 32, \"%*o\", width_, value);\n"
                    "        else snprintf(b, 32, \"%*u\", width_, value);\n"
                    "        width_ = 0;\n"
                    "        return (*this) << b;\n"
                    "    }\n"
                    "    ostream& operator<<(double value) {\n"
                    "        char b[64]; snprintf(b, 64, \"%*.*g\", width_, precision_, value);\n"
                    "        width_ = 0;\n"
                    "        return (*this) << b;\n"
                    "    }\n"
                    "    int tellp() { return file_ != 0 ? ftell(file_) : -1; }\n"
                    "    ostream& seekp(int offset) { if (file_ != 0) fseek(file_, offset, 0); return *this; }\n"
                    "};\n"
                    "struct hex_t {};\n"
                    "struct dec_t {};\n"
                    "struct oct_t {};\n"
                    "static const hex_t hex;\n"
                    "static const dec_t dec;\n"
                    "static const oct_t oct;\n"
                    "ostream& operator<<(ostream& out, const hex_t&) { return out.hex(); }\n"
                    "ostream& operator<<(ostream& out, const dec_t&) { return out.dec(); }\n"
                    "ostream& operator<<(ostream& out, const oct_t&) { return out.oct(); }\n"
                    "class istream {\n"
                    "protected: FILE* file_;\n"
                    "public:\n"
                    "    istream() : file_(0) {}\n"
                    "    explicit istream(FILE* file) : file_(file) {}\n"
                    "    bool is_open() const { return file_ != 0; }\n"
                    "    bool good() const { return file_ != 0 && feof(file_) == 0 && ferror(file_) == 0; }\n"
                    "    bool eof() const { return file_ != 0 && feof(file_) != 0; }\n"
                    "    bool fail() const { return !good(); }\n"
                    "    int get() { return file_ != 0 ? fgetc(file_) : -1; }\n"
                    "    istream& get(char& value) { int c = get(); if (c >= 0) value = (char)c; return *this; }\n"
                    "    istream& read(char* buffer, int count) { if (file_ != 0) fread(buffer, 1, count, file_); return *this; }\n"
                    "    istream& getline(char* buffer, int size) {\n"
                    "        if (file_ != 0 && fgets(buffer, size, file_) != 0) {\n"
                    "            int i = 0;\n"
                    "            while (buffer[i] != 0) { if (buffer[i] == 10) { buffer[i] = 0; break; } i = i + 1; }\n"
                    "        }\n"
                    "        return *this;\n"
                    "    }\n"
                    "    istream& operator>>(int& value) { if (file_ != 0) fscanf(file_, \"%d\", &value); return *this; }\n"
                    "    istream& operator>>(unsigned int& value) { if (file_ != 0) fscanf(file_, \"%u\", &value); return *this; }\n"
                    "    istream& operator>>(double& value) { if (file_ != 0) fscanf(file_, \"%lf\", &value); return *this; }\n"
                    "    istream& operator>>(char* buffer) { if (file_ != 0) fscanf(file_, \"%s\", buffer); return *this; }\n"
                    "    int tellg() { return file_ != 0 ? ftell(file_) : -1; }\n"
                    "    istream& seekg(int offset) { if (file_ != 0) fseek(file_, offset, 0); return *this; }\n"
                    "};\n"
                    "class ifstream : public istream {\n"
                    "public:\n"
                    "    ifstream() {}\n"
                    "    explicit ifstream(const char* path) { open(path); }\n"
                    "    ~ifstream() { close(); }\n"
                    "    bool open(const char* path) { close(); file_ = fopen(path, \"r\"); return file_ != 0; }\n"
                    "    void close() { if (file_ != 0) { fclose(file_); file_ = 0; } }\n"
                    "};\n"
                    "class ofstream : public ostream {\n"
                    "public:\n"
                    "    ofstream() {}\n"
                    "    explicit ofstream(const char* path) { open(path); }\n"
                    "    ~ofstream() { close(); }\n"
                    "    bool open(const char* path) { return open(path, 2); }\n"
                    "    bool open(const char* path, int mode) { close(); file_ = fopen(path, __kr32_open_mode(mode)); return file_ != 0; }\n"
                    "    void close() { if (file_ != 0) { fclose(file_); file_ = 0; } }\n"
                    "};\n"
                    "class fstream : public ifstream {\n"
                    "public:\n"
                    "    fstream() {}\n"
                    "    explicit fstream(const char* path) { open(path); }\n"
                    "    fstream(const char* path, int mode) { open(path, mode); }\n"
                    "    bool open(const char* path) { return open(path, 3); }\n"
                    "    bool open(const char* path, int mode) { close(); file_ = fopen(path, __kr32_open_mode(mode)); return file_ != 0; }\n"
                    "    fstream& put(char value) { if (file_ != 0) fputc((int)value, file_); return *this; }\n"
                    "    fstream& write(const char* text, int count) { if (file_ != 0) fwrite(text, 1, count, file_); return *this; }\n"
                    "    fstream& operator<<(const char* text) { if (file_ != 0) fputs(text, file_); return *this; }\n"
                    "    fstream& operator<<(char value) { return put(value); }\n"
                    "    fstream& operator<<(int value) { char b[32]; snprintf(b, 32, \"%d\", value); return (*this) << b; }\n"
                    "    fstream& operator<<(unsigned int value) { char b[32]; snprintf(b, 32, \"%u\", value); return (*this) << b; }\n"
                    "    fstream& operator<<(double value) { char b[64]; snprintf(b, 64, \"%g\", value); return (*this) << b; }\n"
                    "};\n"
                    "static ostream cout(stdout);\n"
                    "static istream cin(stdin);\n"
                    "static ostream cerr(stderr);\n"
                    "static const char endl = '\\n';\n"
                    "}\n");
                macros_["EOF"] = Macro{false, {}, "-1"};
                return;
            }
            if (header.find("stdio.h") != std::string::npos ||
                header.find("cstdio") != std::string::npos) {
                append_line(
                    "struct kr32_file { int descriptor; int flags; int ungot; int owns_descriptor; const char* memory; int memory_position; int memory_mode; unsigned char* buffer; int buffer_capacity; int buffer_length; int buffer_mode; int buffer_owned; unsigned char* input_buffer; int input_capacity; int input_begin; int input_end; int input_owned; };\n"
                    "typedef struct kr32_file FILE;\n"
                    "typedef int fpos_t;\n"
                    "extern \"C\" {\n"
                    "extern FILE* stdin;\n"
                    "extern FILE* stdout;\n"
                    "extern FILE* stderr;\n"
                    "int putchar(int c);\n"
                    "int getchar();\n"
                    "int printf(const char* format, ...);\n"
                    "FILE* fopen(const char* path, const char* mode);\n"
                    "FILE* fdopen(int fd, const char* mode);\n"
                    "FILE* freopen(const char* path, const char* mode, FILE* stream);\n"
                    "int fclose(FILE* stream);\n"
                    "int fflush(FILE* stream);\n"
                    "int fgetc(FILE* stream);\n"
                    "int fputc(int c, FILE* stream);\n"
                    "int ungetc(int c, FILE* stream);\n"
                    "char* fgets(char* buffer, int size, FILE* stream);\n"
                    "int fputs(const char* text, FILE* stream);\n"
                    "int puts(const char* text);\n"
                    "int fread(void* buffer, int size, int count, FILE* stream);\n"
                    "int fwrite(const void* buffer, int size, int count, FILE* stream);\n"
                    "int fseek(FILE* stream, int offset, int whence);\n"
                    "int ftell(FILE* stream);\n"
                    "void rewind(FILE* stream);\n"
                    "int fgetpos(FILE* stream, fpos_t* position);\n"
                    "int fsetpos(FILE* stream, const fpos_t* position);\n"
                    "int setvbuf(FILE* stream, char* buffer, int mode, int size);\n"
                    "void setbuf(FILE* stream, char* buffer);\n"
                    "int feof(FILE* stream);\n"
                    "int ferror(FILE* stream);\n"
                    "void clearerr(FILE* stream);\n"
                    "int fileno(FILE* stream);\n"
                    "int fprintf(FILE* stream, const char* format, ...);\n"
                    "int fscanf(FILE* stream, const char* format, ...);\n"
                    "int scanf(const char* format, ...);\n"
                    "int sscanf(const char* text, const char* format, ...);\n"
                    "int getline(char** line, int* capacity, FILE* stream);\n"
                    "int getdelim(char** line, int* capacity, int delimiter, FILE* stream);\n"
                    "int remove(const char* path);\n"
                    "int rename(const char* old_path, const char* new_path);\n"
                    "FILE* tmpfile();\n"
                    "int snprintf(char* buffer, int size, const char* format, ...);\n"
                    "int sprintf(char* buffer, const char* format, ...);\n"
                    "}\n");
                macros_["EOF"] = Macro{false, {}, "-1"};
                macros_["SEEK_SET"] = Macro{false, {}, "0"};
                macros_["SEEK_CUR"] = Macro{false, {}, "1"};
                macros_["SEEK_END"] = Macro{false, {}, "2"};
                macros_["_IOFBF"] = Macro{false, {}, "0"};
                macros_["_IOLBF"] = Macro{false, {}, "1"};
                macros_["_IONBF"] = Macro{false, {}, "2"};
                macros_["BUFSIZ"] = Macro{false, {}, "1024"};
                return;
            }
            if (header.find("stdlib.h") != std::string::npos) {
                append_line(
                    "extern \"C\" {\n"
                    "void exit(int code);\n"
                    "void* malloc(int size);\n"
                    "void free(void* pointer);\n"
                    "void* calloc(int count, int size);\n"
                    "void* realloc(void* pointer, int size);\n"
                    "int _heapwalk(void);\n"
                    "int sys_random();\n"
                    "int sys_file_dialog(int mode, int flags, char* path, int capacity);\n"
                    "int sys_open_dialog(char* path, int capacity);\n"
                    "int sys_save_dialog(char* path, int capacity);\n"
                    "}\n");
                return;
            }
            if (header.find("string.h") != std::string::npos) {
                append_line(
                    "extern \"C\" {\n"
                    "int strlen(const char* text);\n"
                    "void* memcpy(void* destination, const void* source, int count);\n"
                    "void* memset(void* destination, int value, int count);\n"
                    "char* strcpy(char* destination, const char* source);\n"
                    "char* strncpy(char* destination, const char* source, int count);\n"
                    "char* strcat(char* destination, const char* source);\n"
                    "char* strncat(char* destination, const char* source, int count);\n"
                    "int strcmp(const char* left, const char* right);\n"
                    "int strncmp(const char* left, const char* right, int count);\n"
                    "char* strchr(const char* text, int character);\n"
                    "char* strrchr(const char* text, int character);\n"
                    "char* strstr(const char* text, const char* needle);\n"
                    "char* strtok(char* text, const char* delimiters);\n"
                    "char* strdup(const char* text);\n"
                    "void* memmove(void* destination, const void* source, int count);\n"
                    "int memcmp(const void* left, const void* right, int count);\n"
                    "void* memchr(const void* memory, int character, int count);\n"
                    "}\n");
                return;
            }
            if (header.find("stddef.h") != std::string::npos) {
                append_line("typedef unsigned int size_t;\n");
                return;
            }
            if (header.find("math.h") != std::string::npos) {
                append_line(
                    "extern \"C\" {\n"
                    "double sqrt(double value);\n"
                    "float sqrtf(float value);\n"
                    "double fabs(double value);\n"
                    "float fabsf(float value);\n"
                    "double fmin(double left, double right);\n"
                    "float fminf(float left, float right);\n"
                    "double fmax(double left, double right);\n"
                    "float fmaxf(float left, float right);\n"
                    "}\n");
                return;
            }
            error(line_number, 1,
                  "freestanding C++ header set is not implemented yet: " + header);
            return;
        }
        if (directive == "error") {
            error(line_number, 1, std::string(trim(rest)));
            return;
        }
        if (directive == "pragma" || directive == "line") {
            return;
        }
        if (directive.empty()) {
            return;
        }
        error(line_number, 1, "unknown preprocessor directive: #" + directive);
    }

    void start_conditional(bool condition, int line_number) {
        (void)line_number;
        const bool parent = active();
        conditions_.push_back({parent, condition && parent, condition && parent, false});
    }

    void define_macro(std::string_view rest, int line_number) {
        rest = trim(rest);
        if (rest.empty()) {
            error(line_number, 1, "missing macro name");
            return;
        }
        std::size_t index = 0;
        while (index < rest.size() && identifier_continue(rest[index])) {
            ++index;
        }
        const std::string name(rest.substr(0, index));
        if (name.empty() || !identifier_start(name[0])) {
            error(line_number, 1, "invalid macro name");
            return;
        }
        Macro macro;
        if (index < rest.size() && rest[index] == '(') {
            macro.function_like = true;
            ++index;
            std::string parameter;
            int depth = 1;
            while (index < rest.size() && depth > 0) {
                const char c = rest[index++];
                if (c == '(') {
                    ++depth;
                    parameter.push_back(c);
                } else if (c == ')') {
                    --depth;
                    if (depth == 0) {
                        const std::string_view trimmed = trim(parameter);
                        if (!trimmed.empty()) {
                            macro.parameters.push_back(std::string(trimmed));
                        }
                        parameter.clear();
                    } else {
                        parameter.push_back(c);
                    }
                } else if (c == ',' && depth == 1) {
                    const std::string_view trimmed = trim(parameter);
                    if (!trimmed.empty()) {
                        macro.parameters.push_back(std::string(trimmed));
                    }
                    parameter.clear();
                } else {
                    parameter.push_back(c);
                }
            }
            if (depth != 0) {
                error(line_number, 1, "unterminated macro parameter list");
            }
            rest = trim(rest.substr(index));
        }
        if (!macro.function_like) {
            macro.body = std::string(trim(rest.substr(index)));
        } else {
            macro.body = std::string(rest);
        }
        macros_[name] = std::move(macro);
    }

    bool evaluate_condition(std::string text, int line_number) {
        // Resolve `defined` before macro expansion so a macro named in
        // `defined(X)` is not expanded.
        std::string resolved;
        for (std::size_t index = 0; index < text.size();) {
            if (text.compare(index, 7, "defined") == 0 &&
                (index == 0 || !identifier_continue(text[index - 1]))) {
                std::size_t cursor = index + 7;
                while (cursor < text.size() &&
                       std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                    ++cursor;
                }
                bool parenthesized = false;
                if (cursor < text.size() && text[cursor] == '(') {
                    parenthesized = true;
                    ++cursor;
                    while (cursor < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                        ++cursor;
                    }
                }
                const std::size_t name_start = cursor;
                while (cursor < text.size() && identifier_continue(text[cursor])) {
                    ++cursor;
                }
                const std::string name = text.substr(name_start, cursor - name_start);
                if (parenthesized) {
                    while (cursor < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                        ++cursor;
                    }
                    if (cursor < text.size() && text[cursor] == ')') {
                        ++cursor;
                    }
                }
                resolved += macros_.find(name) != macros_.end() ? " 1 " : " 0 ";
                index = cursor;
                continue;
            }
            resolved.push_back(text[index++]);
        }
        resolved = expand(resolved, line_number, 0);
        IfExpression expression(resolved, source_name_, diagnostics_, line_number);
        return expression.evaluate() != 0;
    }

    std::string expand(const std::string& text, int line_number, int depth) {
        if (depth > 64) {
            error(line_number, 1, "macro expansion too deep");
            return text;
        }
        std::string result;
        std::size_t index = 0;
        while (index < text.size()) {
            const char c = text[index];
            if (c == '"' || c == '\'') {
                const char quote = c;
                result.push_back(c);
                ++index;
                while (index < text.size()) {
                    const char ch = text[index++];
                    result.push_back(ch);
                    if (ch == '\\' && index < text.size()) {
                        result.push_back(text[index++]);
                    } else if (ch == quote) {
                        break;
                    }
                }
                continue;
            }
            if (!identifier_start(c)) {
                result.push_back(c);
                ++index;
                continue;
            }
            const std::size_t start = index;
            while (index < text.size() && identifier_continue(text[index])) {
                ++index;
            }
            const std::string name = text.substr(start, index - start);
            if (name == "__LINE__") {
                result += std::to_string(line_number);
                continue;
            }
            if (name == "__FILE__") {
                result += "\"" + std::string(source_name_) + "\"";
                continue;
            }
            const auto macro = macros_.find(name);
            if (macro == macros_.end() || expanding_.find(name) != expanding_.end()) {
                result += name;
                continue;
            }
            if (macro->second.function_like) {
                std::size_t cursor = index;
                while (cursor < text.size() &&
                       std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                    ++cursor;
                }
                if (cursor >= text.size() || text[cursor] != '(') {
                    result += name;
                    continue;
                }
                ++cursor;
                std::vector<std::string> arguments;
                std::string argument;
                int parens = 1;
                bool closed = false;
                while (cursor < text.size()) {
                    const char ch = text[cursor++];
                    if (ch == '(') {
                        ++parens;
                        argument.push_back(ch);
                    } else if (ch == ')') {
                        --parens;
                        if (parens == 0) {
                            closed = true;
                            break;
                        }
                        argument.push_back(ch);
                    } else if (ch == ',' && parens == 1) {
                        arguments.push_back(std::string(trim(argument)));
                        argument.clear();
                    } else if (ch == '"' || ch == '\'') {
                        const char quote = ch;
                        argument.push_back(ch);
                        while (cursor < text.size()) {
                            const char inner = text[cursor++];
                            argument.push_back(inner);
                            if (inner == '\\' && cursor < text.size()) {
                                argument.push_back(text[cursor++]);
                            } else if (inner == quote) {
                                break;
                            }
                        }
                    } else {
                        argument.push_back(ch);
                    }
                }
                if (!closed) {
                    error(line_number, 1, "unterminated macro invocation");
                }
                const std::string_view trimmed = trim(argument);
                if (!trimmed.empty() || !macro->second.parameters.empty()) {
                    arguments.push_back(std::string(trimmed));
                }
                std::string body = macro->second.body;
                for (std::size_t parameter = 0;
                     parameter < macro->second.parameters.size(); ++parameter) {
                    const std::string& parameter_name = macro->second.parameters[parameter];
                    const std::string value =
                        parameter < arguments.size() ? arguments[parameter] : "";
                    body = substitute(body, parameter_name, value);
                }
                index = cursor;
                expanding_.insert(name);
                result += expand(body, line_number, depth + 1);
                expanding_.erase(name);
                continue;
            }
            expanding_.insert(name);
            result += expand(macro->second.body, line_number, depth + 1);
            expanding_.erase(name);
        }
        return result;
    }

    static std::string substitute(const std::string& body, const std::string& parameter,
                                  const std::string& value) {
        std::string result;
        std::size_t index = 0;
        while (index < body.size()) {
            if (!identifier_start(body[index])) {
                result.push_back(body[index++]);
                continue;
            }
            const std::size_t start = index;
            while (index < body.size() && identifier_continue(body[index])) {
                ++index;
            }
            const std::string name = body.substr(start, index - start);
            result += name == parameter ? value : name;
        }
        return result;
    }

    struct Conditional {
        bool parent_active = true;
        bool active = true;
        bool taken = false;
        bool saw_else = false;
    };

    std::string_view source_name_;
    std::vector<Diagnostic>& diagnostics_;
    std::string source_;
    std::string result_;
    std::map<std::string, Macro> macros_;
    std::set<std::string> expanding_;
    std::vector<Conditional> conditions_;
    bool thread_header_injected_ = false;
    bool initializer_list_header_injected_ = false;
};

} // namespace

std::string preprocess(std::string_view source, std::string_view source_name,
                       std::vector<Diagnostic>& diagnostics) {
    Preprocessor preprocessor(source, source_name, diagnostics);
    return preprocessor.run();
}

} // namespace kr32::cxx
