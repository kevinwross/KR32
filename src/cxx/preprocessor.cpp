#include "cxx/preprocessor.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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

std::string two_digit(int value) {
    std::string text = std::to_string(value);
    if (text.size() < 2) {
        text.insert(text.begin(), '0');
    }
    return text;
}

std::tm local_time_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

std::string compile_date_text() {
    static const char* const months[] = {"Jan", "Feb", "Mar", "Apr",
                                         "May", "Jun", "Jul", "Aug",
                                         "Sep", "Oct", "Nov", "Dec"};
    const std::tm local = local_time_now();
    const int month = local.tm_mon >= 0 && local.tm_mon < 12 ? local.tm_mon : 0;
    std::string day = std::to_string(local.tm_mday);
    if (day.size() < 2) {
        day.insert(day.begin(), ' ');
    }
    return std::string(months[month]) + " " + day + " " +
           std::to_string(local.tm_year + 1900);
}

std::string compile_time_text() {
    const std::tm local = local_time_now();
    return two_digit(local.tm_hour) + ":" + two_digit(local.tm_min) + ":" +
           two_digit(local.tm_sec);
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
            if (header.find("<utility>") != std::string::npos) {
                if (!utility_header_injected_) {
                    utility_header_injected_ = true;
                    append_line(
                        "namespace std {\n"
                        "inline void swap(int& left, int& right) {\n"
                        "    int temporary = left; left = right; right = temporary;\n"
                        "}\n"
                        "inline void swap(unsigned int& left, unsigned int& right) {\n"
                        "    unsigned int temporary = left;\n"
                        "    left = right; right = temporary;\n"
                        "}\n"
                        "inline void swap(char& left, char& right) {\n"
                        "    char temporary = left; left = right; right = temporary;\n"
                        "}\n"
                        "inline void swap(float& left, float& right) {\n"
                        "    float temporary = left; left = right; right = temporary;\n"
                        "}\n"
                        "inline void swap(double& left, double& right) {\n"
                        "    double temporary = left; left = right; right = temporary;\n"
                        "}\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<algorithm>") != std::string::npos) {
                if (!algorithm_header_injected_) {
                    algorithm_header_injected_ = true;
                    append_line(
                        "namespace std {\n"
                        "inline int min(int left, int right) {\n"
                        "    return right < left ? right : left;\n"
                        "}\n"
                        "inline int max(int left, int right) {\n"
                        "    return left < right ? right : left;\n"
                        "}\n"
                        "inline unsigned int min(unsigned int left, unsigned int right) {\n"
                        "    return right < left ? right : left;\n"
                        "}\n"
                        "inline unsigned int max(unsigned int left, unsigned int right) {\n"
                        "    return left < right ? right : left;\n"
                        "}\n"
                        "inline char min(char left, char right) {\n"
                        "    return right < left ? right : left;\n"
                        "}\n"
                        "inline char max(char left, char right) {\n"
                        "    return left < right ? right : left;\n"
                        "}\n"
                        "inline float min(float left, float right) {\n"
                        "    return right < left ? right : left;\n"
                        "}\n"
                        "inline float max(float left, float right) {\n"
                        "    return left < right ? right : left;\n"
                        "}\n"
                        "inline double min(double left, double right) {\n"
                        "    return right < left ? right : left;\n"
                        "}\n"
                        "inline double max(double left, double right) {\n"
                        "    return left < right ? right : left;\n"
                        "}\n"
                        "inline int clamp(int value, int low, int high) {\n"
                        "    if (value < low) return low;\n"
                        "    if (high < value) return high;\n"
                        "    return value;\n"
                        "}\n"
                        "inline unsigned int clamp(unsigned int value, unsigned int low,\n"
                        "                          unsigned int high) {\n"
                        "    if (value < low) return low;\n"
                        "    if (high < value) return high;\n"
                        "    return value;\n"
                        "}\n"
                        "inline char clamp(char value, char low, char high) {\n"
                        "    if (value < low) return low;\n"
                        "    if (high < value) return high;\n"
                        "    return value;\n"
                        "}\n"
                        "inline float clamp(float value, float low, float high) {\n"
                        "    if (value < low) return low;\n"
                        "    if (high < value) return high;\n"
                        "    return value;\n"
                        "}\n"
                        "inline double clamp(double value, double low, double high) {\n"
                        "    if (value < low) return low;\n"
                        "    if (high < value) return high;\n"
                        "    return value;\n"
                        "}\n"
                        "inline bool equal(int* first, int* last, int* other) {\n"
                        "    while (first < last) {\n"
                        "        if (*first != *other) return false;\n"
                        "        first = first + 1;\n"
                        "        other = other + 1;\n"
                        "    }\n"
                        "    return true;\n"
                        "}\n"
                        "inline bool equal(unsigned int* first, unsigned int* last,\n"
                        "                  unsigned int* other) {\n"
                        "    while (first < last) {\n"
                        "        if (*first != *other) return false;\n"
                        "        first = first + 1;\n"
                        "        other = other + 1;\n"
                        "    }\n"
                        "    return true;\n"
                        "}\n"
                        "inline bool equal(char* first, char* last, char* other) {\n"
                        "    while (first < last) {\n"
                        "        if (*first != *other) return false;\n"
                        "        first = first + 1;\n"
                        "        other = other + 1;\n"
                        "    }\n"
                        "    return true;\n"
                        "}\n"
                        "inline bool equal(float* first, float* last, float* other) {\n"
                        "    while (first < last) {\n"
                        "        if (*first != *other) return false;\n"
                        "        first = first + 1;\n"
                        "        other = other + 1;\n"
                        "    }\n"
                        "    return true;\n"
                        "}\n"
                        "inline bool equal(double* first, double* last, double* other) {\n"
                        "    while (first < last) {\n"
                        "        if (*first != *other) return false;\n"
                        "        first = first + 1;\n"
                        "        other = other + 1;\n"
                        "    }\n"
                        "    return true;\n"
                        "}\n"
                        "inline int count(int* first, int* last, int value) {\n"
                        "    int total = 0;\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) total = total + 1;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return total;\n"
                        "}\n"
                        "inline int count(unsigned int* first, unsigned int* last,\n"
                        "                 unsigned int value) {\n"
                        "    int total = 0;\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) total = total + 1;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return total;\n"
                        "}\n"
                        "inline int count(char* first, char* last, char value) {\n"
                        "    int total = 0;\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) total = total + 1;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return total;\n"
                        "}\n"
                        "inline int count(float* first, float* last, float value) {\n"
                        "    int total = 0;\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) total = total + 1;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return total;\n"
                        "}\n"
                        "inline int count(double* first, double* last, double value) {\n"
                        "    int total = 0;\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) total = total + 1;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return total;\n"
                        "}\n"
                        "inline int* copy(int* first, int* last, int* output) {\n"
                        "    while (first < last) {\n"
                        "        *output = *first;\n"
                        "        first = first + 1;\n"
                        "        output = output + 1;\n"
                        "    }\n"
                        "    return output;\n"
                        "}\n"
                        "inline unsigned int* copy(unsigned int* first, unsigned int* last,\n"
                        "                          unsigned int* output) {\n"
                        "    while (first < last) {\n"
                        "        *output = *first;\n"
                        "        first = first + 1;\n"
                        "        output = output + 1;\n"
                        "    }\n"
                        "    return output;\n"
                        "}\n"
                        "inline char* copy(char* first, char* last, char* output) {\n"
                        "    while (first < last) {\n"
                        "        *output = *first;\n"
                        "        first = first + 1;\n"
                        "        output = output + 1;\n"
                        "    }\n"
                        "    return output;\n"
                        "}\n"
                        "inline float* copy(float* first, float* last, float* output) {\n"
                        "    while (first < last) {\n"
                        "        *output = *first;\n"
                        "        first = first + 1;\n"
                        "        output = output + 1;\n"
                        "    }\n"
                        "    return output;\n"
                        "}\n"
                        "inline double* copy(double* first, double* last, double* output) {\n"
                        "    while (first < last) {\n"
                        "        *output = *first;\n"
                        "        first = first + 1;\n"
                        "        output = output + 1;\n"
                        "    }\n"
                        "    return output;\n"
                        "}\n"
                        "inline int* find(int* first, int* last, int value) {\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) return first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return last;\n"
                        "}\n"
                        "inline char* find(char* first, char* last, char value) {\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) return first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return last;\n"
                        "}\n"
                        "inline double* find(double* first, double* last, double value) {\n"
                        "    while (first < last) {\n"
                        "        if (*first == value) return first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return last;\n"
                        "}\n"
                        "inline int* fill_n(int* first, int count, int value) {\n"
                        "    while (count > 0) {\n"
                        "        *first = value;\n"
                        "        first = first + 1;\n"
                        "        count = count - 1;\n"
                        "    }\n"
                        "    return first;\n"
                        "}\n"
                        "inline unsigned int* fill_n(unsigned int* first, int count,\n"
                        "                            unsigned int value) {\n"
                        "    while (count > 0) {\n"
                        "        *first = value;\n"
                        "        first = first + 1;\n"
                        "        count = count - 1;\n"
                        "    }\n"
                        "    return first;\n"
                        "}\n"
                        "inline char* fill_n(char* first, int count, char value) {\n"
                        "    while (count > 0) {\n"
                        "        *first = value;\n"
                        "        first = first + 1;\n"
                        "        count = count - 1;\n"
                        "    }\n"
                        "    return first;\n"
                        "}\n"
                        "inline float* fill_n(float* first, int count, float value) {\n"
                        "    while (count > 0) {\n"
                        "        *first = value;\n"
                        "        first = first + 1;\n"
                        "        count = count - 1;\n"
                        "    }\n"
                        "    return first;\n"
                        "}\n"
                        "inline double* fill_n(double* first, int count, double value) {\n"
                        "    while (count > 0) {\n"
                        "        *first = value;\n"
                        "        first = first + 1;\n"
                        "        count = count - 1;\n"
                        "    }\n"
                        "    return first;\n"
                        "}\n"
                        "inline void fill(int* first, int* last, int value) {\n"
                        "    while (first < last) { *first = value; first = first + 1; }\n"
                        "}\n"
                        "inline void fill(unsigned int* first, unsigned int* last,\n"
                        "                 unsigned int value) {\n"
                        "    while (first < last) { *first = value; first = first + 1; }\n"
                        "}\n"
                        "inline void fill(char* first, char* last, char value) {\n"
                        "    while (first < last) { *first = value; first = first + 1; }\n"
                        "}\n"
                        "inline void fill(float* first, float* last, float value) {\n"
                        "    while (first < last) { *first = value; first = first + 1; }\n"
                        "}\n"
                        "inline void fill(double* first, double* last, double value) {\n"
                        "    while (first < last) { *first = value; first = first + 1; }\n"
                        "}\n"
                        "inline void sort(int* first, int* last) {\n"
                        "    int* current = first;\n"
                        "    if (current != last) current = current + 1;\n"
                        "    while (current < last) {\n"
                        "        int value = *current;\n"
                        "        int* hole = current;\n"
                        "        while (hole > first && value < *(hole - 1)) {\n"
                        "            *hole = *(hole - 1);\n"
                        "            hole = hole - 1;\n"
                        "        }\n"
                        "        *hole = value;\n"
                        "        current = current + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void sort(unsigned int* first, unsigned int* last) {\n"
                        "    unsigned int* current = first;\n"
                        "    if (current != last) current = current + 1;\n"
                        "    while (current < last) {\n"
                        "        unsigned int value = *current;\n"
                        "        unsigned int* hole = current;\n"
                        "        while (hole > first && value < *(hole - 1)) {\n"
                        "            *hole = *(hole - 1);\n"
                        "            hole = hole - 1;\n"
                        "        }\n"
                        "        *hole = value;\n"
                        "        current = current + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void sort(char* first, char* last) {\n"
                        "    char* current = first;\n"
                        "    if (current != last) current = current + 1;\n"
                        "    while (current < last) {\n"
                        "        char value = *current;\n"
                        "        char* hole = current;\n"
                        "        while (hole > first && value < *(hole - 1)) {\n"
                        "            *hole = *(hole - 1);\n"
                        "            hole = hole - 1;\n"
                        "        }\n"
                        "        *hole = value;\n"
                        "        current = current + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void sort(float* first, float* last) {\n"
                        "    float* current = first;\n"
                        "    if (current != last) current = current + 1;\n"
                        "    while (current < last) {\n"
                        "        float value = *current;\n"
                        "        float* hole = current;\n"
                        "        while (hole > first && value < *(hole - 1)) {\n"
                        "            *hole = *(hole - 1);\n"
                        "            hole = hole - 1;\n"
                        "        }\n"
                        "        *hole = value;\n"
                        "        current = current + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void sort(double* first, double* last) {\n"
                        "    double* current = first;\n"
                        "    if (current != last) current = current + 1;\n"
                        "    while (current < last) {\n"
                        "        double value = *current;\n"
                        "        double* hole = current;\n"
                        "        while (hole > first && value < *(hole - 1)) {\n"
                        "            *hole = *(hole - 1);\n"
                        "            hole = hole - 1;\n"
                        "        }\n"
                        "        *hole = value;\n"
                        "        current = current + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void reverse(int* first, int* last) {\n"
                        "    while (first < last) {\n"
                        "        last = last - 1;\n"
                        "        int value = *first;\n"
                        "        *first = *last;\n"
                        "        *last = value;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void reverse(unsigned int* first, unsigned int* last) {\n"
                        "    while (first < last) {\n"
                        "        last = last - 1;\n"
                        "        unsigned int value = *first;\n"
                        "        *first = *last;\n"
                        "        *last = value;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void reverse(char* first, char* last) {\n"
                        "    while (first < last) {\n"
                        "        last = last - 1;\n"
                        "        char value = *first;\n"
                        "        *first = *last;\n"
                        "        *last = value;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void reverse(float* first, float* last) {\n"
                        "    while (first < last) {\n"
                        "        last = last - 1;\n"
                        "        float value = *first;\n"
                        "        *first = *last;\n"
                        "        *last = value;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "}\n"
                        "inline void reverse(double* first, double* last) {\n"
                        "    while (first < last) {\n"
                        "        last = last - 1;\n"
                        "        double value = *first;\n"
                        "        *first = *last;\n"
                        "        *last = value;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "}\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cmath>") != std::string::npos) {
                if (!cmath_header_injected_) {
                    cmath_header_injected_ = true;
                    append_line(
                        "extern \"C\" {\n"
                        "double sqrt(double value);\n"
                        "float sqrtf(float value);\n"
                        "double fabs(double value);\n"
                        "float fabsf(float value);\n"
                        "}\n"
                        "namespace std {\n"
                        "inline int abs(int value) {\n"
                        "    return value < 0 ? -value : value;\n"
                        "}\n"
                        "inline float abs(float value) {\n"
                        "    return value < 0 ? -value : value;\n"
                        "}\n"
                        "inline double abs(double value) {\n"
                        "    return value < 0 ? -value : value;\n"
                        "}\n"
                        "inline double sqrt(double value) { return ::sqrt(value); }\n"
                        "inline float sqrt(float value) { return ::sqrtf(value); }\n"
                        "inline double fabs(double value) { return ::fabs(value); }\n"
                        "inline float fabs(float value) { return ::fabsf(value); }\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<numeric>") != std::string::npos) {
                if (!numeric_header_injected_) {
                    numeric_header_injected_ = true;
                    append_line(
                        "namespace std {\n"
                        "inline int accumulate(int* first, int* last, int initial) {\n"
                        "    while (first < last) {\n"
                        "        initial = initial + *first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return initial;\n"
                        "}\n"
                        "inline unsigned int accumulate(unsigned int* first,\n"
                        "                               unsigned int* last,\n"
                        "                               unsigned int initial) {\n"
                        "    while (first < last) {\n"
                        "        initial = initial + *first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return initial;\n"
                        "}\n"
                        "inline double accumulate(double* first, double* last,\n"
                        "                         double initial) {\n"
                        "    while (first < last) {\n"
                        "        initial = initial + *first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return initial;\n"
                        "}\n"
                        "inline float accumulate(float* first, float* last,\n"
                        "                        float initial) {\n"
                        "    while (first < last) {\n"
                        "        initial = initial + *first;\n"
                        "        first = first + 1;\n"
                        "    }\n"
                        "    return initial;\n"
                        "}\n"
                        "inline void iota(int* first, int* last, int value) {\n"
                        "    while (first < last) {\n"
                        "        *first = value;\n"
                        "        first = first + 1;\n"
                        "        value = value + 1;\n"
                        "    }\n"
                        "}\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cstdlib>") != std::string::npos) {
                if (!cstdlib_header_injected_) {
                    cstdlib_header_injected_ = true;
                    append_line(
                        "extern \"C\" {\n"
                        "int atoi(const char* text);\n"
                        "double atof(const char* text);\n"
                        "long strtol(const char* text, char** end, int base);\n"
                        "unsigned long strtoul(const char* text, char** end, int base);\n"
                        "long long strtoll(const char* text, char** end, int base);\n"
                        "unsigned long long strtoull(const char* text, char** end, int base);\n"
                        "double strtod(const char* text, char** end);\n"
                        "float strtof(const char* text, char** end);\n"
                        "long double strtold(const char* text, char** end);\n"
                        "long labs(long value);\n"
                        "long long llabs(long long value);\n"
                        "void qsort(void* base, int count, int size,\n"
                        "           int (*compare)(const void*, const void*));\n"
                        "void* bsearch(const void* key, const void* base, int count,\n"
                        "              int size, int (*compare)(const void*, const void*));\n"
                        "void* malloc(int size);\n"
                        "void* calloc(int count, int size);\n"
                        "void* realloc(void* memory, int size);\n"
                        "void free(void* memory);\n"
                        "void exit(int status);\n"
                        "void abort();\n"
                        "}\n"
                        "namespace std {\n"
                        "inline int atoi(const char* text) { return ::atoi(text); }\n"
                        "inline double atof(const char* text) { return ::atof(text); }\n"
                        "using ::strtol;\n"
                        "using ::strtoul;\n"
                        "using ::strtoll;\n"
                        "using ::strtoull;\n"
                        "using ::strtod;\n"
                        "using ::strtof;\n"
                        "using ::strtold;\n"
                        "using ::labs;\n"
                        "using ::llabs;\n"
                        "using ::qsort;\n"
                        "using ::bsearch;\n"

                        "using ::malloc;\n"
                        "using ::calloc;\n"
                        "using ::realloc;\n"
                        "using ::free;\n"
                        "using ::exit;\n"
                        "using ::abort;\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cassert>") != std::string::npos) {
                if (!cassert_header_injected_) {
                    cassert_header_injected_ = true;
                    append_line(
                        "extern \"C\" int __kr32_assert_fail(void);\n");
                    Macro macro;
                    macro.function_like = true;
                    macro.parameters = {"condition"};
                    macro.body =
                        "((condition) ? 0 : __kr32_assert_fail())";
                    macros_["assert"] = std::move(macro);
                }
                return;
            }
            if (header.find("<ctime>") != std::string::npos) {
                if (!ctime_header_injected_) {
                    ctime_header_injected_ = true;
                    append_line(
                        "typedef int time_t;\n"
                        "extern \"C\" {\n"
                        "time_t time(time_t* result);\n"
                        "double difftime(double later, double earlier);\n"
                        "}\n"
                        "namespace std {\n"
                        "typedef ::time_t time_t;\n"
                        "using ::time;\n"
                        "using ::difftime;\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cfloat>") != std::string::npos) {
                if (!cfloat_header_injected_) {
                    cfloat_header_injected_ = true;
                    auto define_constant = [this](const char* name,
                                                  const char* value) {
                        Macro macro;
                        macro.body = value;
                        macros_[name] = std::move(macro);
                    };
                    define_constant("FLT_MAX", "3.402823466e+38");
                    define_constant("FLT_MIN", "1.175494351e-38");
                    define_constant("FLT_EPSILON", "1.192092896e-07");
                    define_constant("FLT_DIG", "6");
                    define_constant("FLT_MANT_DIG", "24");
                    define_constant("DBL_MAX", "1.7976931348623158e+308");
                    define_constant("DBL_MIN", "2.2250738585072014e-308");
                    define_constant("DBL_EPSILON", "2.2204460492503131e-16");
                    define_constant("DBL_DIG", "15");
                    define_constant("DBL_MANT_DIG", "53");
                }
                return;
            }
            if (header.find("<climits>") != std::string::npos) {
                if (!climits_header_injected_) {
                    climits_header_injected_ = true;
                    auto define_constant = [this](const char* name,
                                                  const char* value) {
                        Macro macro;
                        macro.body = value;
                        macros_[name] = std::move(macro);
                    };
                    define_constant("CHAR_BIT", "8");
                    define_constant("SCHAR_MIN", "-128");
                    define_constant("SCHAR_MAX", "127");
                    define_constant("UCHAR_MAX", "255");
                    define_constant("SHRT_MIN", "-32768");
                    define_constant("SHRT_MAX", "32767");
                    define_constant("USHRT_MAX", "65535");
                    define_constant("INT_MIN", "(-2147483647 - 1)");
                    define_constant("INT_MAX", "2147483647");
                    define_constant("UINT_MAX", "4294967295");
                    define_constant("LONG_MIN", "(-2147483647 - 1)");
                    define_constant("LONG_MAX", "2147483647");
                    define_constant("ULONG_MAX", "4294967295");
                }
                return;
            }
            if (header.find("<cstdint>") != std::string::npos) {
                if (!cstdint_header_injected_) {
                    cstdint_header_injected_ = true;
                    append_line(
                        "namespace std {\n"
                        "typedef signed char int8_t;\n"
                        "typedef unsigned char uint8_t;\n"
                        "typedef short int16_t;\n"
                        "typedef unsigned short uint16_t;\n"
                        "typedef int int32_t;\n"
                        "typedef unsigned int uint32_t;\n"
                        "typedef long long int64_t;\n"
                        "typedef unsigned long long uint64_t;\n"
                        "typedef int intptr_t;\n"
                        "typedef unsigned int uintptr_t;\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cstddef>") != std::string::npos) {
                if (!cstddef_header_injected_) {
                    cstddef_header_injected_ = true;
                    append_line(
                        "namespace std {\n"
                        "typedef unsigned int size_t;\n"
                        "typedef int ptrdiff_t;\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cctype>") != std::string::npos) {
                if (!cctype_header_injected_) {
                    cctype_header_injected_ = true;
                    append_line(
                        "extern \"C\" {\n"
                        "int isdigit(int character);\n"
                        "int isalpha(int character);\n"
                        "int isalnum(int character);\n"
                        "int isspace(int character);\n"
                        "int isupper(int character);\n"
                        "int islower(int character);\n"
                        "int toupper(int character);\n"
                        "int tolower(int character);\n"
                        "}\n"
                        "namespace std {\n"
                        "inline int isdigit(int character) { return ::isdigit(character); }\n"
                        "inline int isalpha(int character) { return ::isalpha(character); }\n"
                        "inline int isalnum(int character) { return ::isalnum(character); }\n"
                        "inline int isspace(int character) { return ::isspace(character); }\n"
                        "inline int isupper(int character) { return ::isupper(character); }\n"
                        "inline int islower(int character) { return ::islower(character); }\n"
                        "inline int toupper(int character) { return ::toupper(character); }\n"
                        "inline int tolower(int character) { return ::tolower(character); }\n"
                        "}\n");
                }
                return;
            }
            if (header.find("<cstring>") != std::string::npos) {
                if (!cstring_header_injected_) {
                    cstring_header_injected_ = true;
                    append_line(
                        "extern \"C\" {\n"
                        "int strlen(const char* text);\n"
                        "void* memcpy(void* destination, const void* source, int count);\n"
                        "void* memset(void* destination, int value, int count);\n"
                        "char* strcpy(char* destination, const char* source);\n"
                        "char* strncpy(char* destination, const char* source, int count);\n"
                        "char* strcat(char* destination, const char* source);\n"
                        "int strcmp(const char* left, const char* right);\n"
                        "int strncmp(const char* left, const char* right, int count);\n"
                        "char* strchr(const char* text, int character);\n"
                        "char* strstr(const char* text, const char* needle);\n"
                        "char* strncat(char* destination, const char* source, int count);\n"
                        "char* strrchr(const char* text, int character);\n"
                        "char* strtok(char* text, const char* delimiters);\n"
                        "char* strdup(const char* text);\n"
                        "void* memmove(void* destination, const void* source, int count);\n"
                        "void* memchr(const void* memory, int character, int count);\n"
                        "int memcmp(const void* left, const void* right, int count);\n"
                        "}\n"
                        "namespace std {\n"
                        "inline int strlen(const char* text) { return ::strlen(text); }\n"
                        "inline void* memcpy(void* destination, const void* source, int count) {\n"
                        "    return ::memcpy(destination, source, count);\n"
                        "}\n"
                        "inline void* memset(void* destination, int value, int count) {\n"
                        "    return ::memset(destination, value, count);\n"
                        "}\n"
                        "inline char* strcpy(char* destination, const char* source) {\n"
                        "    return ::strcpy(destination, source);\n"
                        "}\n"
                        "inline char* strncpy(char* destination, const char* source, int count) {\n"
                        "    return ::strncpy(destination, source, count);\n"
                        "}\n"
                        "inline char* strcat(char* destination, const char* source) {\n"
                        "    return ::strcat(destination, source);\n"
                        "}\n"
                        "inline int strcmp(const char* left, const char* right) {\n"
                        "    return ::strcmp(left, right);\n"
                        "}\n"
                        "inline int strncmp(const char* left, const char* right, int count) {\n"
                        "    return ::strncmp(left, right, count);\n"
                        "}\n"
                        "inline char* strchr(const char* text, int character) {\n"
                        "    return ::strchr(text, character);\n"
                        "}\n"
                        "inline char* strstr(const char* text, const char* needle) {\n"
                        "    return ::strstr(text, needle);\n"
                        "}\n"
                        "inline char* strncat(char* destination, const char* source, int count) {\n"
                        "    return ::strncat(destination, source, count);\n"
                        "}\n"
                        "inline char* strrchr(const char* text, int character) {\n"
                        "    return ::strrchr(text, character);\n"
                        "}\n"
                        "inline char* strtok(char* text, const char* delimiters) {\n"
                        "    return ::strtok(text, delimiters);\n"
                        "}\n"
                        "inline char* strdup(const char* text) { return ::strdup(text); }\n"
                        "inline void* memmove(void* destination, const void* source, int count) {\n"
                        "    return ::memmove(destination, source, count);\n"
                        "}\n"
                        "inline void* memchr(const void* memory, int character, int count) {\n"
                        "    return ::memchr(memory, character, count);\n"
                        "}\n"
                        "inline int memcmp(const void* left, const void* right, int count) {\n"
                        "    return ::memcmp(left, right, count);\n"
                        "}\n"
                        "}\n");
                }
                return;
            }
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
                    "}\n"
                    "namespace std {\n"
                    "using ::errno;\n"
                    "using ::strerror;\n"
                    "using ::perror;\n"
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
                    "double strtod(const char* text, char** end);\n"
                    "}\n"
                    "namespace std {\n"
                    "class string {\n"
                    "    char* data_;\n"
                    "    int size_;\n"
                    "    int capacity_;\n"
                    "    void assign_text(const char* text, int length) {\n"
                    "        char* next = (char*)malloc(length + 1);\n"
                    "        if (next == 0) return;\n"
                    "        int index = 0;\n"
                    "        while (index < length) { next[index] = text[index]; index = index + 1; }\n"
                    "        next[length] = 0;\n"
                    "        if (data_ != 0) free(data_);\n"
                    "        data_ = next;\n"
                    "        size_ = length;\n"
                    "        capacity_ = length + 1;\n"
                    "    }\n"
                    "public:\n"
                    "    static const int npos = -1;\n"
                    "    string() { data_ = (char*)malloc(1); size_ = 0; capacity_ = 1; if (data_ != 0) data_[0] = 0; }\n"
                    "    string(const char* text) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign_text(text != 0 ? text : \"\", length);\n"
                    "    }\n"

                    "    string(const char* text, int count) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        if (count < 0) count = 0;\n"
                    "        assign_text(text != 0 ? text : \"\", count);\n"
                    "    }\n"
                    "    string(const char* text, int position, int count) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        if (text == 0) text = \"\";\n"
                    "        int length = 0;\n"
                    "        while (text[length] != 0) length = length + 1;\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > length) position = length;\n"
                    "        length = length - position;\n"
                    "        if (length > count) length = count;\n"
                    "        assign_text(text + position, length);\n"
                    "    }\n"
                    "    string(int count, char character) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        if (count < 0) count = 0;\n"
                    "        data_ = (char*)malloc(count + 1);\n"
                    "        if (data_ != 0) {\n"
                    "            int index = 0;\n"
                    "            while (index < count) {\n"
                    "                data_[index] = character;\n"
                    "                index = index + 1;\n"
                    "            }\n"
                    "            data_[count] = 0;\n"
                    "            size_ = count;\n"
                    "            capacity_ = count + 1;\n"
                    "        }\n"
                    "    }\n"
                    "    string(const string& other) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        assign_text(other.data_ != 0 ? other.data_ : \"\", other.size_);\n"
                    "    }\n"
                    "    string(const string& other, int position, int count) {\n"
                    "        data_ = 0; size_ = 0;\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > other.size_) position = other.size_;\n"
                    "        int length = other.size_ - position;\n"
                    "        if (length > count) length = count;\n"
                    "        assign_text(other.data_ + position, length);\n"
                    "    }\n"
                    "    ~string() { if (data_ != 0) free(data_); }\n"
                    "    const char* c_str() const { return data_ != 0 ? data_ : \"\"; }\n"
                    "    char* data() { return data_; }\n"
                    "    const char* data() const { return data_ != 0 ? data_ : \"\"; }\n"
                    "    int size() const { return size_; }\n"
                    "    int length() const { return size_; }\n"
                    "    bool empty() const { return size_ == 0; }\n"
                    "    string& assign(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign_text(text != 0 ? text : \"\", length);\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& assign(const char* text, int count) {\n"
                    "        if (count < 0) count = 0;\n"
                    "        assign_text(text != 0 ? text : \"\", count);\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& assign(int count, char value) {\n"
                    "        if (count < 0) count = 0;\n"
                    "        char* next = (char*)malloc(count + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        int index = 0;\n"
                    "        while (index < count) {\n"
                    "            next[index] = value;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        next[count] = 0;\n"
                    "        if (data_ != 0) free(data_);\n"
                    "        data_ = next;\n"
                    "        size_ = count;\n"
                    "        capacity_ = count + 1;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& assign(const std::string& other) {\n"
                    "        assign_text(other.data_ != 0 ? other.data_ : \"\",\n"
                    "                    other.size_);\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& assign(const std::string& other, int position,\n"
                    "                   int count) {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > other.size_) position = other.size_;\n"
                    "        int length = other.size_ - position;\n"
                    "        if (length > count) length = count;\n"
                    "        return assign(other.data_ + position, length);\n"
                    "    }\n"
                    "    string& assign(const char* text, int position, int count) {\n"
                    "        return assign(std::string(text), position, count);\n"
                    "    }\n"
                    "    char& operator[](int index) { return data_[index]; }\n"
                    "    char operator[](int index) const { return data_[index]; }\n"
                    "    char& at(int index) { return data_[index]; }\n"
                    "    char at(int index) const { return data_[index]; }\n"
                    "    string& operator=(const string& other) {\n"
                    "        if (this == &other) return *this;\n"
                    "        assign_text(other.data_ != 0 ? other.data_ : \"\", other.size_);\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& operator=(char value) {\n"
                    "        char* next = (char*)realloc(data_, 2);\n"
                    "        if (next == 0) return *this;\n"
                    "        data_ = next;\n"
                    "        data_[0] = value;\n"
                    "        data_[1] = 0;\n"
                    "        size_ = 1;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& operator=(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign_text(text != 0 ? text : \"\", length);\n"
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
                    "    const char& front() const { return data_[0]; }\n"
                    "    char& back() { return data_[size_ - 1]; }\n"
                    "    const char& back() const { return data_[size_ - 1]; }\n"
                    "    char* begin() { return data_; }\n"
                    "    char* end() { return data_ + size_; }\n"
                    "    const char* begin() const { return data_; }\n"
                    "    const char* end() const { return data_ + size_; }\n"
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
                    "    int find(const char* text, int start) const {\n"
                    "        if (start < 0) start = 0;\n"
                    "        if (start > size_) return -1;\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        if (length == 0) return start;\n"
                    "        int index = start;\n"
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
                    "    int find(const std::string& text, int start) const {\n"
                    "        return find(text.c_str(), start);\n"
                    "    }\n"
                    "    int find(const char* text, int start, int count) const {\n"
                    "        if (count < 0) count = 0;\n"
                    "        string piece;\n"
                    "        piece.assign(text, count);\n"
                    "        return find(piece.c_str(), start);\n"
                    "    }\n"
                    "    int find_first_of(const char* text, int start, int count) const {\n"
                    "        if (count < 0) count = 0;\n"
                    "        string piece;\n"
                    "        piece.assign(text, count);\n"
                    "        return find_first_of(piece.c_str(), start);\n"
                    "    }\n"
                    "    int find_last_of(const char* text, int start, int count) const {\n"
                    "        if (count < 0) count = 0;\n"
                    "        string piece;\n"
                    "        piece.assign(text, count);\n"
                    "        return find_last_of(piece.c_str(), start);\n"
                    "    }\n"
                    "    int find_first_not_of(const char* text, int start,\n"
                    "                          int count) const {\n"
                    "        if (count < 0) count = 0;\n"
                    "        string piece;\n"
                    "        piece.assign(text, count);\n"
                    "        return find_first_not_of(piece.c_str(), start);\n"
                    "    }\n"
                    "    int find_last_not_of(const char* text, int start,\n"
                    "                         int count) const {\n"
                    "        if (count < 0) count = 0;\n"
                    "        string piece;\n"
                    "        piece.assign(text, count);\n"
                    "        return find_last_not_of(piece.c_str(), start);\n"
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
                    "    int rfind(const char* text, int start, int count) const {\n"
                    "        if (count < 0) count = 0;\n"
                    "        string piece;\n"
                    "        piece.assign(text, count);\n"
                    "        return rfind(piece.c_str(), start);\n"
                    "    }\n"
                    "    int rfind(const char* text, int start) const {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        if (length == 0) return start < size_ ? start : size_;\n"
                    "        if (start > size_ - length) start = size_ - length;\n"
                    "        int index = start;\n"
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
                    "    bool operator<(const char* other) const { return compare(other) < 0; }\n"
                    "    bool operator<=(const char* other) const { return compare(other) <= 0; }\n"
                    "    bool operator>(const char* other) const { return compare(other) > 0; }\n"
                    "    bool operator>=(const char* other) const { return compare(other) >= 0; }\n"
                    "    bool starts_with(const char* prefix) const {\n"
                    "        int length = 0;\n"
                    "        while (prefix[length] != 0) length = length + 1;\n"
                    "        if (length > size_) return false;\n"
                    "        int index = 0;\n"
                    "        while (index < length) {\n"
                    "            if (data_[index] != prefix[index]) return false;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return true;\n"
                    "    }\n"
                    "    bool ends_with(const char* suffix) const {\n"
                    "        int length = 0;\n"
                    "        while (suffix[length] != 0) length = length + 1;\n"
                    "        if (length > size_) return false;\n"
                    "        int start = size_ - length;\n"
                    "        int index = 0;\n"
                    "        while (index < length) {\n"
                    "            if (data_[start + index] != suffix[index]) return false;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return true;\n"
                    "    }\n"
                    "    bool starts_with(const std::string& prefix) const {\n"
                    "        return starts_with(prefix.c_str());\n"
                    "    }\n"
                    "    bool ends_with(const std::string& suffix) const {\n"
                    "        return ends_with(suffix.c_str());\n"
                    "    }\n"
                    "    bool contains(const char* text) const {\n"
                    "        return find(text) != npos;\n"
                    "    }\n"
                    "    bool contains(const std::string& text) const {\n"
                    "        return find(text) != npos;\n"
                    "    }\n"
                    "    int find_first_of(const char* set) const {\n"
                    "        int index = 0;\n"
                    "        while (index < size_) {\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) return index;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_of(const char* set) const {\n"
                    "        int index = size_ - 1;\n"
                    "        while (index >= 0) {\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) return index;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_first_not_of(const char* set) const {\n"
                    "        int index = 0;\n"
                    "        while (index < size_) {\n"
                    "            bool found = false;\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) found = true;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            if (!found) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_not_of(const char* set) const {\n"
                    "        int index = size_ - 1;\n"
                    "        while (index >= 0) {\n"
                    "            bool found = false;\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) found = true;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            if (!found) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_first_not_of(char value) const {\n"
                    "        int index = 0;\n"
                    "        while (index < size_) {\n"
                    "            if (data_[index] != value) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_not_of(char value) const {\n"
                    "        int index = size_ - 1;\n"
                    "        while (index >= 0) {\n"
                    "            if (data_[index] != value) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_first_not_of(const char* set, int start) const {\n"
                    "        if (start < 0) start = 0;\n"
                    "        int index = start;\n"
                    "        while (index < size_) {\n"
                    "            bool found = false;\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) found = true;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            if (!found) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_not_of(const char* set, int start) const {\n"
                    "        if (start >= size_) start = size_ - 1;\n"
                    "        int index = start;\n"
                    "        while (index >= 0) {\n"
                    "            bool found = false;\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) found = true;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            if (!found) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_first_not_of(char value, int start) const {\n"
                    "        if (start < 0) start = 0;\n"
                    "        int index = start;\n"
                    "        while (index < size_) {\n"
                    "            if (data_[index] != value) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_not_of(char value, int start) const {\n"
                    "        if (start >= size_) start = size_ - 1;\n"
                    "        int index = start;\n"
                    "        while (index >= 0) {\n"
                    "            if (data_[index] != value) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_first_of(char value) const {\n"
                    "        return find_first_of(value, 0);\n"
                    "    }\n"
                    "    int find_first_of(char value, int start) const {\n"
                    "        if (start < 0) start = 0;\n"
                    "        int index = start;\n"
                    "        while (index < size_) {\n"
                    "            if (data_[index] == value) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_of(char value) const {\n"
                    "        return find_last_of(value, size_ - 1);\n"
                    "    }\n"
                    "    int find_last_of(char value, int start) const {\n"
                    "        if (start >= size_) start = size_ - 1;\n"
                    "        int index = start;\n"
                    "        while (index >= 0) {\n"
                    "            if (data_[index] == value) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_first_of(const char* set, int start) const {\n"
                    "        if (start < 0) start = 0;\n"
                    "        int index = start;\n"
                    "        while (index < size_) {\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) return index;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find_last_of(const char* set, int start) const {\n"
                    "        if (start >= size_) start = size_ - 1;\n"
                    "        int index = start;\n"
                    "        while (index >= 0) {\n"
                    "            int offset = 0;\n"
                    "            while (set[offset] != 0) {\n"
                    "                if (data_[index] == set[offset]) return index;\n"
                    "                offset = offset + 1;\n"
                    "            }\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find(const std::string& text) const { return find(text.c_str()); }\n"
                    "    int rfind(const std::string& text) const { return rfind(text.c_str()); }\n"
                    "    int rfind(const std::string& text, int start) const { return rfind(text.c_str(), start); }\n"
                    "    int find_first_of(const std::string& set, int start) const { return find_first_of(set.c_str(), start); }\n"
                    "    int find_last_of(const std::string& set, int start) const { return find_last_of(set.c_str(), start); }\n"
                    "    int find_first_not_of(const std::string& set) const { return find_first_not_of(set.c_str()); }\n"
                    "    int find_last_not_of(const std::string& set) const { return find_last_not_of(set.c_str()); }\n"
                    "    int find_first_not_of(const std::string& set, int start) const { return find_first_not_of(set.c_str(), start); }\n"
                    "    int find_last_not_of(const std::string& set, int start) const { return find_last_not_of(set.c_str(), start); }\n"
                    "    int compare(const std::string& other) const { return compare(other.c_str()); }\n"
                    "    int compare(int position, int count, const char* text) const {\n"
                    "        return substr(position, count).compare(text);\n"
                    "    }\n"
                    "    int compare(int position, int count, const std::string& text) const {\n"
                    "        return substr(position, count).compare(text.c_str());\n"
                    "    }\n"
                    "    int compare(int position, int count, const char* text, int text_count) const {\n"
                    "        string piece;\n"
                    "        piece.assign(text, text_count);\n"
                    "        return substr(position, count).compare(piece.c_str());\n"
                    "    }\n"
                    "    int compare(int position, int count, const std::string& text, int text_count) const {\n"
                    "        return compare(position, count, text.c_str(), text_count);\n"
                    "    }\n"
                    "    int compare(int position, int count, const char* text, int text_position, int text_count) const {\n"
                    "        string piece;\n"
                    "        piece.assign(text + text_position, text_count);\n"
                    "        return substr(position, count).compare(piece.c_str());\n"
                    "    }\n"
                    "    int compare(int position, int count, const std::string& text, int text_position, int text_count) const {\n"
                    "        return compare(position, count, text.c_str(), text_position, text_count);\n"
                    "    }\n"
                    "    int copy(char* destination, int count, int position = 0) const {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > size_) position = size_;\n"
                    "        int length = size_ - position;\n"
                    "        if (length > count) length = count;\n"
                    "        int index = 0;\n"
                    "        while (index < length) {\n"
                    "            destination[index] = data_[position + index];\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return length;\n"
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
                    "    string substr(int position) const {\n"
                    "        return substr(position, size_ - position);\n"
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
                    "    string& operator+=(const string& other) {\n"
                    "        int length = other.size_;\n"
                    "        char* next = (char*)realloc(data_, size_ + length + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        data_ = next;\n"
                    "        int index = 0;\n"
                    "        while (index < length) {\n"
                    "            data_[size_ + index] = other.data_[index];\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        size_ = size_ + length;\n"
                    "        data_[size_] = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& append(const std::string& other, int position,\n"
                    "                   int count) {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > other.size_) position = other.size_;\n"
                    "        int length = other.size_ - position;\n"
                    "        if (length > count) length = count;\n"
                    "        return append(other.data_ + position, length);\n"
                    "    }\n"
                    "    string& append(const char* text, int position, int count) {\n"
                    "        return append(std::string(text), position, count);\n"
                    "    }\n"
                    "    string& append(const std::string& other, int count) {\n"
                    "        int index = 0;\n"
                    "        while (index < count && index < other.size_) {\n"
                    "            push_back(other.data_[index]);\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& append(const std::string& other) {\n"
                    "        int length = other.size_;\n"
                    "        char* next = (char*)realloc(data_, size_ + length + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        data_ = next;\n"
                    "        int index = 0;\n"
                    "        while (index < length) {\n"
                    "            data_[size_ + index] = other.data_[index];\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        size_ = size_ + length;\n"
                    "        data_[size_] = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& append(int count, char value) {\n"
                    "        int index = 0;\n"
                    "        while (index < count) {\n"
                    "            push_back(value);\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& append(const char* text, int count) {\n"
                    "        if (count < 0) count = 0;\n"
                    "        char* next = (char*)realloc(data_, size_ + count + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        data_ = next;\n"
                    "        int index = 0;\n"
                    "        while (index < count) {\n"
                    "            data_[size_ + index] = text != 0 ? text[index] : 0;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        size_ = size_ + count;\n"
                    "        data_[size_] = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    void swap(std::string& other) {\n"
                    "        char* data = data_;\n"
                    "        int size = size_;\n"
                    "        data_ = other.data_;\n"
                    "        size_ = other.size_;\n"
                    "        other.data_ = data;\n"
                    "        other.size_ = size;\n"
                    "    }\n"
                    "    void set(const char* text) {\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        assign_text(text != 0 ? text : \"\", length);\n"
                    "    }\n"
                    "    string& append(const char* text) {\n"
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
                    "    string& insert(int position, const char* text) {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > size_) position = size_;\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        int next_size = size_ + length;\n"
                    "        char* next = (char*)malloc(next_size + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        int index = 0;\n"
                    "        while (index < position) { next[index] = data_[index]; index = index + 1; }\n"
                    "        int offset = 0;\n"
                    "        while (offset < length) { next[position + offset] = text[offset]; offset = offset + 1; }\n"
                    "        offset = 0;\n"
                    "        while (position + offset < size_) { next[position + length + offset] = data_[position + offset]; offset = offset + 1; }\n"
                    "        next[next_size] = 0;\n"
                    "        free(data_);\n"
                    "        data_ = next;\n"
                    "        size_ = next_size;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& insert(int position, const std::string& text) {\n"
                    "        return insert(position, text.c_str());\n"
                    "    }\n"
                    "    string& insert(int position, char value) {\n"
                    "        char buffer[2];\n"
                    "        buffer[0] = value;\n"
                    "        buffer[1] = 0;\n"
                    "        return insert(position, buffer);\n"
                    "    }\n"
                    "    string& insert(int position, int count, char value) {\n"
                    "        string piece;\n"
                    "        int index = 0;\n"
                    "        while (index < count) {\n"
                    "            piece.push_back(value);\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return insert(position, piece.c_str());\n"
                    "    }\n"
                    "    string& insert(int position, const char* text, int count) {\n"
                    "        string piece;\n"
                    "        piece.append(text, count);\n"
                    "        return insert(position, piece.c_str());\n"
                    "    }\n"
                    "    string& insert(int position, const std::string& text, int count) {\n"
                    "        return insert(position, text.c_str(), count);\n"
                    "    }\n"
                    "    string& insert(int position, const std::string& text,\n"
                    "                   int text_position, int count) {\n"
                    "        if (text_position < 0) text_position = 0;\n"
                    "        if (text_position > text.size_) text_position = text.size_;\n"
                    "        int length = text.size_ - text_position;\n"
                    "        if (length > count) length = count;\n"
                    "        return insert(position, text.data_ + text_position, length);\n"
                    "    }\n"
                    "    string& insert(int position, const char* text,\n"
                    "                   int text_position, int count) {\n"
                    "        return insert(position, std::string(text), text_position,\n"
                    "                      count);\n"
                    "    }\n"
                    "    string& erase() {\n"
                    "        size_ = 0;\n"
                    "        if (data_ != 0) data_[0] = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& erase(int position) {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > size_) position = size_;\n"
                    "        return erase(position, size_ - position);\n"
                    "    }\n"
                    "    string& erase(int position, int count) {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > size_) position = size_;\n"
                    "        if (count < 0) count = 0;\n"
                    "        if (position + count > size_) count = size_ - position;\n"
                    "        int index = position;\n"
                    "        while (index + count < size_) {\n"
                    "            data_[index] = data_[index + count];\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        size_ = size_ - count;\n"
                    "        data_[size_] = 0;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& replace(int position, int count, const char* text) {\n"
                    "        if (position < 0) position = 0;\n"
                    "        if (position > size_) position = size_;\n"
                    "        if (count < 0) count = 0;\n"
                    "        if (position + count > size_) count = size_ - position;\n"
                    "        int length = 0;\n"
                    "        if (text != 0) { while (text[length] != 0) length = length + 1; }\n"
                    "        int next_size = size_ - count + length;\n"
                    "        char* next = (char*)malloc(next_size + 1);\n"
                    "        if (next == 0) return *this;\n"
                    "        int index = 0;\n"
                    "        while (index < position) { next[index] = data_[index]; index = index + 1; }\n"
                    "        int offset = 0;\n"
                    "        while (offset < length) { next[position + offset] = text[offset]; offset = offset + 1; }\n"
                    "        int tail = position + count;\n"
                    "        offset = 0;\n"
                    "        while (tail + offset < size_) { next[position + length + offset] = data_[tail + offset]; offset = offset + 1; }\n"
                    "        next[next_size] = 0;\n"
                    "        free(data_);\n"
                    "        data_ = next;\n"
                    "        size_ = next_size;\n"
                    "        return *this;\n"
                    "    }\n"
                    "    string& replace(int position, int count, const std::string& text) {\n"
                    "        return replace(position, count, text.c_str());\n"
                    "    }\n"
                    "    string& replace(int position, int count, const char* text, int text_count) {\n"
                    "        string piece;\n"
                    "        piece.append(text, text_count);\n"
                    "        return replace(position, count, piece.c_str());\n"
                    "    }\n"
                    "    string& replace(int position, int count, const std::string& text, int text_count) {\n"
                    "        return replace(position, count, text.c_str(), text_count);\n"
                    "    }\n"
                    "    string& replace(int position, int count, const std::string& text,\n"
                    "                    int text_position, int text_count) {\n"
                    "        if (text_position < 0) text_position = 0;\n"
                    "        if (text_position > text.size_) text_position = text.size_;\n"
                    "        int length = text.size_ - text_position;\n"
                    "        if (length > text_count) length = text_count;\n"
                    "        return replace(position, count, text.data_ + text_position,\n"
                    "                       length);\n"
                    "    }\n"
                    "    string& replace(int position, int count, const char* text,\n"
                    "                    int text_position, int text_count) {\n"
                    "        return replace(position, count, std::string(text), text_position,\n"
                    "                       text_count);\n"
                    "    }\n"
                    "    string& replace(int position, int count, int new_count,\n"
                    "                    char value) {\n"
                    "        string piece(new_count, value);\n"
                    "        return replace(position, count, piece.c_str());\n"
                    "    }\n"
                    "    void push_back(char value) {\n"
                    "        char* next = (char*)realloc(data_, size_ + 2);\n"
                    "        if (next == 0) return;\n"
                    "        data_ = next;\n"
                    "        data_[size_] = value;\n"
                    "        size_ = size_ + 1;\n"
                    "        data_[size_] = 0;\n"
                    "    }\n"
                    "    void pop_back() {\n"
                    "        if (size_ == 0) return;\n"
                    "        size_ = size_ - 1;\n"
                    "        data_[size_] = 0;\n"
                    "    }\n"
                    "    int find(char value) const {\n"
                    "        int index = 0;\n"
                    "        while (index < size_) {\n"
                    "            if (data_[index] == value) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int find(char value, int start) const {\n"
                    "        if (start < 0) start = 0;\n"
                    "        int index = start;\n"
                    "        while (index < size_) {\n"
                    "            if (data_[index] == value) return index;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int rfind(char value) const {\n"
                    "        int index = size_ - 1;\n"
                    "        while (index >= 0) {\n"
                    "            if (data_[index] == value) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
                    "    }\n"
                    "    int rfind(char value, int start) const {\n"
                    "        if (start >= size_) start = size_ - 1;\n"
                    "        int index = start;\n"
                    "        while (index >= 0) {\n"
                    "            if (data_[index] == value) return index;\n"
                    "            index = index - 1;\n"
                    "        }\n"
                    "        return -1;\n"
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
                    "    void resize(int count, char value) {\n"
                    "        int previous = size_;\n"
                    "        resize(count);\n"
                    "        int index = previous;\n"
                    "        while (index < size_) {\n"
                    "            data_[index] = value;\n"
                    "            index = index + 1;\n"
                    "        }\n"
                    "    }\n"
                    "    void clear() { size_ = 0; if (data_ != 0) data_[0] = 0; }\n"
                    "    int capacity() const {\n"
                    "        return capacity_ > size_ ? capacity_ : size_ + 1;\n"
                    "    }\n"
                    "    void shrink_to_fit() {\n"
                    "        char* next = (char*)realloc(data_, size_ + 1);\n"
                    "        if (next != 0) { data_ = next; capacity_ = size_ + 1; }\n"
                    "    }\n"
                    "    void reserve(int amount) {\n"
                    "        if (amount < 0) amount = 0;\n"
                    "        if (amount + 1 <= capacity()) return;\n"
                    "        char* next = (char*)realloc(data_, amount + 1);\n"
                    "        if (next != 0) { data_ = next; capacity_ = amount + 1; }\n"
                    "    }\n"
                    "};\n"
                    "string operator+(const string& left, const string& right) {\n"
                    "    string result(left);\n"
                    "    result.append(right.c_str());\n"
                    "    return result;\n"
                    "}\n"
                    "string operator+(const string& left, const char* right) {\n"
                    "    string result(left);\n"
                    "    result += right;\n"
                    "    return result;\n"
                    "}\n"
                    "string operator+(const char* left, const string& right) {\n"
                    "    string result(left);\n"
                    "    result += right.c_str();\n"
                    "    return result;\n"
                    "}\n"
                    "string operator+(const string& left, char right) {\n"
                    "    string result(left);\n"
                    "    result += right;\n"
                    "    return result;\n"
                    "}\n"
                    "string operator+(char left, const string& right) {\n"
                    "    string result;\n"
                    "    result += left;\n"
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
                    "bool operator<(const string& left, const string& right) {\n"
                    "    return left.compare(right.c_str()) < 0;\n"
                    "}\n"
                    "bool operator<=(const string& left, const string& right) {\n"
                    "    return left.compare(right.c_str()) <= 0;\n"
                    "}\n"
                    "bool operator>(const string& left, const string& right) {\n"
                    "    return left.compare(right.c_str()) > 0;\n"
                    "}\n"
                    "bool operator>=(const string& left, const string& right) {\n"
                    "    return left.compare(right.c_str()) >= 0;\n"
                    "}\n"
                    "bool operator<(const string& left, const char* right) {\n"
                    "    return left.compare(right) < 0;\n"
                    "}\n"
                    "bool operator>(const string& left, const char* right) {\n"
                    "    return left.compare(right) > 0;\n"
                    "}\n"
                    "bool operator<(const char* left, const string& right) {\n"
                    "    return right.compare(left) > 0;\n"
                    "}\n"
                    "bool operator>(const char* left, const string& right) {\n"
                    "    return right.compare(left) < 0;\n"
                    "}\n"
                    "bool operator<=(const char* left, const string& right) {\n"
                    "    return right.compare(left) >= 0;\n"
                    "}\n"
                    "bool operator>=(const char* left, const string& right) {\n"
                    "    return right.compare(left) <= 0;\n"
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
                    "string to_string(long long value) {\n"
                    "    char buffer[32];\n"
                    "    snprintf(buffer, 32, \"%lld\", value);\n"
                    "    return string(buffer);\n"
                    "}\n"
                    "string to_string(unsigned long long value) {\n"
                    "    char buffer[32];\n"
                    "    snprintf(buffer, 32, \"%llu\", value);\n"
                    "    return string(buffer);\n"
                    "}\n"
                    "string to_string(double value) {\n"
                    "    char buffer[64];\n"
                    "    snprintf(buffer, 64, \"%g\", value);\n"
                    "    return string(buffer);\n"
                    "}\n"
                    "inline int stoi(const char* text) {\n"
                    "    int index = 0;\n"
                    "    while (text[index] == ' ' || text[index] == '\\t' ||\n"
                    "           text[index] == '\\n') {\n"
                    "        index = index + 1;\n"
                    "    }\n"
                    "    int negative = 0;\n"
                    "    if (text[index] == '-' || text[index] == '+') {\n"
                    "        negative = text[index] == '-';\n"
                    "        index = index + 1;\n"
                    "    }\n"
                    "    int value = 0;\n"
                    "    while (text[index] >= '0' && text[index] <= '9') {\n"
                    "        value = value * 10 + (text[index] - '0');\n"
                    "        index = index + 1;\n"
                    "    }\n"
                    "    return negative ? -value : value;\n"
                    "}\n"
                    "inline long stol(const char* text) { return (long)stoi(text); }\n"
                    "inline int stoi(const string& text) {\n"
                    "    return stoi(text.c_str());\n"
                    "}\n"
                    "inline long stol(const string& text) {\n"
                    "    return stol(text.c_str());\n"
                    "}\n"
                    "inline double stod(const char* text) {\n"
                    "    return strtod(text, (char**)0);\n"
                    "}\n"
                    "inline double stod(const string& text) {\n"
                    "    return stod(text.c_str());\n"
                    "}\n"
                    "inline float stof(const char* text) {\n"
                    "    return (float)stod(text);\n"
                    "}\n"
                    "inline float stof(const string& text) {\n"
                    "    return stof(text.c_str());\n"
                    "}\n"
                    "inline long double stold(const char* text) {\n"
                    "    return (long double)stod(text);\n"
                    "}\n"
                    "inline long double stold(const string& text) {\n"
                    "    return stold(text.c_str());\n"
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
                if (stdio_header_injected_) {
                    return;
                }
                stdio_header_injected_ = true;
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
                    "}\n"
                    "namespace std {\n"
                    "using FILE = kr32_file;\n"
                    "using fpos_t = int;\n"
                    "using ::stdin;\n"
                    "using ::stdout;\n"
                    "using ::stderr;\n"
                    "using ::printf;\n"
                    "using ::sprintf;\n"
                    "using ::snprintf;\n"
                    "using ::puts;\n"
                    "using ::putchar;\n"
                    "using ::getchar;\n"
                    "using ::fopen;\n"
                    "using ::fclose;\n"
                    "using ::fflush;\n"
                    "using ::fgetc;\n"
                    "using ::fputc;\n"
                    "using ::fgets;\n"
                    "using ::fputs;\n"
                    "using ::fread;\n"
                    "using ::fwrite;\n"
                    "using ::fseek;\n"
                    "using ::ftell;\n"
                    "using ::rewind;\n"
                    "using ::feof;\n"
                    "using ::ferror;\n"
                    "using ::remove;\n"
                    "using ::rename;\n"
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
        if (directive == "warning") {
            return;
        }
        if (directive == "line") {
            std::string_view text = trim(rest);
            int line = 0;
            std::size_t position = 0;
            while (position < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
                line = line * 10 + (text[position] - '0');
                ++position;
            }
            if (line > 0) {
                line_delta_ = line - (line_number + 1);
            }
            while (position < text.size() &&
                   std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                ++position;
            }
            if (position < text.size() && text[position] == '"') {
                ++position;
                std::string file;
                while (position < text.size() && text[position] != '"') {
                    file.push_back(text[position]);
                    ++position;
                }
                logical_file_ = std::move(file);
            }
            return;
        }
        if (directive == "pragma") {
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
                result += std::to_string(line_number + line_delta_);
                continue;
            }
            if (name == "__COUNTER__") {
                result += std::to_string(counter_++);
                continue;
            }
            if (name == "__FILE__") {
                result += "\"" +
                          (logical_file_.empty() ? std::string(source_name_)
                                                 : logical_file_) +
                          "\"";
                continue;
            }
            if (name == "__DATE__") {
                result += "\"" + compile_date_text() + "\"";
                continue;
            }
            if (name == "__TIME__") {
                result += "\"" + compile_time_text() + "\"";
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
    bool utility_header_injected_ = false;
    bool algorithm_header_injected_ = false;
    bool cstdlib_header_injected_ = false;
    bool numeric_header_injected_ = false;
    bool cstring_header_injected_ = false;
    bool cctype_header_injected_ = false;
    bool cstddef_header_injected_ = false;
    bool cstdint_header_injected_ = false;
    bool climits_header_injected_ = false;
    bool cfloat_header_injected_ = false;
    bool stdio_header_injected_ = false;
    bool ctime_header_injected_ = false;
    bool cassert_header_injected_ = false;
    bool cmath_header_injected_ = false;
    int counter_ = 0;
    int line_delta_ = 0;
    std::string logical_file_;
};

} // namespace

std::string preprocess(std::string_view source, std::string_view source_name,
                       std::vector<Diagnostic>& diagnostics) {
    Preprocessor preprocessor(source, source_name, diagnostics);
    return preprocessor.run();
}

} // namespace kr32::cxx
