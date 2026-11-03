#include "kr32/cc/cc.h"

#include "libc_runtime.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kr32::cc {
namespace {

struct CompileError : std::runtime_error {
    CompileError(int line, int column, std::string message)
        : std::runtime_error(std::move(message)), line(line), column(column) {}
    int line;
    int column;
};

std::string preprocess(std::string_view source) {
    std::string without_comments;
    without_comments.reserve(source.size());
    bool in_block = false;
    bool in_line = false;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char c = source[index];
        if (in_line) {
            if (c == '\n') {
                in_line = false;
                without_comments.push_back(c);
            }
            continue;
        }
        if (in_block) {
            if (c == '*' && index + 1 < source.size() && source[index + 1] == '/') {
                in_block = false;
                ++index;
            } else if (c == '\n') {
                without_comments.push_back(c);
            }
            continue;
        }
        if (in_string) {
            without_comments.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            without_comments.push_back(c);
        } else if (c == '/' && index + 1 < source.size() && source[index + 1] == '/') {
            in_line = true;
            ++index;
        } else if (c == '/' && index + 1 < source.size() && source[index + 1] == '*') {
            in_block = true;
            ++index;
        } else {
            without_comments.push_back(c);
        }
    }

    std::map<std::string, std::string> macros;
    struct FunctionMacro {
        std::vector<std::string> parameters;
        std::string body;
    };
    std::map<std::string, FunctionMacro> function_macros;
    struct Conditional {
        bool parent_active = true;
        bool condition = false;
    };
    std::vector<Conditional> conditions;
    bool active = true;

    std::function<std::string(const std::string&)> expand_line;
    expand_line = [&](const std::string& line) {
        std::string expanded;
        std::size_t index = 0;
        while (index < line.size()) {
            const char c = line[index];
            if (c == '"' || c == '\'') {
                const char quote = c;
                expanded.push_back(c);
                ++index;
                while (index < line.size()) {
                    const char ch = line[index++];
                    expanded.push_back(ch);
                    if (ch == '\\' && index < line.size()) {
                        expanded.push_back(line[index++]);
                    } else if (ch == quote) {
                        break;
                    }
                }
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
                const std::size_t begin = index;
                ++index;
                while (index < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[index])) != 0 ||
                        line[index] == '_')) {
                    ++index;
                }
                const std::string name = line.substr(begin, index - begin);
                const auto function_macro = function_macros.find(name);
                if (function_macro != function_macros.end() && index < line.size() &&
                    line[index] == '(') {
                    std::size_t close = index + 1;
                    int depth = 1;
                    bool in_quote = false;
                    char quote = 0;
                    while (close < line.size() && depth > 0) {
                        const char ch = line[close];
                        if (in_quote) {
                            if (ch == '\\') {
                                close += 2;
                                continue;
                            }
                            if (ch == quote) {
                                in_quote = false;
                            }
                        } else if (ch == '"' || ch == '\'') {
                            in_quote = true;
                            quote = ch;
                        } else if (ch == '(') {
                            ++depth;
                        } else if (ch == ')') {
                            --depth;
                        }
                        ++close;
                    }
                    if (depth == 0) {
                        const std::string argument_text = line.substr(index + 1, close - index - 2);
                        std::vector<std::string> arguments;
                        std::string current;
                        int argument_depth = 0;
                        bool argument_quote = false;
                        char argument_quote_char = 0;
                        for (char ch : argument_text) {
                            if (argument_quote) {
                                current.push_back(ch);
                                if (ch == argument_quote_char) {
                                    argument_quote = false;
                                }
                            } else if (ch == '"' || ch == '\'') {
                                argument_quote = true;
                                argument_quote_char = ch;
                                current.push_back(ch);
                            } else if (ch == '(') {
                                ++argument_depth;
                                current.push_back(ch);
                            } else if (ch == ')') {
                                --argument_depth;
                                current.push_back(ch);
                            } else if (ch == ',' && argument_depth == 0) {
                                arguments.push_back(current);
                                current.clear();
                            } else {
                                current.push_back(ch);
                            }
                        }
                        if (!current.empty() || !argument_text.empty()) {
                            arguments.push_back(current);
                        }
                        std::string substituted = function_macro->second.body;
                        for (std::size_t parameter = 0;
                             parameter < function_macro->second.parameters.size(); ++parameter) {
                            const std::string needle = function_macro->second.parameters[parameter];
                            const std::string replacement =
                                parameter < arguments.size() ? arguments[parameter] : "";
                            std::size_t position = 0;
                            while ((position = substituted.find(needle, position)) !=
                                   std::string::npos) {
                                substituted.replace(position, needle.size(), replacement);
                                position += replacement.size();
                            }
                        }
                        expanded += "(" + substituted + ")";
                        index = close;
                        continue;
                    }
                }
                const auto macro = macros.find(name);
                if (macro != macros.end()) {
                    expanded += "(" + macro->second + ")";
                } else {
                    expanded += name;
                }
                continue;
            }
            expanded.push_back(c);
            ++index;
        }
        return expanded;
    };

    std::string result;
    std::istringstream lines(without_comments);
    std::string line;
    while (std::getline(lines, line)) {
        std::string trimmed = line;
        const std::size_t first = trimmed.find_first_not_of(" \t");
        if (first != std::string::npos && trimmed[first] == '#') {
            const std::string directive = trimmed.substr(first);
            if (directive.rfind("#define", 0) == 0) {
                std::string rest = directive.substr(7);
                const std::size_t rest_first = rest.find_first_not_of(" \t");
                if (rest_first != std::string::npos) {
                    rest = rest.substr(rest_first);
                }
                const std::size_t name_end = rest.find_first_of(" \t(");
                const std::string name = rest.substr(0, name_end);
                if (name_end != std::string::npos && rest[name_end] == '(') {
                    const std::size_t close = rest.find(')', name_end + 1);
                    if (close != std::string::npos) {
                        FunctionMacro macro;
                        std::string parameters = rest.substr(name_end + 1, close - name_end - 1);
                        std::size_t position = 0;
                        while (position < parameters.size()) {
                            const std::size_t comma = parameters.find(',', position);
                            std::string parameter = parameters.substr(
                                position, comma == std::string::npos ? std::string::npos
                                                                     : comma - position);
                            const std::size_t first_char = parameter.find_first_not_of(" \t");
                            if (first_char != std::string::npos) {
                                macro.parameters.push_back(parameter.substr(first_char));
                            }
                            if (comma == std::string::npos) {
                                break;
                            }
                            position = comma + 1;
                        }
                        std::string body = rest.substr(close + 1);
                        const std::size_t body_first = body.find_first_not_of(" \t");
                        macro.body = body_first == std::string::npos ? "" : body.substr(body_first);
                        function_macros[name] = std::move(macro);
                    }
                } else if (!name.empty()) {
                    std::string value = name_end == std::string::npos
                                            ? "1"
                                            : rest.substr(name_end);
                    const std::size_t value_first = value.find_first_not_of(" \t");
                    macros[name] = value_first == std::string::npos
                                       ? "1"
                                       : value.substr(value_first);
                }
            } else if (directive.rfind("#undef", 0) == 0) {
                const std::string name = directive.substr(6);
                const std::size_t name_first = name.find_first_not_of(" \t");
                if (name_first != std::string::npos) {
                    macros.erase(name.substr(name_first));
                }
            } else if (directive.rfind("#ifdef", 0) == 0) {
                const std::string name = directive.substr(6);
                const std::size_t name_first = name.find_first_not_of(" \t");
                const bool condition = name_first != std::string::npos &&
                                       macros.find(name.substr(name_first)) != macros.end();
                conditions.push_back({active, condition});
                active = active && condition;
            } else if (directive.rfind("#ifndef", 0) == 0) {
                const std::string name = directive.substr(7);
                const std::size_t name_first = name.find_first_not_of(" \t");
                const bool condition = name_first == std::string::npos ||
                                       macros.find(name.substr(name_first)) == macros.end();
                conditions.push_back({active, condition});
                active = active && condition;
            } else if (directive.rfind("#if", 0) == 0) {
                const std::string condition_text = directive.substr(3);
                const std::size_t condition_first = condition_text.find_first_not_of(" \t");
                const bool condition = condition_first != std::string::npos &&
                                       condition_text.substr(condition_first) != "0";
                conditions.push_back({active, condition});
                active = active && condition;
            } else if (directive.rfind("#else", 0) == 0) {
                if (conditions.empty()) {
                    // Ignore unmatched #else rather than failing the lexer.
                } else {
                    const Conditional current = conditions.back();
                    active = current.parent_active && !current.condition;
                }
            } else if (directive.rfind("#endif", 0) == 0) {
                if (!conditions.empty()) {
                    active = conditions.back().parent_active;
                    conditions.pop_back();
                }
            } else if (directive.rfind("#include", 0) == 0 && active) {
                if (directive.find("kr32.h") != std::string::npos ||
                    directive.find("stdio.h") != std::string::npos ||
                    directive.find("stdlib.h") != std::string::npos ||
                    directive.find("string.h") != std::string::npos) {
                    result +=
                        "int putchar(int c); void exit(int code); int getchar(void); "
                        "int open(const char *path, int flags); int close(int fd); "
                        "int read(int fd, void *buffer, int count); "
                        "int write(int fd, const void *buffer, int count); "
                        "int seek(int fd, int offset, int whence); int sys_time(void); "
                        "int sys_random(void); int sys_argc(void); "
                        "int sys_arg(int index, char *buffer, int size); "
                        "int printf(const char *format, ...); void *malloc(int size); "
                        "void free(void *pointer); void *calloc(int count, int size); "
                        "void *realloc(void *pointer, int size); "
                        "int _heapwalk(void); "
                        "void *memcpy(void *destination, const void *source, int count); "
                        "void *memset(void *destination, int value, int count); "
                        "int strlen(const char *text); "
                        "char *strcpy(char *destination, const char *source); "
                        "char *strncpy(char *destination, const char *source, int count); "
                        "char *strcat(char *destination, const char *source); "
                        "char *strncat(char *destination, const char *source, int count); "
                        "int strcmp(const char *left, const char *right); "
                        "int strncmp(const char *left, const char *right, int count); "
                        "char *strchr(const char *text, int character); "
                        "char *strrchr(const char *text, int character); "
                        "char *strstr(const char *text, const char *needle); "
                        "char *strtok(char *text, const char *delimiters); "
                        "char *strdup(const char *text); "
                        "void *memmove(void *destination, const void *source, int count); "
                        "int memcmp(const void *left, const void *right, int count); "
                        "void *memchr(const void *memory, int character, int count);";
                }
                if (directive.find("stdarg.h") != std::string::npos) {
                    result += "typedef char *va_list;";
                    function_macros["va_start"] =
                        FunctionMacro{{"ap", "last"}, "__builtin_va_start(ap)"};
                    function_macros["va_arg"] =
                        FunctionMacro{{"ap", "type"}, "__builtin_va_arg(ap, type)"};
                    function_macros["va_end"] = FunctionMacro{{"ap"}, "((void)0)"};
                    function_macros["va_copy"] =
                        FunctionMacro{{"dest", "src"}, "__builtin_va_copy(dest, src)"};
                }
                if (directive.find("stddef.h") != std::string::npos) {
                    result +=
                        "typedef unsigned int size_t; typedef int ptrdiff_t;";
                    macros["NULL"] = "0";
                    function_macros["offsetof"] =
                        FunctionMacro{{"type", "member"},
                                      "__builtin_offsetof(type, member)"};
                }
                if (directive.find("math.h") != std::string::npos) {
                    result +=
                        "double sqrt(double value); float sqrtf(float value); "
                        "double fabs(double value); float fabsf(float value); "
                        "double fmin(double left, double right); "
                        "float fminf(float left, float right); "
                        "double fmax(double left, double right); "
                        "float fmaxf(float left, float right);";
                    macros["M_PI"] = "3.14159265358979323846";
                }
            }
            result.push_back('\n');
            continue;
        }
        if (!active) {
            result.push_back('\n');
            continue;
        }
        result += expand_line(line);
        result.push_back('\n');
    }
    return result;
}

enum class TokenKind { End, Identifier, Number, Float, String, Character, Punct };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    std::int64_t value = 0;
    double real = 0.0;
    bool is_float = false;
    bool is_unsigned = false;
    int line = 1;
    int column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (true) {
            Token token = next();
            tokens.push_back(token);
            if (token.kind == TokenKind::End) {
                break;
            }
        }
        return tokens;
    }

private:
    char peek(std::size_t ahead = 0) const {
        return position_ + ahead < source_.size() ? source_[position_ + ahead] : '\0';
    }

    void advance() {
        if (position_ >= source_.size()) {
            return;
        }
        if (source_[position_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++position_;
    }

    void skip_space() {
        while (position_ < source_.size()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                advance();
            } else {
                break;
            }
        }
    }

    Token next() {
        skip_space();
        Token token;
        token.line = line_;
        token.column = column_;
        if (position_ >= source_.size()) {
            token.kind = TokenKind::End;
            return token;
        }
        const char c = peek();
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            token.kind = TokenKind::Identifier;
            while (position_ < source_.size() &&
                   (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_')) {
                token.text.push_back(peek());
                advance();
            }
            return token;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0)) {
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                std::string number = "0x";
                advance();
                advance();
                while (std::isxdigit(static_cast<unsigned char>(peek())) != 0 ||
                       peek() == '\'') {
                    number.push_back(peek());
                    advance();
                }
                if (peek() == 'u' || peek() == 'U') {
                    token.is_unsigned = true;
                    advance();
                }
                token.kind = TokenKind::Number;
                token.text = number;
                char* end = nullptr;
                token.value = std::strtoll(number.c_str(), &end, 0);
                if (end == nullptr || *end != '\0') {
                    throw CompileError(token.line, token.column, "invalid number: " + number);
                }
                return token;
            }
            std::string number;
            bool floating = false;
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
                   peek() == '\'' || peek() == '.') {
                if (peek() == '.') {
                    floating = true;
                }
                number.push_back(peek());
                advance();
            }
            if (peek() == 'e' || peek() == 'E') {
                floating = true;
                number.push_back(peek());
                advance();
                if (peek() == '+' || peek() == '-') {
                    number.push_back(peek());
                    advance();
                }
                while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                    number.push_back(peek());
                    advance();
                }
            }
            bool float_suffix = false;
            bool unsigned_suffix = false;
            if (peek() == 'f' || peek() == 'F') {
                float_suffix = true;
                floating = true;
                advance();
            } else if (peek() == 'l' || peek() == 'L') {
                advance();
            } else if (peek() == 'u' || peek() == 'U') {
                unsigned_suffix = true;
                advance();
            }
            token.text = number;
            token.is_unsigned = unsigned_suffix;
            if (floating) {
                if (unsigned_suffix) {
                    throw CompileError(token.line, token.column,
                                       "unsigned suffix is not valid on a float");
                }
                token.kind = TokenKind::Float;
                char* end = nullptr;
                token.real = std::strtod(number.c_str(), &end);
                if (end == nullptr || *end != '\0') {
                    throw CompileError(token.line, token.column, "invalid number: " + number);
                }
                token.is_float = float_suffix;
            } else {
                token.kind = TokenKind::Number;
                char* end = nullptr;
                token.value = std::strtoll(number.c_str(), &end, 0);
                if (end == nullptr || *end != '\0') {
                    throw CompileError(token.line, token.column, "invalid number: " + number);
                }
            }
            return token;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            advance();
            std::string value;
            bool closed = false;
            while (position_ < source_.size()) {
                char ch = peek();
                advance();
                if (ch == quote) {
                    closed = true;
                    break;
                }
                if (ch == '\\') {
                    if (position_ >= source_.size()) {
                        break;
                    }
                    const char escaped = peek();
                    advance();
                    switch (escaped) {
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case '0': value.push_back('\0'); break;
                    case '\\': value.push_back('\\'); break;
                    case '\'': value.push_back('\''); break;
                    case '"': value.push_back('"'); break;
                    default: value.push_back(escaped); break;
                    }
                } else {
                    value.push_back(ch);
                }
            }
            if (!closed) {
                throw CompileError(token.line, token.column, "unterminated literal");
            }
            if (quote == '\'') {
                token.kind = TokenKind::Character;
                token.value = value.empty() ? 0 : static_cast<unsigned char>(value.front());
            } else {
                token.kind = TokenKind::String;
                token.text = value;
            }
            return token;
        }

        static const char* punctuation[] = {
            "<<=", ">>=", "...", "==", "!=", "<=", ">=", "&&", "||", "++", "--",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "->",
            "+", "-", "*", "/", "%", "=", "<", ">", "&", "|", "^", "~", "!",
            "(", ")", "{", "}", "[", "]", ";", ",", ".", "?", ":",
        };
        for (const char* candidate : punctuation) {
            const std::size_t length = std::string(candidate).size();
            if (source_.compare(position_, length, candidate) == 0) {
                token.kind = TokenKind::Punct;
                token.text = candidate;
                for (std::size_t index = 0; index < length; ++index) {
                    advance();
                }
                return token;
            }
        }
        throw CompileError(token.line, token.column,
                           std::string("unexpected character: ") + c);
    }

    std::string_view source_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Member {
    std::string name;
    TypePtr type;
    int offset = 0;
};

struct Type {
    enum class Kind { Void, Char, Int, Bool, Float, Double, Pointer, Array, Struct, Union };

    Kind kind = Kind::Int;
    TypePtr base;
    std::weak_ptr<Type> pointer_base;
    int count = 0;
    bool is_unsigned = false;
    std::vector<Member> members;
    std::string tag;
    int total_size = 0;
    int struct_alignment = 1;

    Type() = default;
    Type(Kind type_kind, TypePtr type_base, int type_count)
        : kind(type_kind), base(std::move(type_base)), count(type_count) {}

    [[nodiscard]] int size() const {
        switch (kind) {
        case Kind::Void: return 1;
        case Kind::Char: return 1;
        case Kind::Bool: return 1;
        case Kind::Int: return 4;
        case Kind::Float: return 4;
        case Kind::Double: return 8;
        case Kind::Pointer: return 4;
        case Kind::Array: return base ? base->size() * count : 0;
        case Kind::Struct:
        case Kind::Union: return total_size;
        }
        return 4;
    }

    [[nodiscard]] int alignment() const {
        switch (kind) {
        case Kind::Void: return 1;
        case Kind::Char: return 1;
        case Kind::Bool: return 1;
        case Kind::Int: return 4;
        case Kind::Float: return 4;
        case Kind::Double: return 8;
        case Kind::Pointer: return 4;
        case Kind::Array: return base ? base->alignment() : 1;
        case Kind::Struct:
        case Kind::Union: return struct_alignment;
        }
        return 4;
    }

    [[nodiscard]] bool is_pointer() const {
        return kind == Kind::Pointer || kind == Kind::Array;
    }

    [[nodiscard]] TypePtr element() const {
        if (kind == Kind::Pointer) {
            TypePtr pointed = pointer_base.lock();
            if (pointed) {
                return pointed;
            }
        }
        return base;
    }
};

TypePtr type_int() { return std::make_shared<Type>(Type{Type::Kind::Int, nullptr, 0}); }
TypePtr type_uint() {
    auto type = type_int();
    type->is_unsigned = true;
    return type;
}
TypePtr type_char() { return std::make_shared<Type>(Type{Type::Kind::Char, nullptr, 0}); }
TypePtr type_bool() { return std::make_shared<Type>(Type{Type::Kind::Bool, nullptr, 0}); }
TypePtr type_float() { return std::make_shared<Type>(Type{Type::Kind::Float, nullptr, 0}); }
TypePtr type_double() { return std::make_shared<Type>(Type{Type::Kind::Double, nullptr, 0}); }
TypePtr type_void() { return std::make_shared<Type>(Type{Type::Kind::Void, nullptr, 0}); }
TypePtr type_pointer(TypePtr base) {
    auto pointer = std::make_shared<Type>();
    pointer->kind = Type::Kind::Pointer;
    if (base != nullptr && !base->tag.empty()) {
        pointer->pointer_base = base;
    } else {
        pointer->base = std::move(base);
    }
    return pointer;
}
TypePtr type_array(TypePtr base, int count) {
    return std::make_shared<Type>(Type{Type::Kind::Array, std::move(base), count});
}

bool is_fp_type(const TypePtr& type) {
    return type != nullptr &&
           (type->kind == Type::Kind::Float || type->kind == Type::Kind::Double);
}

bool is_unsigned_scalar(const TypePtr& type) {
    return type != nullptr && type->is_unsigned &&
           (type->kind == Type::Kind::Char || type->kind == Type::Kind::Int);
}

TypePtr common_fp_type(const TypePtr& left, const TypePtr& right) {
    if ((left != nullptr && left->kind == Type::Kind::Double) ||
        (right != nullptr && right->kind == Type::Kind::Double)) {
        return type_double();
    }
    return type_float();
}

struct Variable {
    std::string name;
    TypePtr type;
    int offset = 0;
    std::string label;
    bool global = false;
};

struct FunctionSignature {
    std::string name;
    TypePtr result;
    std::vector<TypePtr> parameters;
    bool variadic = false;
    bool defined = false;
};

class Compiler {
public:
    Compiler(std::string_view source, std::string_view name, int source_id = 0)
        : source_name_(name), source_id_(source_id),
          tokens_(Lexer(preprocess(source)).tokenize()) {}

    Result run() {
        Result result;
        try {
            emit(".text");
            debug_record("S " + std::to_string(source_id_) + " " + source_name_);
            while (!at_end()) {
                parse_top_level();
            }
            result.assembly = text_;
            if (!rodata_.empty()) {
                result.assembly += ".rodata\n" + rodata_;
            }
            if (!data_.empty()) {
                result.assembly += ".data\n" + data_;
            }
            if (!bss_.empty()) {
                result.assembly += ".bss\n" + bss_;
            }
            if (!debug_.empty()) {
                result.assembly += ".debug_info\n" + debug_;
            }
            result.ok = true;
        } catch (const CompileError& error) {
            result.ok = false;
            result.diagnostics.push_back(
                {error.line, error.column, std::string(error.what())});
        }
        return result;
    }

private:
    const Token& peek(std::size_t ahead = 0) const {
        const std::size_t index = position_ + ahead;
        return index < tokens_.size() ? tokens_[index] : tokens_.back();
    }

    bool at_end() const { return peek().kind == TokenKind::End; }

    const Token& take() {
        const Token& token = peek();
        if (position_ < tokens_.size()) {
            ++position_;
        }
        return token;
    }

    bool match(std::string_view text) {
        if (peek().text == text) {
            take();
            return true;
        }
        return false;
    }

    const Token& expect(std::string_view text) {
        if (peek().text != text) {
            throw CompileError(peek().line, peek().column,
                               "expected '" + std::string(text) + "'");
        }
        return take();
    }

    const Token& expect_identifier() {
        if (peek().kind != TokenKind::Identifier) {
            throw CompileError(peek().line, peek().column, "expected an identifier");
        }
        return take();
    }

    [[noreturn]] void error(std::string message) {
        throw CompileError(peek().line, peek().column, std::move(message));
    }

    void emit(std::string line) {
        line += '\n';
        if (output_ != nullptr) {
            *output_ += line;
        } else {
            text_ += line;
        }
    }

    std::string new_label(std::string_view prefix) {
        return ".L" + std::string(prefix) + "_" + std::to_string(label_counter_++);
    }

    TypePtr emit_fp_constant(double value, bool is_float) {
        const std::string label = ".Lfp_" + std::to_string(string_counter_++);
        if (is_float) {
            const std::uint32_t bits =
                std::bit_cast<std::uint32_t>(static_cast<float>(value));
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "    .word 0x%08X\n", bits);
            rodata_ += label + ":\n" + buffer;
            emit("    FLOAD.W F0, [" + label + "]");
            return type_float();
        }
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        char low[32] = {};
        char high[32] = {};
        std::snprintf(low, sizeof(low), "    .word 0x%08X\n",
                      static_cast<std::uint32_t>(bits & 0xFFFFFFFFU));
        std::snprintf(high, sizeof(high), "    .word 0x%08X\n",
                      static_cast<std::uint32_t>(bits >> 32));
        rodata_ += label + ":\n" + low + high;
        emit("    FLOAD.D F0, [" + label + "]");
        return type_double();
    }

    std::string type_name(const TypePtr& type) const {
        if (type == nullptr) {
            return "int";
        }
        switch (type->kind) {
        case Type::Kind::Void: return "void";
        case Type::Kind::Char: return type->is_unsigned ? "unsigned char" : "char";
        case Type::Kind::Int: return type->is_unsigned ? "unsigned int" : "int";
        case Type::Kind::Bool: return "_Bool";
        case Type::Kind::Float: return "float";
        case Type::Kind::Double: return "double";
        case Type::Kind::Pointer: return "ptr";
        case Type::Kind::Array: return "array";
        case Type::Kind::Struct: return "struct:" + type->tag;
        case Type::Kind::Union: return "union:" + type->tag;
        }
        return "int";
    }

    void debug_record(const std::string& record) {
        debug_ += "    .asciz \"" + escape_string(record) + "\"\n";
    }

    void debug_type(const TypePtr& type) {
        debug_record("T " + type_name(type) + " " + std::to_string(type->size()));
    }

    bool is_type_start() const {
        const std::string& text = peek().text;
        return text == "int" || text == "char" || text == "void" || text == "_Bool" ||
               text == "float" || text == "double" ||
               text == "struct" || text == "union" || text == "enum" ||
               text == "const" || text == "unsigned" || text == "signed" ||
               text == "static" || text == "extern" || text == "volatile" ||
               text == "register" || text == "long" || typedefs_.find(text) != typedefs_.end();
    }

    static int align_up(int value, int alignment) {
        if (alignment <= 1) {
            return value;
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    TypePtr parse_struct_or_union(bool is_union) {
        std::string tag;
        if (peek().kind == TokenKind::Identifier) {
            tag = take().text;
        }
        if (match("{")) {
            auto type = std::make_shared<Type>();
            type->kind = is_union ? Type::Kind::Union : Type::Kind::Struct;
            type->tag = tag;
            if (!tag.empty()) {
                tags_[tag] = type;
            }
            while (!match("}")) {
                TypePtr member_type = parse_type();
                const std::string member_name = parse_name();
                if (match("[")) {
                    std::int64_t count = 0;
                    if (peek().kind == TokenKind::Number) {
                        count = take().value;
                    }
                    expect("]");
                    member_type = type_array(member_type, static_cast<int>(count));
                }
                expect(";");
                type->members.push_back(Member{member_name, member_type, 0});
            }
            int offset = 0;
            int max_alignment = 1;
            int max_size = 0;
            for (Member& member : type->members) {
                const int alignment = std::max(1, member.type->alignment());
                max_alignment = std::max(max_alignment, alignment);
                if (is_union) {
                    member.offset = 0;
                    max_size = std::max(max_size, member.type->size());
                } else {
                    offset = align_up(offset, alignment);
                    member.offset = offset;
                    offset += member.type->size();
                }
            }
            type->struct_alignment = max_alignment;
            type->total_size = align_up(is_union ? max_size : offset, max_alignment);
            if (type->total_size == 0) {
                type->total_size = 1;
            }
            if (!tag.empty()) {
                tags_[tag] = type;
            }
            return type;
        }
        if (tag.empty()) {
            error("expected a struct or union body");
        }
        const auto found = tags_.find(tag);
        if (found == tags_.end()) {
            error("unknown struct/union: " + tag);
        }
        return found->second;
    }

    std::int64_t constant_primary() {
        if (match("(")) {
            const std::int64_t value = constant_expression();
            expect(")");
            return value;
        }
        if (peek().kind == TokenKind::Number || peek().kind == TokenKind::Character) {
            return take().value;
        }
        if (match("sizeof")) {
            if (!match("(")) {
                error("sizeof in a constant expression requires parentheses");
            }
            TypePtr type = parse_type();
            expect(")");
            return type->size();
        }
        if (match("_Alignof") || match("alignof")) {
            expect("(");
            TypePtr type = parse_type();
            expect(")");
            return type->alignment();
        }
        if (match("__builtin_offsetof")) {
            expect("(");
            TypePtr type = parse_type();
            expect(",");
            const std::string member_name = parse_name();
            expect(")");
            const Member* member = find_member(type, member_name);
            if (member == nullptr) {
                error("unknown member in offsetof: " + member_name);
            }
            return member->offset;
        }
        if (peek().kind == TokenKind::Identifier) {
            const std::string name = take().text;
            const auto found = enum_constants_.find(name);
            if (found != enum_constants_.end()) {
                return found->second;
            }
        }
        error("expected a constant expression");
    }

    std::int64_t constant_unary() {
        if (match("-")) {
            return -constant_unary();
        }
        if (match("+")) {
            return constant_unary();
        }
        if (match("!")) {
            return constant_unary() == 0 ? 1 : 0;
        }
        if (match("~")) {
            return ~constant_unary();
        }
        return constant_primary();
    }

    std::int64_t constant_multiplicative() {
        std::int64_t value = constant_unary();
        while (true) {
            if (match("*")) {
                value *= constant_unary();
            } else if (match("/")) {
                const std::int64_t right = constant_unary();
                if (right == 0) {
                    error("division by zero in a constant expression");
                }
                value /= right;
            } else if (match("%")) {
                const std::int64_t right = constant_unary();
                if (right == 0) {
                    error("division by zero in a constant expression");
                }
                value %= right;
            } else {
                return value;
            }
        }
    }

    std::int64_t constant_additive() {
        std::int64_t value = constant_multiplicative();
        while (true) {
            if (match("+")) {
                value += constant_multiplicative();
            } else if (match("-")) {
                value -= constant_multiplicative();
            } else {
                return value;
            }
        }
    }

    std::int64_t constant_shift() {
        std::int64_t value = constant_additive();
        while (true) {
            if (match("<<")) {
                value <<= constant_additive();
            } else if (match(">>")) {
                value >>= constant_additive();
            } else {
                return value;
            }
        }
    }

    std::int64_t constant_relational() {
        std::int64_t value = constant_shift();
        while (true) {
            if (match("<")) {
                value = value < constant_shift() ? 1 : 0;
            } else if (match("<=")) {
                value = value <= constant_shift() ? 1 : 0;
            } else if (match(">")) {
                value = value > constant_shift() ? 1 : 0;
            } else if (match(">=")) {
                value = value >= constant_shift() ? 1 : 0;
            } else {
                return value;
            }
        }
    }

    std::int64_t constant_equality() {
        std::int64_t value = constant_relational();
        while (true) {
            if (match("==")) {
                value = value == constant_relational() ? 1 : 0;
            } else if (match("!=")) {
                value = value != constant_relational() ? 1 : 0;
            } else {
                return value;
            }
        }
    }

    std::int64_t constant_bit_and() {
        std::int64_t value = constant_equality();
        while (match("&")) {
            value &= constant_equality();
        }
        return value;
    }

    std::int64_t constant_bit_xor() {
        std::int64_t value = constant_bit_and();
        while (match("^")) {
            value ^= constant_bit_and();
        }
        return value;
    }

    std::int64_t constant_bit_or() {
        std::int64_t value = constant_bit_xor();
        while (match("|")) {
            value |= constant_bit_xor();
        }
        return value;
    }

    std::int64_t constant_expression() {
        std::int64_t value = constant_bit_or();
        while (true) {
            if (match("&&")) {
                const std::int64_t right = constant_bit_or();
                value = (value != 0 && right != 0) ? 1 : 0;
            } else if (match("||")) {
                const std::int64_t right = constant_bit_or();
                value = (value != 0 || right != 0) ? 1 : 0;
            } else {
                return value;
            }
        }
    }

    void parse_static_assert() {
        expect("(");
        const std::int64_t value = constant_expression();
        expect(",");
        if (peek().kind != TokenKind::String) {
            error("expected a static assertion message");
        }
        const std::string message = take().text;
        expect(")");
        expect(";");
        if (value == 0) {
            error("static assertion failed: " + message);
        }
    }

    std::int64_t parse_constant_value() {
        int sign = 1;
        if (match("-")) {
            sign = -1;
        }
        if (peek().kind == TokenKind::Number || peek().kind == TokenKind::Character) {
            return sign * take().value;
        }
        if (peek().kind == TokenKind::Identifier) {
            const std::string name = take().text;
            const auto found = enum_constants_.find(name);
            if (found != enum_constants_.end()) {
                return sign * found->second;
            }
        }
        error("expected a constant");
    }

    TypePtr parse_enum() {
        if (peek().kind == TokenKind::Identifier) {
            take();
        }
        if (!match("{")) {
            error("expected an enum body");
        }
        std::int64_t value = 0;
        while (!match("}")) {
            const std::string name = parse_name();
            if (match("=")) {
                value = parse_constant_value();
            }
            enum_constants_[name] = value;
            ++value;
            if (!match(",")) {
                break;
            }
        }
        expect("}");
        return type_int();
    }

    TypePtr parse_type() {
        bool unsigned_flag = false;
        bool signed_flag = false;
        bool long_flag = false;
        while (peek().text == "const" || peek().text == "unsigned" ||
               peek().text == "signed" || peek().text == "static" ||
               peek().text == "extern" || peek().text == "volatile" ||
               peek().text == "register" || peek().text == "typedef" ||
               peek().text == "long") {
            const std::string qualifier = take().text;
            if (qualifier == "unsigned") {
                unsigned_flag = true;
            } else if (qualifier == "signed") {
                signed_flag = true;
            } else if (qualifier == "long") {
                if (long_flag) {
                    error("long long is not supported");
                }
                long_flag = true;
            }
        }
        if (peek().text == "short") {
            error("short is not supported yet");
        }
        TypePtr base;
        if (match("struct")) {
            base = parse_struct_or_union(false);
        } else if (match("union")) {
            base = parse_struct_or_union(true);
        } else if (match("enum")) {
            base = parse_enum();
        } else if (peek().kind == TokenKind::Identifier &&
                   typedefs_.find(peek().text) != typedefs_.end()) {
            base = typedefs_[take().text];
        } else if (match("int")) {
            base = type_int();
        } else if (match("char")) {
            base = type_char();
        } else if (match("_Bool")) {
            base = type_bool();
        } else if (match("float")) {
            base = type_float();
        } else if (match("double")) {
            base = type_double();
        } else if (match("void")) {
            base = type_void();
        } else if (unsigned_flag || signed_flag || long_flag) {
            base = type_int();
        } else {
            error("expected a type");
        }
        if (long_flag && base->kind == Type::Kind::Double) {
            error("long double is not supported");
        }
        if (unsigned_flag) {
            if (base->kind != Type::Kind::Int && base->kind != Type::Kind::Char) {
                error("unsigned is only supported for int and char");
            }
            base->is_unsigned = true;
        }
        while (match("*")) {
            base = type_pointer(base);
        }
        return base;
    }

    std::string parse_name() {
        const Token& token = expect_identifier();
        return token.text;
    }

    TypePtr parse_array_suffix(TypePtr type) {
        std::vector<int> dimensions;
        while (match("[")) {
            int count = 0;
            if (peek().kind == TokenKind::Number) {
                count = static_cast<int>(take().value);
            }
            expect("]");
            dimensions.push_back(count);
        }
        for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
            type = type_array(type, *it);
        }
        return type;
    }

    void parse_top_level() {
        if (match(";")) {
            return;
        }
        if (match("_Static_assert")) {
            parse_static_assert();
            return;
        }
        if (match("typedef")) {
            TypePtr type = parse_type();
            const std::string name = parse_name();
            typedefs_[name] = type;
            expect(";");
            return;
        }
        TypePtr type = parse_type();
        if (match(";")) {
            return;
        }
        const std::string name = parse_name();
        if (match("(")) {
            parse_function(type, name);
            return;
        }
        parse_global(type, name);
    }

    void parse_global(const TypePtr& type, const std::string& name) {
        TypePtr actual = parse_array_suffix(type);
        if (match("=")) {
            if (actual->kind == Type::Kind::Array) {
                error("array initializers are not supported yet");
            }
            if (peek().kind == TokenKind::Float) {
                const Token token = take();
                if (actual->kind != Type::Kind::Float &&
                    actual->kind != Type::Kind::Double) {
                    error("floating-point initializer requires a float or double");
                }
                if (actual->kind == Type::Kind::Double) {
                    data_ += ".align 8\n";
                }
                data_ += ".global " + name + "\n" + name + ":\n";
                if (actual->kind == Type::Kind::Float) {
                    const std::uint32_t bits =
                        std::bit_cast<std::uint32_t>(static_cast<float>(token.real));
                    char buffer[32] = {};
                    std::snprintf(buffer, sizeof(buffer), "    .word 0x%08X\n", bits);
                    data_ += buffer;
                } else {
                    const std::uint64_t bits = std::bit_cast<std::uint64_t>(token.real);
                    char low[32] = {};
                    char high[32] = {};
                    std::snprintf(low, sizeof(low), "    .word 0x%08X\n",
                                  static_cast<std::uint32_t>(bits & 0xFFFFFFFFU));
                    std::snprintf(high, sizeof(high), "    .word 0x%08X\n",
                                  static_cast<std::uint32_t>(bits >> 32));
                    data_ += low;
                    data_ += high;
                }
                expect(";");
            } else {
                if (peek().kind != TokenKind::Number && peek().kind != TokenKind::Character) {
                    error("global initializer must be a constant");
                }
                const std::int64_t value = take().value;
                data_ += ".global " + name + "\n" + name + ":\n";
                if (actual->kind == Type::Kind::Char) {
                    data_ += "    .byte " + std::to_string(value & 0xFF) + "\n";
                } else {
                    data_ += "    .word " + std::to_string(value & 0xFFFF'FFFFU) + "\n";
                }
                expect(";");
            }
        } else {
            expect(";");
            bss_ += ".global " + name + "\n" + name + ":\n";
            if (actual->kind == Type::Kind::Double) {
                bss_ += "    .align 8\n";
            }
            bss_ += "    .space " + std::to_string(actual->size()) + "\n";
        }
        globals_.emplace(name, Variable{name, actual, 0, name, true});
        debug_type(actual);
        debug_record("G " + name + " " + name + " " + type_name(actual) + " " +
                     std::to_string(actual->size()));
    }

    int allocate_local(const TypePtr& type) {
        const int size = std::max(1, type->size());
        const int alignment = std::max(4, type->alignment());
        frame_size_ = align_up(frame_size_, alignment);
        const int offset = frame_size_;
        frame_size_ += align_up(size, alignment);
        return offset;
    }

    void parse_function(const TypePtr& result_type, const std::string& name) {
        std::vector<TypePtr> parameters;
        std::vector<std::string> parameter_names;
        bool variadic = false;
        if (!match(")")) {
            if (peek().text == "void" && peek(1).text == ")") {
                take();
                take();
            } else {
                while (true) {
                    if (match("...")) {
                        variadic = true;
                        break;
                    }
                    TypePtr parameter = parse_type();
                    std::string parameter_name = parse_name();
                    if (match("[")) {
                        expect("]");
                        parameter = type_pointer(parameter);
                    }
                    parameters.push_back(parameter);
                    parameter_names.push_back(std::move(parameter_name));
                    if (!match(",")) {
                        break;
                    }
                    if (match("...")) {
                        variadic = true;
                        break;
                    }
                }
                expect(")");
            }
        }

        auto found = functions_.find(name);
        if (found == functions_.end()) {
            functions_.emplace(
                name, FunctionSignature{name, result_type, parameters, variadic, false});
        } else if (found->second.defined) {
            error("function already defined: " + name);
        } else {
            found->second.variadic = variadic;
        }

        if (match(";")) {
            return;
        }
        if (!match("{")) {
            error("expected '{' or ';'");
        }
        if (parameters.size() > 4) {
            error("more than four parameters are not supported yet");
        }

        functions_[name].defined = true;
        current_function_ = name;
        current_result_type_ = result_type;
        frame_size_ = 0;
        locals_.clear();
        return_label_ = ".Lreturn_" + name;
        emit(".global " + name);
        emit(name + ":");
        emit("    PUSH R7");
        emit("    ADJSP #-__FRAME_" + name + "__");
        emit("    LEA R7, [SP]");

        int integer_register = 0;
        int fp_register = 0;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const int offset = allocate_local(parameters[index]);
            locals_.emplace(parameter_names[index],
                            Variable{parameter_names[index], parameters[index], offset, "", false});
            if (is_fp_type(parameters[index])) {
                emit(std::string("    FSTORE") +
                     (parameters[index]->kind == Type::Kind::Double ? ".D" : ".W") +
                     " F" + std::to_string(fp_register) + ", [R7 + " +
                     std::to_string(offset) + "]");
                ++fp_register;
            } else {
                emit("    STORE R" + std::to_string(integer_register) + ", [R7 + " +
                     std::to_string(offset) + "]");
                ++integer_register;
            }
            debug_type(parameters[index]);
            debug_record("V " + name + " " + parameter_names[index] + " " +
                         std::to_string(offset) + " " + type_name(parameters[index]) + " " +
                         std::to_string(parameters[index]->size()));
        }

        parse_block_contents();
        if (result_type->kind == Type::Kind::Float) {
            emit("    MOV R0, #0");
            emit("    FCVT.S.W F0, R0");
        } else if (result_type->kind == Type::Kind::Double) {
            emit("    MOV R0, #0");
            emit("    FCVT.D.W F0, R0");
        } else {
            emit("    MOV R0, #0");
        }
        emit(return_label_ + ":");
        emit("    ADJSP #__FRAME_" + name + "__");
        emit("    POP R7");
        emit("    RET");
        emit(".Lend_" + name + ":");
        debug_record("F " + name + " " + name + " .Lend_" + name + " " +
                     std::to_string(frame_size_) + " " + type_name(result_type));
        replace_all("__FRAME_" + name + "__", std::to_string(frame_size_));
    }

    void parse_block_contents() {
        while (!at_end() && peek().text != "}") {
            parse_statement();
        }
        expect("}");
    }

    void parse_statement() {
        if (!current_function_.empty()) {
            const std::string line_label =
                ".Lline_" + current_function_ + "_" + std::to_string(line_counter_++);
            emit(line_label + ":");
            debug_record("L " + line_label + " " + std::to_string(source_id_) + " " +
                         std::to_string(peek().line) + " " +
                         std::to_string(peek().column));
        }
        if (match("_Static_assert")) {
            parse_static_assert();
            return;
        }
        if (peek().kind == TokenKind::Identifier && peek(1).text == ":") {
            const std::string label = take().text;
            take();
            emit(".Luser_" + current_function_ + "_" + label + ":");
            return;
        }
        if (match("goto")) {
            const std::string label = parse_name();
            expect(";");
            emit("    JMP .Luser_" + current_function_ + "_" + label);
            return;
        }
        if (match("{")) {
            parse_block_contents();
            return;
        }
        if (match(";")) {
            return;
        }
        if (match("switch")) {
            expect("(");
            gen_expression();
            expect(")");
            const int temp = allocate_local(type_int());
            emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
            expect("{");
            const std::string end = new_label("switchend");
            const std::string dispatch = new_label("switchdispatch");
            std::vector<std::pair<std::int64_t, std::string>> cases;
            std::string default_label;
            emit("    JMP " + dispatch);
            break_labels_.push_back(end);
            while (!match("}")) {
                if (match("case")) {
                    const std::int64_t value = parse_constant_value();
                    expect(":");
                    const std::string label = new_label("casebody");
                    cases.push_back({value, label});
                    emit(label + ":");
                    while (!at_end() && peek().text != "}" && peek().text != "case" &&
                           peek().text != "default") {
                        parse_statement();
                    }
                    continue;
                }
                if (match("default")) {
                    expect(":");
                    default_label = new_label("defaultbody");
                    emit(default_label + ":");
                    while (!at_end() && peek().text != "}" && peek().text != "case" &&
                           peek().text != "default") {
                        parse_statement();
                    }
                    continue;
                }
                error("expected case, default, or '}'");
            }
            break_labels_.pop_back();
            emit("    JMP " + end);
            emit(dispatch + ":");
            emit("    LOAD R0, [R7 + " + std::to_string(temp) + "]");
            for (const auto& entry : cases) {
                emit("    MOV R1, #" + std::to_string(entry.first));
                emit("    CMP R0, R1");
                emit("    JZ " + entry.second);
            }
            emit("    JMP " + (default_label.empty() ? end : default_label));
            emit(end + ":");
            return;
        }
        if (match("if")) {
            expect("(");
            gen_condition();
            expect(")");
            const std::string else_label = new_label("else");
            const std::string end_label = new_label("endif");
            emit("    CMP R0, #0");
            emit("    JZ " + else_label);
            parse_statement();
            emit("    JMP " + end_label);
            emit(else_label + ":");
            if (match("else")) {
                parse_statement();
            }
            emit(end_label + ":");
            return;
        }
        if (match("do")) {
            const std::string start = new_label("do");
            const std::string condition = new_label("docond");
            const std::string end = new_label("doend");
            emit(start + ":");
            break_labels_.push_back(end);
            continue_labels_.push_back(condition);
            parse_statement();
            break_labels_.pop_back();
            continue_labels_.pop_back();
            emit(condition + ":");
            if (!match("while")) {
                error("expected while after do body");
            }
            expect("(");
            gen_condition();
            expect(")");
            emit("    CMP R0, #0");
            emit("    JNZ " + start);
            emit(end + ":");
            expect(";");
            return;
        }
        if (match("while")) {
            const std::string start = new_label("while");
            const std::string end = new_label("endwhile");
            emit(start + ":");
            expect("(");
            gen_condition();
            expect(")");
            emit("    CMP R0, #0");
            emit("    JZ " + end);
            break_labels_.push_back(end);
            continue_labels_.push_back(start);
            parse_statement();
            break_labels_.pop_back();
            continue_labels_.pop_back();
            emit("    JMP " + start);
            emit(end + ":");
            return;
        }
        if (match("for")) {
            expect("(");
            if (!match(";")) {
                if (is_type_start()) {
                    parse_local_declaration();
                } else {
                    parse_assignment_statement();
                    expect(";");
                }
            }
            const std::string start = new_label("for");
            const std::string end = new_label("endfor");
            const std::string increment = new_label("forinc");
            emit(start + ":");
            if (!match(";")) {
                gen_condition();
                expect(";");
                emit("    CMP R0, #0");
                emit("    JZ " + end);
            }

            std::string increment_code;
            std::string* previous_output = output_;
            output_ = &increment_code;
            if (!match(")")) {
                parse_assignment_statement();
                expect(")");
            }
            output_ = previous_output;

            break_labels_.push_back(end);
            continue_labels_.push_back(increment);
            parse_statement();
            break_labels_.pop_back();
            continue_labels_.pop_back();
            emit(increment + ":");
            if (!increment_code.empty()) {
                text_ += increment_code;
            }
            emit("    JMP " + start);
            emit(end + ":");
            return;
        }
        if (match("return")) {
            if (!match(";")) {
                TypePtr type = gen_expression();
                convert_value(type, current_result_type_);
                expect(";");
            } else if (current_result_type_ != nullptr &&
                       current_result_type_->kind == Type::Kind::Float) {
                emit("    MOV R0, #0");
                emit("    FCVT.S.W F0, R0");
            } else if (current_result_type_ != nullptr &&
                       current_result_type_->kind == Type::Kind::Double) {
                emit("    MOV R0, #0");
                emit("    FCVT.D.W F0, R0");
            } else {
                emit("    MOV R0, #0");
            }
            emit("    JMP " + return_label_);
            return;
        }
        if (match("break")) {
            expect(";");
            if (break_labels_.empty()) {
                error("break outside a loop");
            }
            emit("    JMP " + break_labels_.back());
            return;
        }
        if (match("continue")) {
            expect(";");
            if (continue_labels_.empty()) {
                error("continue outside a loop");
            }
            emit("    JMP " + continue_labels_.back());
            return;
        }
        if (match("typedef")) {
            TypePtr type = parse_type();
            const std::string name = parse_name();
            typedefs_[name] = type;
            expect(";");
            return;
        }
        if (is_type_start()) {
            parse_local_declaration();
            return;
        }
        parse_assignment_statement();
        expect(";");
    }

    void parse_local_declaration() {
        TypePtr type = parse_type();
        const std::string name = parse_name();
        TypePtr actual = parse_array_suffix(type);
        const int offset = allocate_local(actual);
        locals_[name] = Variable{name, actual, offset, "", false};
        debug_type(actual);
        debug_record("V " + current_function_ + " " + name + " " + std::to_string(offset) +
                     " " + type_name(actual) + " " + std::to_string(actual->size()));
        if (match("=")) {
            gen_lvalue_address(name);
            emit("    PUSH R0");
            TypePtr value_type = gen_expression();
            convert_value(value_type, actual);
            emit("    POP R1");
            gen_store(actual);
        }
        expect(";");
    }

    bool gen_lvalue(TypePtr& type) {
        if (peek().kind == TokenKind::Identifier) {
            const std::string name = take().text;
            const Variable* variable = lookup(name);
            if (variable == nullptr) {
                return false;
            }
            gen_lvalue_address(name);
            type = variable->type;
            bool address = true;
            if (type->kind == Type::Kind::Pointer &&
                (peek().text == "[" || peek().text == "->")) {
                gen_load(type);
                address = false;
            }
            type = apply_postfix(type, address);
            return address;
        }
        if (match("*")) {
            TypePtr pointer = parse_unary();
            if (!pointer->is_pointer()) {
                error("dereference of a non-pointer (" + type_name(pointer) +
                      ") near '" + peek().text + "'");
            }
            type = pointer->element();
            bool address = true;
            type = apply_postfix(type, address);
            return address;
        }
        return false;
    }

    void parse_assignment_statement() {
        gen_expression();
    }

    void gen_lvalue_address(const std::string& name) {
        const Variable* variable = lookup(name);
        if (variable == nullptr) {
            error("unknown variable: " + name);
        }
        if (variable->global) {
            emit("    LEA R0, [" + variable->label + "]");
        } else {
            emit("    LEA R0, [R7 + " + std::to_string(variable->offset) + "]");
        }
    }

    const Variable* lookup(const std::string& name) const {
        const auto local = locals_.find(name);
        if (local != locals_.end()) {
            return &local->second;
        }
        const auto global = globals_.find(name);
        if (global != globals_.end()) {
            return &global->second;
        }
        return nullptr;
    }

    void gen_load(const TypePtr& type) {
        if (type->kind == Type::Kind::Char) {
            emit(type->is_unsigned ? "    LOAD8U R0, [R0]" : "    LOAD8S R0, [R0]");
        } else if (type->kind == Type::Kind::Bool) {
            emit("    LOAD8S R0, [R0]");
        } else if (type->kind == Type::Kind::Float) {
            emit("    FLOAD.W F0, [R0]");
        } else if (type->kind == Type::Kind::Double) {
            emit("    FLOAD.D F0, [R0]");
        } else if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Struct ||
                   type->kind == Type::Kind::Union) {
            // Array and aggregate expressions are addresses.
        } else {
            emit("    LOAD R0, [R0]");
        }
    }

    void gen_store(const TypePtr& type) {
        if (type->kind == Type::Kind::Char || type->kind == Type::Kind::Bool) {
            emit("    STORE8 R0, [R1]");
        } else if (type->kind == Type::Kind::Float) {
            emit("    FSTORE.W F0, [R1]");
        } else if (type->kind == Type::Kind::Double) {
            emit("    FSTORE.D F0, [R1]");
        } else if (type->kind == Type::Kind::Struct || type->kind == Type::Kind::Union) {
            error("aggregate assignment is not supported yet");
        } else {
            emit("    STORE R0, [R1]");
        }
    }

    void convert_value(const TypePtr& from, const TypePtr& to) {
        if (from == nullptr || to == nullptr || from->kind == to->kind) {
            return;
        }
        const bool from_fp = is_fp_type(from);
        if (to->kind == Type::Kind::Float || to->kind == Type::Kind::Double) {
            const bool wide = to->kind == Type::Kind::Double;
            if (from_fp) {
                if (from->kind == Type::Kind::Float && wide) {
                    emit("    FCVT.D.S F0, F0");
                } else if (from->kind == Type::Kind::Double && !wide) {
                    emit("    FCVT.S.D F0, F0");
                }
            } else {
                emit(std::string("    ") + (wide ? "FCVT.D.W" : "FCVT.S.W") + " F0, R0");
            }
            return;
        }
        if (from_fp) {
            emit(std::string("    ") +
                 (from->kind == Type::Kind::Double ? "FCVT.W.D" : "FCVT.W.S") + " R0, F0");
        }
        if (to->kind == Type::Kind::Char) {
            emit("    AND R0, R0, #255");
            if (!to->is_unsigned) {
                emit("    SHL R0, R0, #24");
                emit("    SAR R0, R0, #24");
            }
        } else if (to->kind == Type::Kind::Bool) {
            const std::string nonzero = new_label("bool");
            const std::string done = new_label("boolend");
            emit("    CMP R0, #0");
            emit("    JNZ " + nonzero);
            emit("    MOV R0, #0");
            emit("    JMP " + done);
            emit(nonzero + ":");
            emit("    MOV R0, #1");
            emit(done + ":");
        }
    }

    void value_to_integer(const TypePtr& type) {
        if (type != nullptr && type->kind == Type::Kind::Double) {
            emit("    FCVT.W.D R0, F0");
        } else if (type != nullptr && type->kind == Type::Kind::Float) {
            emit("    FCVT.W.S R0, F0");
        }
    }

    TypePtr gen_condition() {
        TypePtr type = gen_expression();
        value_to_integer(type);
        return type;
    }

    TypePtr gen_expression() {
        return gen_assignment_expression();
    }

    void push_value(const TypePtr& type) {
        if (is_fp_type(type)) {
            emit("    ADJSP #-8");
            emit("    FSTORE.D F0, [SP]");
        } else {
            emit("    PUSH R0");
        }
    }

    void gen_fp_binary(const std::string& op, const TypePtr& left, const TypePtr& right) {
        if (is_fp_type(left)) {
            emit("    FLOAD.D F1, [SP]");
            emit("    ADJSP #8");
        } else {
            emit("    POP R1");
        }
        const TypePtr common = common_fp_type(left, right);
        const std::string suffix = common->kind == Type::Kind::Double ? ".D" : ".S";
        if (is_fp_type(left)) {
            if (left->kind == Type::Kind::Float && common->kind == Type::Kind::Double) {
                emit("    FCVT.D.S F1, F1");
            } else if (left->kind == Type::Kind::Double &&
                       common->kind == Type::Kind::Float) {
                emit("    FCVT.S.D F1, F1");
            }
        } else {
            emit(std::string("    FCVT") + suffix + ".W F1, R1");
        }
        if (is_fp_type(right)) {
            if (right->kind == Type::Kind::Float && common->kind == Type::Kind::Double) {
                emit("    FCVT.D.S F0, F0");
            } else if (right->kind == Type::Kind::Double &&
                       common->kind == Type::Kind::Float) {
                emit("    FCVT.S.D F0, F0");
            }
        } else {
            emit(std::string("    FCVT") + suffix + ".W F0, R0");
        }

        if (op == "+") {
            emit("    FADD" + suffix + " F0, F1, F0");
            return;
        }
        if (op == "-") {
            emit("    FSUB" + suffix + " F0, F1, F0");
            return;
        }
        if (op == "*") {
            emit("    FMUL" + suffix + " F0, F1, F0");
            return;
        }
        if (op == "/") {
            emit("    FDIV" + suffix + " F0, F1, F0");
            return;
        }
        if (op == "fmin") {
            emit("    FMIN" + suffix + " F0, F1, F0");
            return;
        }
        if (op == "fmax") {
            emit("    FMAX" + suffix + " F0, F1, F0");
            return;
        }
        if (op == "==") {
            emit("    FEQ" + suffix + " R0, F1, F0");
            return;
        }
        if (op == "!=") {
            emit("    FEQ" + suffix + " R0, F1, F0");
            emit("    XOR R0, R0, #1");
            return;
        }
        if (op == "<") {
            emit("    FLT" + suffix + " R0, F1, F0");
            return;
        }
        if (op == "<=") {
            emit("    FLE" + suffix + " R0, F1, F0");
            return;
        }
        if (op == ">") {
            emit("    FLT" + suffix + " R0, F0, F1");
            return;
        }
        if (op == ">=") {
            emit("    FLE" + suffix + " R0, F0, F1");
            return;
        }
        error("operator '" + op + "' is not supported for floating-point values");
    }

    void gen_binary(const std::string& op, const TypePtr& left, const TypePtr& right) {
        if (is_fp_type(left) || is_fp_type(right)) {
            gen_fp_binary(op, left, right);
            return;
        }
        emit("    POP R1");
        if (op == "+" || op == "-") {
            if (left->is_pointer() && !right->is_pointer()) {
                const int size = left->element() ? left->element()->size() : 1;
                if (size > 1) {
                    emit("    MUL R0, R0, #" + std::to_string(size));
                }
                emit(std::string("    ") + (op == "+" ? "ADD" : "SUB") + " R0, R1, R0");
                return;
            }
            if (!left->is_pointer() && right->is_pointer() && op == "+") {
                const int size = right->element() ? right->element()->size() : 1;
                if (size > 1) {
                    emit("    MUL R1, R1, #" + std::to_string(size));
                }
                emit("    ADD R0, R1, R0");
                return;
            }
            if (left->is_pointer() && right->is_pointer() && op == "-") {
                const int size = left->element() ? left->element()->size() : 1;
                emit("    SUB R0, R1, R0");
                if (size > 1) {
                    emit("    DIV R0, R0, #" + std::to_string(size));
                }
                return;
            }
            emit(std::string("    ") + (op == "+" ? "ADD" : "SUB") + " R0, R1, R0");
            return;
        }
        if (op == "*") {
            emit("    MUL R0, R1, R0");
            return;
        }
        if (op == "/") {
            emit(is_unsigned_scalar(left) || is_unsigned_scalar(right)
                     ? "    DIV R0, R1, R0"
                     : "    DIVS R0, R1, R0");
            return;
        }
        if (op == "%") {
            emit(is_unsigned_scalar(left) || is_unsigned_scalar(right)
                     ? "    MOD R0, R1, R0"
                     : "    MODS R0, R1, R0");
            return;
        }
        if (op == "&") {
            emit("    AND R0, R1, R0");
            return;
        }
        if (op == "|") {
            emit("    OR R0, R1, R0");
            return;
        }
        if (op == "^") {
            emit("    XOR R0, R1, R0");
            return;
        }
        if (op == "<<" || op == ">>") {
            const bool logical_shift = op == ">>" &&
                                       (is_unsigned_scalar(left) ||
                                        is_unsigned_scalar(right));
            emit(std::string("    ") + (op == "<<" ? "SHL" : (logical_shift ? "SHR" : "SAR")) +
                 " R0, R1, R0");
            return;
        }
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
            op == ">=") {
            const std::string true_label = new_label("true");
            const std::string end_label = new_label("cmpend");
            const bool unsigned_compare = is_unsigned_scalar(left) ||
                                          is_unsigned_scalar(right);
            emit("    CMP R1, R0");
            if (op == "==") emit("    JZ " + true_label);
            if (op == "!=") emit("    JNZ " + true_label);
            if (unsigned_compare) {
                if (op == "<") emit("    JNC " + true_label);
                if (op == "<=") emit("    JLS " + true_label);
                if (op == ">") emit("    JHI " + true_label);
                if (op == ">=") emit("    JCS " + true_label);
            } else {
                if (op == "<") emit("    JLT " + true_label);
                if (op == "<=") emit("    JLE " + true_label);
                if (op == ">") emit("    JGT " + true_label);
                if (op == ">=") emit("    JGE " + true_label);
            }
            emit("    MOV R0, #0");
            emit("    JMP " + end_label);
            emit(true_label + ":");
            emit("    MOV R0, #1");
            emit(end_label + ":");
        }
    }

    static bool is_assignment_operator(std::string_view text) {
        return text == "=" || text == "+=" || text == "-=" || text == "*=" ||
               text == "/=" || text == "%=" || text == "<<=" || text == ">>=" ||
               text == "&=" || text == "|=" || text == "^=";
    }

    void gen_increment(const TypePtr& type, int delta, bool prefix) {
        if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Struct ||
            type->kind == Type::Kind::Union) {
            error("increment/decrement requires a scalar lvalue");
        }
        if (is_fp_type(type)) {
            const int address_slot = allocate_local(type_int());
            emit("    STORE R0, [R7 + " + std::to_string(address_slot) + "]");
            emit("    LOAD R0, [R7 + " + std::to_string(address_slot) + "]");
            gen_load(type);
            int old_slot = -1;
            if (!prefix) {
                old_slot = allocate_local(type_double());
                emit("    FSTORE.D F0, [R7 + " + std::to_string(old_slot) + "]");
            }
            const bool wide = type->kind == Type::Kind::Double;
            const std::string label = new_label("fpone");
            rodata_ += label + ":\n    ." + (wide ? "double" : "float") + " 1.0\n";
            emit(std::string("    FLOAD.") + (wide ? "D" : "W") + " F1, [" + label + "]");
            emit(std::string("    ") + (delta > 0 ? "FADD" : "FSUB") + (wide ? ".D" : ".S") +
                 " F0, F0, F1");
            emit("    LOAD R1, [R7 + " + std::to_string(address_slot) + "]");
            gen_store(type);
            if (!prefix) {
                emit("    FLOAD.D F0, [R7 + " + std::to_string(old_slot) + "]");
            }
            return;
        }
        const int address_slot = allocate_local(type_int());
        emit("    STORE R0, [R7 + " + std::to_string(address_slot) + "]");
        emit("    LOAD R0, [R7 + " + std::to_string(address_slot) + "]");
        gen_load(type);
        int old_slot = -1;
        if (!prefix) {
            old_slot = allocate_local(type_int());
            emit("    STORE R0, [R7 + " + std::to_string(old_slot) + "]");
        }
        int amount = delta;
        if (type->is_pointer()) {
            const int size = type->element() ? type->element()->size() : 1;
            amount *= size;
        }
        if (amount >= 0) {
            emit("    ADD R0, R0, #" + std::to_string(amount));
        } else {
            emit("    SUB R0, R0, #" + std::to_string(-amount));
        }
        if (type->kind == Type::Kind::Bool) {
            convert_value(type_int(), type);
        }
        emit("    LOAD R1, [R7 + " + std::to_string(address_slot) + "]");
        gen_store(type);
        if (!prefix) {
            emit("    LOAD R0, [R7 + " + std::to_string(old_slot) + "]");
        }
    }

    void gen_compound_assignment(const TypePtr& type, const std::string& op) {
        if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Struct ||
            type->kind == Type::Kind::Union) {
            error("compound assignment requires a scalar lvalue");
        }
        const int address_slot = allocate_local(type_int());
        emit("    STORE R0, [R7 + " + std::to_string(address_slot) + "]");
        emit("    LOAD R0, [R7 + " + std::to_string(address_slot) + "]");
        gen_load(type);
        push_value(type);
        TypePtr right = gen_expression();
        if (type->is_pointer()) {
            if (op != "+" && op != "-") {
                error("only += and -= are valid for pointer compound assignment");
            }
            if (right->is_pointer()) {
                error("pointer compound assignment requires an integer operand");
            }
        } else if (is_fp_type(right) && !is_fp_type(type)) {
            convert_value(right, type);
            right = type;
        }
        gen_binary(op, type, right);
        if (type->kind == Type::Kind::Bool) {
            convert_value(type_int(), type);
        }
        emit("    LOAD R1, [R7 + " + std::to_string(address_slot) + "]");
        gen_store(type);
    }

    TypePtr gen_assignment_expression() {
        const bool looks_lvalue =
            (peek().kind == TokenKind::Identifier &&
             (is_assignment_operator(peek(1).text) || peek(1).text == "[" ||
              peek(1).text == "." || peek(1).text == "->")) ||
            peek().text == "*";
        if (looks_lvalue) {
            const std::size_t saved_position = position_;
            const std::size_t saved_text = text_.size();
            const std::size_t saved_rodata = rodata_.size();
            const std::size_t saved_data = data_.size();
            const std::size_t saved_bss = bss_.size();
            const std::size_t saved_debug = debug_.size();
            const int saved_frame = frame_size_;
            const int saved_label = label_counter_;
            TypePtr type;
            if (gen_lvalue(type) && is_assignment_operator(peek().text)) {
                const std::string op = take().text;
                if (op == "=") {
                    emit("    PUSH R0");
                    TypePtr value_type = gen_expression();
                    convert_value(value_type, type);
                    emit("    POP R1");
                    gen_store(type);
                } else {
                    gen_compound_assignment(type, op.substr(0, op.size() - 1));
                }
                return type;
            }
            position_ = saved_position;
            text_.resize(saved_text);
            rodata_.resize(saved_rodata);
            data_.resize(saved_data);
            bss_.resize(saved_bss);
            debug_.resize(saved_debug);
            frame_size_ = saved_frame;
            label_counter_ = saved_label;
        }
        return parse_conditional();
    }

    TypePtr parse_conditional() {
        TypePtr condition = parse_logical_or();
        if (!match("?")) {
            return condition;
        }
        value_to_integer(condition);
        const std::string else_label = new_label("condelse");
        const std::string end_label = new_label("condend");
        emit("    CMP R0, #0");
        emit("    JZ " + else_label);
        TypePtr result = gen_expression();
        expect(":");
        emit("    JMP " + end_label);
        emit(else_label + ":");
        TypePtr alternative = gen_expression();
        convert_value(alternative, result);
        emit(end_label + ":");
        return result;
    }

    void gen_binary_chain(TypePtr (Compiler::*next)(), const std::vector<std::string>& ops) {
        TypePtr left = (this->*next)();
        while (true) {
            bool matched = false;
            for (const std::string& op : ops) {
                if (peek().text == op) {
                    take();
                    push_value(left);
                    TypePtr right = (this->*next)();
                    const bool comparison = op == "==" || op == "!=" || op == "<" ||
                                            op == "<=" || op == ">" || op == ">=";
                    gen_binary(op, left, right);
                    left = comparison
                               ? type_int()
                               : (is_fp_type(left) || is_fp_type(right)
                                      ? common_fp_type(left, right)
                                      : (is_unsigned_scalar(left) ||
                                                 is_unsigned_scalar(right)
                                             ? type_uint()
                                             : left));
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return;
            }
        }
    }

    TypePtr parse_multiplicative() { return parse_unary(); }
    TypePtr parse_additive() { return parse_multiplicative(); }
    TypePtr parse_shift() { return parse_additive(); }
    TypePtr parse_relational() { return parse_shift(); }
    TypePtr parse_equality() { return parse_relational(); }
    TypePtr parse_bit_and() { return parse_equality(); }
    TypePtr parse_bit_xor() { return parse_bit_and(); }
    TypePtr parse_bit_or() { return parse_bit_xor(); }

    TypePtr parse_logical_and() {
        TypePtr left = parse_bit_or_real();
        value_to_integer(left);
        while (match("&&")) {
            const std::string false_label = new_label("andfalse");
            const std::string end_label = new_label("andend");
            emit("    CMP R0, #0");
            emit("    JZ " + false_label);
            TypePtr right = parse_bit_or_real();
            value_to_integer(right);
            emit("    CMP R0, #0");
            emit("    JZ " + false_label);
            emit("    MOV R0, #1");
            emit("    JMP " + end_label);
            emit(false_label + ":");
            emit("    MOV R0, #0");
            emit(end_label + ":");
            left = type_int();
        }
        return left;
    }

    TypePtr parse_logical_or() {
        TypePtr left = parse_logical_and();
        value_to_integer(left);
        while (match("||")) {
            const std::string true_label = new_label("ortrue");
            const std::string end_label = new_label("orend");
            emit("    CMP R0, #0");
            emit("    JNZ " + true_label);
            TypePtr right = parse_logical_and();
            value_to_integer(right);
            emit("    CMP R0, #0");
            emit("    JNZ " + true_label);
            emit("    MOV R0, #0");
            emit("    JMP " + end_label);
            emit(true_label + ":");
            emit("    MOV R0, #1");
            emit(end_label + ":");
            left = type_int();
        }
        return left;
    }

    const Member* find_member(const TypePtr& type, const std::string& name) const {
        if (type == nullptr) {
            return nullptr;
        }
        for (const Member& member : type->members) {
            if (member.name == name) {
                return &member;
            }
        }
        return nullptr;
    }

    TypePtr apply_postfix(TypePtr type, bool& address) {
        while (true) {
            if (match("[")) {
                if (!type->is_pointer()) {
                    error("subscript of a non-array");
                }
                emit("    PUSH R0");
                gen_expression();
                expect("]");
                emit("    POP R1");
                const int size = type->element() ? type->element()->size() : 1;
                if (size > 1) {
                    emit("    MUL R0, R0, #" + std::to_string(size));
                }
                emit("    ADD R0, R1, R0");
                type = type->element();
                address = true;
                continue;
            }
            if (match(".")) {
                if (type->kind != Type::Kind::Struct && type->kind != Type::Kind::Union) {
                    error("member access on a non-struct");
                }
                if (!address) {
                    error("member access requires an address");
                }
                const std::string member_name = parse_name();
                const Member* member = find_member(type, member_name);
                if (member == nullptr) {
                    error("unknown member: " + member_name);
                }
                if (member->offset != 0) {
                    emit("    ADD R0, R0, #" + std::to_string(member->offset));
                }
                type = member->type;
                address = true;
                continue;
            }
            if (match("->")) {
                if (type->kind != Type::Kind::Pointer ||
                    (type->element()->kind != Type::Kind::Struct &&
                     type->element()->kind != Type::Kind::Union)) {
                    error("'->' requires a pointer to a struct");
                }
                if (address) {
                    gen_load(type);
                    address = false;
                }
                const std::string member_name = parse_name();
                const Member* member = find_member(type->element(), member_name);
                if (member == nullptr) {
                    error("unknown member: " + member_name);
                }
                if (member->offset != 0) {
                    emit("    ADD R0, R0, #" + std::to_string(member->offset));
                }
                type = member->type;
                address = true;
                continue;
            }
            if (peek().text == "++" || peek().text == "--") {
                if (!address) {
                    error("increment/decrement requires an lvalue");
                }
                const std::string op = take().text;
                gen_increment(type, op == "++" ? 1 : -1, false);
                address = false;
                break;
            }
            break;
        }
        return type;
    }

    TypePtr parse_unevaluated_unary() {
        const std::size_t text_size = text_.size();
        const std::size_t rodata_size = rodata_.size();
        const std::size_t data_size = data_.size();
        const std::size_t bss_size = bss_.size();
        const std::size_t debug_size = debug_.size();
        const int saved_frame = frame_size_;
        const int saved_labels = label_counter_;
        const int saved_strings = string_counter_;
        TypePtr type = parse_unary();
        text_.resize(text_size);
        rodata_.resize(rodata_size);
        data_.resize(data_size);
        bss_.resize(bss_size);
        debug_.resize(debug_size);
        frame_size_ = saved_frame;
        label_counter_ = saved_labels;
        string_counter_ = saved_strings;
        return type;
    }

    TypePtr parse_unevaluated_expression() {
        const std::size_t text_size = text_.size();
        const std::size_t rodata_size = rodata_.size();
        const std::size_t data_size = data_.size();
        const std::size_t bss_size = bss_.size();
        const std::size_t debug_size = debug_.size();
        const int saved_frame = frame_size_;
        const int saved_labels = label_counter_;
        const int saved_strings = string_counter_;
        TypePtr type = gen_expression();
        text_.resize(text_size);
        rodata_.resize(rodata_size);
        data_.resize(data_size);
        bss_.resize(bss_size);
        debug_.resize(debug_size);
        frame_size_ = saved_frame;
        label_counter_ = saved_labels;
        string_counter_ = saved_strings;
        return type;
    }

    TypePtr parse_sizeof(bool alignment) {
        TypePtr type;
        if (match("(")) {
            if (is_type_start()) {
                type = parse_type();
                expect(")");
            } else {
                type = parse_unevaluated_expression();
                expect(")");
            }
        } else {
            type = parse_unevaluated_unary();
        }
        const int value = alignment ? type->alignment() : type->size();
        emit("    MOV R0, #" + std::to_string(value));
        return type_int();
    }

    TypePtr parse_offsetof() {
        expect("(");
        TypePtr type = parse_type();
        expect(",");
        const std::string member_name = parse_name();
        expect(")");
        const Member* member = find_member(type, member_name);
        if (member == nullptr) {
            error("unknown member in offsetof: " + member_name);
        }
        emit("    MOV R0, #" + std::to_string(member->offset));
        return type_int();
    }

    TypePtr parse_unary() {
        if (peek().text == "sizeof") {
            take();
            return parse_sizeof(false);
        }
        if (peek().text == "_Alignof" || peek().text == "alignof") {
            take();
            return parse_sizeof(true);
        }
        if (peek().text == "__builtin_offsetof") {
            take();
            return parse_offsetof();
        }
        if (peek().text == "++" || peek().text == "--") {
            const std::string op = take().text;
            TypePtr type;
            if (!gen_lvalue(type)) {
                error("increment/decrement requires an lvalue");
            }
            gen_increment(type, op == "++" ? 1 : -1, true);
            return type;
        }
        if (match("-")) {
            TypePtr type = parse_unary();
            if (is_fp_type(type)) {
                emit(type->kind == Type::Kind::Double ? "    FNEG.D F0, F0"
                                                      : "    FNEG.S F0, F0");
            } else {
                emit("    NEG R0, R0");
            }
            return type;
        }
        if (match("!")) {
            TypePtr type = parse_unary();
            if (is_fp_type(type)) {
                emit(type->kind == Type::Kind::Double ? "    FCVT.W.D R0, F0"
                                                      : "    FCVT.W.S R0, F0");
            }
            const std::string true_label = new_label("nottrue");
            const std::string end_label = new_label("notend");
            emit("    CMP R0, #0");
            emit("    JZ " + true_label);
            emit("    MOV R0, #0");
            emit("    JMP " + end_label);
            emit(true_label + ":");
            emit("    MOV R0, #1");
            emit(end_label + ":");
            return type_int();
        }
        if (match("~")) {
            TypePtr type = parse_unary();
            emit("    NOT R0, R0");
            return type;
        }
        if (match("*")) {
            TypePtr pointer = parse_unary();
            if (!pointer->is_pointer()) {
                error("dereference of a non-pointer");
            }
            gen_load(pointer->element());
            return pointer->element();
        }
        if (match("&")) {
            if (peek().kind != TokenKind::Identifier) {
                error("address-of requires a variable");
            }
            const std::string name = take().text;
            gen_lvalue_address(name);
            const Variable* variable = lookup(name);
            return variable != nullptr ? type_pointer(variable->type) : type_pointer(type_int());
        }
        if (peek().kind == TokenKind::Float) {
            const Token& token = take();
            return emit_fp_constant(token.real, token.is_float);
        }
        if (peek().kind == TokenKind::Number) {
            const Token token = take();
            emit("    MOV R0, #" + std::to_string(token.value));
            return token.is_unsigned ? type_uint() : type_int();
        }
        if (peek().kind == TokenKind::Character) {
            const std::int64_t value = take().value;
            emit("    MOV R0, #" + std::to_string(value));
            return type_char();
        }
        if (peek().kind == TokenKind::String) {
            const std::string bytes = take().text;
            const std::string label = ".Lstr_" + std::to_string(string_counter_++);
            rodata_ += label + ":\n    .asciz \"" + escape_string(bytes) + "\"\n";
            emit("    LEA R0, [" + label + "]");
            bool address = false;
            TypePtr type = apply_postfix(type_pointer(type_char()), address);
            if (address && type->kind != Type::Kind::Struct &&
                type->kind != Type::Kind::Union && type->kind != Type::Kind::Array) {
                gen_load(type);
            }
            return type;
        }
        if (match("(")) {
            if (is_type_start()) {
                TypePtr target = parse_type();
                expect(")");
                TypePtr source = parse_unary();
                convert_value(source, target);
                return target;
            }
            TypePtr type = gen_expression();
            expect(")");
            bool address = false;
            type = apply_postfix(type, address);
            if (address && type->kind != Type::Kind::Struct &&
                type->kind != Type::Kind::Union && type->kind != Type::Kind::Array) {
                gen_load(type);
            }
            return type;
        }
        if (peek().text == "__builtin_va_start") {
            if (current_function_.empty()) {
                error("__builtin_va_start outside a function");
            }
            take();
            expect("(");
            if (peek().kind != TokenKind::Identifier) {
                error("__builtin_va_start requires a va_list variable");
            }
            const std::string list_name = take().text;
            expect(")");
            gen_lvalue_address(list_name);
            emit("    MOV R1, R0");
            emit("    LEA R0, [R7 + __FRAME_" + current_function_ + "__ + 8]");
            emit("    STORE R0, [R1]");
            return type_pointer(type_char());
        }
        if (peek().text == "__builtin_va_copy") {
            take();
            expect("(");
            if (peek().kind != TokenKind::Identifier) {
                error("__builtin_va_copy requires a destination");
            }
            const std::string destination = take().text;
            expect(",");
            if (peek().kind != TokenKind::Identifier) {
                error("__builtin_va_copy requires a source");
            }
            const std::string source = take().text;
            expect(")");
            gen_lvalue_address(source);
            emit("    LOAD R1, [R0]");
            gen_lvalue_address(destination);
            emit("    STORE R1, [R0]");
            return type_pointer(type_char());
        }
        if (peek().text == "__builtin_va_arg") {
            take();
            expect("(");
            if (peek().kind != TokenKind::Identifier) {
                error("va_arg requires a va_list variable");
            }
            const std::string list_name = take().text;
            expect(",");
            TypePtr type = parse_type();
            expect(")");
            gen_lvalue_address(list_name);
            emit("    MOV R2, R0");
            emit("    LOAD R1, [R2]");
            if (type->kind == Type::Kind::Double) {
                emit("    FLOAD.D F0, [R1]");
            } else if (type->kind == Type::Kind::Float) {
                emit("    FLOAD.W F0, [R1]");
            } else if (type->kind == Type::Kind::Char || type->kind == Type::Kind::Bool) {
                emit("    LOAD8S R0, [R1]");
            } else {
                emit("    LOAD R0, [R1]");
            }
            emit("    ADD R1, R1, #8");
            emit("    STORE R1, [R2]");
            return type;
        }
        if (peek().text == "__builtin_sqrt" || peek().text == "__builtin_fabs") {
            const bool square_root = peek().text == "__builtin_sqrt";
            take();
            expect("(");
            TypePtr type = gen_expression();
            expect(")");
            if (!is_fp_type(type)) {
                error("floating-point builtin requires a float or double");
            }
            const std::string suffix =
                type->kind == Type::Kind::Double ? ".D" : ".S";
            emit(std::string("    ") + (square_root ? "FSQRT" : "FABS") + suffix +
                 " F0, F0");
            return type;
        }
        if (peek().text == "__builtin_fmin" || peek().text == "__builtin_fmax") {
            const std::string operation =
                peek().text == "__builtin_fmin" ? "fmin" : "fmax";
            take();
            expect("(");
            TypePtr left = gen_expression();
            if (!is_fp_type(left)) {
                error("fmin/fmax require float or double arguments");
            }
            expect(",");
            push_value(left);
            TypePtr right = gen_expression();
            if (!is_fp_type(right)) {
                error("fmin/fmax require float or double arguments");
            }
            expect(")");
            gen_binary(operation, left, right);
            return common_fp_type(left, right);
        }
        if (peek().text == "__builtin_bswap32" || peek().text == "__builtin_bswap" ||
            peek().text == "__builtin_clz" || peek().text == "__builtin_ctz" ||
            peek().text == "__builtin_popcount") {
            const std::string name = peek().text;
            take();
            expect("(");
            gen_expression();
            expect(")");
            if (name == "__builtin_bswap32" || name == "__builtin_bswap") {
                emit("    BSWAP R0, R0");
            } else if (name == "__builtin_clz") {
                emit("    CLZ R0, R0");
            } else if (name == "__builtin_ctz") {
                emit("    CTZ R0, R0");
            } else {
                emit("    POPCNT R0, R0");
            }
            return type_int();
        }
        if (peek().text == "__builtin_add_overflow" ||
            peek().text == "__builtin_sub_overflow" ||
            peek().text == "__builtin_mul_overflow") {
            const std::string operation = peek().text;
            take();
            expect("(");
            gen_expression();
            const int left_slot = allocate_local(type_int());
            emit("    STORE R0, [R7 + " + std::to_string(left_slot) + "]");
            expect(",");
            gen_expression();
            const int right_slot = allocate_local(type_int());
            emit("    STORE R0, [R7 + " + std::to_string(right_slot) + "]");
            expect(",");
            gen_expression();
            expect(")");
            const int output_slot = allocate_local(type_int());
            emit("    STORE R0, [R7 + " + std::to_string(output_slot) + "]");
            emit("    LOAD R1, [R7 + " + std::to_string(left_slot) + "]");
            emit("    LOAD R0, [R7 + " + std::to_string(right_slot) + "]");
            if (operation == "__builtin_add_overflow") {
                emit("    ADD R0, R1, R0");
            } else if (operation == "__builtin_sub_overflow") {
                emit("    SUB R0, R1, R0");
            } else {
                emit("    MUL R0, R1, R0");
            }
            const std::string overflow = new_label("overflow");
            const std::string done = new_label("overflowdone");
            emit("    JVS " + overflow);
            emit("    LOAD R1, [R7 + " + std::to_string(output_slot) + "]");
            emit("    STORE R0, [R1]");
            emit("    MOV R0, #0");
            emit("    JMP " + done);
            emit(overflow + ":");
            emit("    LOAD R1, [R7 + " + std::to_string(output_slot) + "]");
            emit("    STORE R0, [R1]");
            emit("    MOV R0, #1");
            emit(done + ":");
            return type_int();
        }
        if (peek().text == "__builtin_ll") {
            take();
            expect("(");
            gen_expression();
            expect(")");
            emit("    LL R0, [R0]");
            return type_int();
        }
        if (peek().text == "__builtin_sc") {
            take();
            expect("(");
            gen_expression();
            emit("    PUSH R0");
            expect(",");
            gen_expression();
            expect(")");
            emit("    POP R1");
            emit("    SC R0, [R1]");
            const std::string failed = new_label("scfailed");
            const std::string done = new_label("scdone");
            emit("    JNZ " + failed);
            emit("    MOV R0, #1");
            emit("    JMP " + done);
            emit(failed + ":");
            emit("    MOV R0, #0");
            emit(done + ":");
            return type_int();
        }
        if (peek().text == "__builtin_fence") {
            take();
            expect("(");
            expect(")");
            emit("    FENCE");
            return type_int();
        }
        if (peek().text == "__builtin_heap_start") {
            take();
            expect("(");
            expect(")");
            emit("    LEA R0, [__heap_start]");
            return type_pointer(type_char());
        }
        if (peek().text == "__builtin_heap_end") {
            take();
            expect("(");
            expect(")");
            emit("    LEA R0, [__heap_end]");
            return type_pointer(type_char());
        }
        if (peek().kind == TokenKind::Identifier) {
            const std::string name = take().text;
            if (match("(")) {
                std::vector<TypePtr> arguments;
                std::vector<int> slots;
                std::vector<TypePtr> target_types;
                const auto signature = functions_.find(name);
                const bool variadic =
                    signature != functions_.end() && signature->second.variadic;
                const std::size_t fixed_count =
                    signature != functions_.end() ? signature->second.parameters.size() : 0;
                if (!match(")")) {
                    while (true) {
                        TypePtr argument = gen_expression();
                        const std::size_t index = arguments.size();
                        const bool extra = variadic && index >= fixed_count;
                        TypePtr target = argument;
                        if (signature != functions_.end() && index < fixed_count) {
                            target = signature->second.parameters[index];
                        } else if (extra) {
                            if (argument->kind == Type::Kind::Float) {
                                target = type_double();
                            } else if (argument->kind == Type::Kind::Char ||
                                       argument->kind == Type::Kind::Bool) {
                                target = type_int();
                            }
                        }
                        convert_value(argument, target);
                        const int slot = allocate_local(extra ? type_double() : target);
                        if (extra) {
                            if (is_fp_type(target)) {
                                emit("    FSTORE.D F0, [R7 + " + std::to_string(slot) + "]");
                            } else {
                                emit("    STORE R0, [R7 + " + std::to_string(slot) + "]");
                                emit("    MOV R1, #0");
                                emit("    STORE R1, [R7 + " + std::to_string(slot + 4) + "]");
                            }
                        } else if (is_fp_type(target)) {
                            emit(std::string("    FSTORE") +
                                 (target->kind == Type::Kind::Double ? ".D" : ".W") +
                                 " F0, [R7 + " + std::to_string(slot) + "]");
                        } else {
                            emit("    STORE R0, [R7 + " + std::to_string(slot) + "]");
                        }
                        arguments.push_back(argument);
                        target_types.push_back(target);
                        slots.push_back(slot);
                        if (!match(",")) {
                            break;
                        }
                    }
                    expect(")");
                }
                if (!variadic && arguments.size() > 4) {
                    error("more than four arguments are not supported yet");
                }
                const std::size_t extra_count =
                    variadic && slots.size() > fixed_count ? slots.size() - fixed_count : 0;
                for (std::size_t offset = 0; offset < extra_count; ++offset) {
                    const std::size_t index = fixed_count + extra_count - 1 - offset;
                    emit("    ADJSP #-8");
                    if (is_fp_type(target_types[index])) {
                        emit("    FLOAD.D F0, [R7 + " + std::to_string(slots[index]) + "]");
                        emit("    FSTORE.D F0, [SP]");
                    } else {
                        emit("    LOAD R0, [R7 + " + std::to_string(slots[index]) + "]");
                        emit("    STORE R0, [SP]");
                        emit("    MOV R1, #0");
                        emit("    STORE R1, [SP + 4]");
                    }
                }
                const std::size_t register_count =
                    variadic ? std::min(fixed_count, slots.size()) : slots.size();
                int integer_register = 0;
                int fp_register = 0;
                for (std::size_t index = 0; index < register_count; ++index) {
                    if (is_fp_type(target_types[index])) {
                        emit(std::string("    FLOAD") +
                             (target_types[index]->kind == Type::Kind::Double ? ".D" : ".W") +
                             " F" + std::to_string(fp_register) + ", [R7 + " +
                             std::to_string(slots[index]) + "]");
                        ++fp_register;
                    } else {
                        emit("    LOAD R" + std::to_string(integer_register) + ", [R7 + " +
                             std::to_string(slots[index]) + "]");
                        ++integer_register;
                    }
                }
                emit("    CALL " + name);
                if (extra_count > 0) {
                    emit("    ADJSP #" + std::to_string(extra_count * 8));
                }
                if (signature != functions_.end()) {
                    return signature->second.result;
                }
                return type_int();
            }
            const Variable* variable = lookup(name);
            if (variable == nullptr) {
                const auto constant = enum_constants_.find(name);
                if (constant != enum_constants_.end()) {
                    emit("    MOV R0, #" + std::to_string(constant->second));
                    return type_int();
                }
                error("unknown variable: " + name);
            }
            const TypePtr type = variable->type;
            bool address = type->kind == Type::Kind::Struct ||
                           type->kind == Type::Kind::Union ||
                           type->kind == Type::Kind::Array;
            gen_lvalue_address(name);
            if (peek().text == "++" || peek().text == "--") {
                const std::string op = take().text;
                gen_increment(type, op == "++" ? 1 : -1, false);
                return type;
            }
            if (!address) {
                gen_load(type);
            }
            TypePtr result = apply_postfix(type, address);
            if (address && result->kind != Type::Kind::Struct &&
                result->kind != Type::Kind::Union && result->kind != Type::Kind::Array) {
                gen_load(result);
            }
            return result;
        }
        error("expected an expression");
    }

    TypePtr parse_primary_with_binary(const std::vector<std::string>& ops,
                                      TypePtr (Compiler::*next)()) {
        TypePtr left = (this->*next)();
        while (true) {
            bool matched = false;
            for (const std::string& op : ops) {
                if (peek().text == op) {
                    take();
                    push_value(left);
                    TypePtr right = (this->*next)();
                    const bool comparison = op == "==" || op == "!=" || op == "<" ||
                                            op == "<=" || op == ">" || op == ">=";
                    gen_binary(op, left, right);
                    left = comparison
                               ? type_int()
                               : (is_fp_type(left) || is_fp_type(right)
                                      ? common_fp_type(left, right)
                                      : (is_unsigned_scalar(left) ||
                                                 is_unsigned_scalar(right)
                                             ? type_uint()
                                             : left));
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return left;
            }
        }
    }

    TypePtr parse_multiplicative_real() { return parse_primary_with_binary({"*", "/", "%"}, &Compiler::parse_unary); }
    TypePtr parse_additive_real() { return parse_primary_with_binary({"+", "-"}, &Compiler::parse_multiplicative_real); }
    TypePtr parse_shift_real() { return parse_primary_with_binary({"<<", ">>"}, &Compiler::parse_additive_real); }
    TypePtr parse_relational_real() { return parse_primary_with_binary({"<", "<=", ">", ">="}, &Compiler::parse_shift_real); }
    TypePtr parse_equality_real() { return parse_primary_with_binary({"==", "!="}, &Compiler::parse_relational_real); }
    TypePtr parse_bit_and_real() { return parse_primary_with_binary({"&"}, &Compiler::parse_equality_real); }
    TypePtr parse_bit_xor_real() { return parse_primary_with_binary({"^"}, &Compiler::parse_bit_and_real); }
    TypePtr parse_bit_or_real() { return parse_primary_with_binary({"|"}, &Compiler::parse_bit_xor_real); }

    std::string escape_string(const std::string& value) const {
        std::string escaped;
        for (char c : value) {
            if (c == '\\' || c == '"') {
                escaped.push_back('\\');
                escaped.push_back(c);
            } else if (c == '\n') {
                escaped += "\\n";
            } else if (c == '\r') {
                escaped += "\\r";
            } else if (c == '\t') {
                escaped += "\\t";
            } else {
                escaped.push_back(c);
            }
        }
        return escaped;
    }

    void replace_all(const std::string& needle, const std::string& value) {
        std::size_t position = 0;
        while ((position = text_.find(needle, position)) != std::string::npos) {
            text_.replace(position, needle.size(), value);
            position += value.size();
        }
    }

    std::string source_name_;
    int source_id_ = 0;
    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::string text_;
    std::string rodata_;
    std::string data_;
    std::string bss_;
    std::map<std::string, Variable> locals_;
    std::map<std::string, Variable> globals_;
    std::map<std::string, FunctionSignature> functions_;
    std::map<std::string, TypePtr> typedefs_;
    std::map<std::string, TypePtr> tags_;
    std::map<std::string, std::int64_t> enum_constants_;
    std::vector<std::string> break_labels_;
    std::vector<std::string> continue_labels_;
    std::string current_function_;
    TypePtr current_result_type_;
    std::string return_label_;
    std::string debug_;
    std::string* output_ = nullptr;
    int frame_size_ = 0;
    int line_counter_ = 0;
    int label_counter_ = 0;
    int string_counter_ = 0;
};

} // namespace

Result compile(std::string_view source, std::string_view source_name, int source_id) {
    Compiler compiler(source, source_name, source_id);
    return compiler.run();
}

Result compile(std::string_view source, std::string_view source_name) {
    return compile(source, source_name, 0);
}

std::string libc_assembly() {
    static const std::string assembly = [] {
        const Result result = compile(libc_source::kLibcSource, "src/cc/libc.c", 1);
        if (!result.ok) {
            const std::string diagnostic =
                result.diagnostics.empty()
                    ? "libc compilation failed"
                    : "line " + std::to_string(result.diagnostics.front().line) +
                          ", column " + std::to_string(result.diagnostics.front().column) +
                          ": " + result.diagnostics.front().message;
            throw std::runtime_error(diagnostic);
        }
        return result.assembly;
    }();
    return assembly;
}

std::string runtime_assembly() {
    return
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    CALL main\n"
        "    CALL exit\n"
        "    HALT\n"
        ".global putchar\n"
        "putchar:\n"
        "    SYSCALL #33\n"
        "    RET\n"
        ".global exit\n"
        "exit:\n"
        "    SYSCALL #32\n"
        "    HALT\n"
        ".global getchar\n"
        "getchar:\n"
        "    SYSCALL #34\n"
        "    RET\n"
        ".global open\n"
        "open:\n"
        "    SYSCALL #35\n"
        "    RET\n"
        ".global close\n"
        "close:\n"
        "    SYSCALL #36\n"
        "    RET\n"
        ".global read\n"
        "read:\n"
        "    SYSCALL #37\n"
        "    RET\n"
        ".global write\n"
        "write:\n"
        "    SYSCALL #38\n"
        "    RET\n"
        ".global seek\n"
        "seek:\n"
        "    SYSCALL #39\n"
        "    RET\n"
        ".global sys_time\n"
        "sys_time:\n"
        "    SYSCALL #40\n"
        "    RET\n"
        ".global sys_random\n"
        "sys_random:\n"
        "    SYSCALL #41\n"
        "    RET\n"
        ".global sys_argc\n"
        "sys_argc:\n"
        "    SYSCALL #42\n"
        "    RET\n"
        ".global sys_arg\n"
        "sys_arg:\n"
        "    SYSCALL #43\n"
        "    RET\n"
        ".global memcpy\n"
        "memcpy:\n"
        "    MOV R4, R0\n"
        ".Lmemcpy_loop:\n"
        "    CMP R2, #0\n"
        "    JZ .Lmemcpy_done\n"
        "    LOAD8U R3, [R1]\n"
        "    STORE8 R3, [R0]\n"
        "    ADD R0, R0, #1\n"
        "    ADD R1, R1, #1\n"
        "    SUB R2, R2, #1\n"
        "    JMP .Lmemcpy_loop\n"
        ".Lmemcpy_done:\n"
        "    MOV R0, R4\n"
        "    RET\n"
        ".global memset\n"
        "memset:\n"
        "    MOV R3, R0\n"
        ".Lmemset_loop:\n"
        "    CMP R2, #0\n"
        "    JZ .Lmemset_done\n"
        "    STORE8 R1, [R0]\n"
        "    ADD R0, R0, #1\n"
        "    SUB R2, R2, #1\n"
        "    JMP .Lmemset_loop\n"
        ".Lmemset_done:\n"
        "    MOV R0, R3\n"
        "    RET\n"
        ".global strlen\n"
        "strlen:\n"
        "    MOV R1, #0\n"
        ".Lstrlen_loop:\n"
        "    LOAD8U R2, [R0]\n"
        "    CMP R2, #0\n"
        "    JZ .Lstrlen_done\n"
        "    ADD R0, R0, #1\n"
        "    ADD R1, R1, #1\n"
        "    JMP .Lstrlen_loop\n"
        ".Lstrlen_done:\n"
        "    MOV R0, R1\n"
        "    RET\n"
        + libc_assembly();
}

} // namespace kr32::cc
