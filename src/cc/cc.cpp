#include "kr32/cc/cc.h"

#include <algorithm>
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
                        "int putchar(int c);\n"
                        "void exit(int code);\n"
                        "void *malloc(int size);\n"
                        "void free(void *pointer);\n"
                        "void *memcpy(void *destination, const void *source, int count);\n"
                        "void *memset(void *destination, int value, int count);\n"
                        "int strlen(const char *text);\n";
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

enum class TokenKind { End, Identifier, Number, String, Character, Punct };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    std::int64_t value = 0;
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
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            token.kind = TokenKind::Number;
            std::string number;
            while (std::isalnum(static_cast<unsigned char>(peek())) != 0 ||
                   peek() == '\'') {
                number.push_back(peek());
                advance();
            }
            token.text = number;
            char* end = nullptr;
            token.value = std::strtoll(number.c_str(), &end, 0);
            if (end == nullptr || *end != '\0') {
                throw CompileError(token.line, token.column, "invalid number: " + number);
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
    enum class Kind { Void, Char, Int, Bool, Pointer, Array, Struct, Union };

    Kind kind = Kind::Int;
    TypePtr base;
    int count = 0;
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
        return base;
    }
};

TypePtr type_int() { return std::make_shared<Type>(Type{Type::Kind::Int, nullptr, 0}); }
TypePtr type_char() { return std::make_shared<Type>(Type{Type::Kind::Char, nullptr, 0}); }
TypePtr type_bool() { return std::make_shared<Type>(Type{Type::Kind::Bool, nullptr, 0}); }
TypePtr type_void() { return std::make_shared<Type>(Type{Type::Kind::Void, nullptr, 0}); }
TypePtr type_pointer(TypePtr base) {
    return std::make_shared<Type>(Type{Type::Kind::Pointer, std::move(base), 0});
}
TypePtr type_array(TypePtr base, int count) {
    return std::make_shared<Type>(Type{Type::Kind::Array, std::move(base), count});
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
    bool defined = false;
};

class Compiler {
public:
    Compiler(std::string_view source, std::string_view name)
        : source_name_(name), tokens_(Lexer(preprocess(source)).tokenize()) {}

    Result run() {
        Result result;
        try {
            emit(".text");
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

    bool is_type_start() const {
        const std::string& text = peek().text;
        return text == "int" || text == "char" || text == "void" || text == "_Bool" ||
               text == "struct" || text == "union" || text == "enum" ||
               text == "const" || text == "unsigned" || text == "signed" ||
               text == "static" || text == "extern" || text == "volatile" ||
               text == "register" || typedefs_.find(text) != typedefs_.end();
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
        while (peek().text == "const" || peek().text == "unsigned" ||
               peek().text == "signed" || peek().text == "static" ||
               peek().text == "extern" || peek().text == "volatile" ||
               peek().text == "register" || peek().text == "typedef") {
            take();
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
        } else if (match("void")) {
            base = type_void();
        } else {
            error("expected a type");
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

    void parse_top_level() {
        if (match(";")) {
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
        TypePtr actual = type;
        if (match("[")) {
            std::int64_t count = 0;
            if (peek().kind == TokenKind::Number) {
                count = take().value;
            }
            expect("]");
            actual = type_array(type, static_cast<int>(count));
        }
        if (match("=")) {
            if (actual->kind == Type::Kind::Array) {
                error("array initializers are not supported yet");
            }
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
        } else {
            expect(";");
            bss_ += ".global " + name + "\n" + name + ":\n";
            bss_ += "    .space " + std::to_string(actual->size()) + "\n";
        }
        globals_.emplace(name, Variable{name, actual, 0, name, true});
    }

    int allocate_local(const TypePtr& type) {
        const int size = std::max(1, type->size());
        frame_size_ = (frame_size_ + 3) & ~3;
        const int offset = frame_size_;
        frame_size_ += (size + 3) & ~3;
        return offset;
    }

    void parse_function(const TypePtr& result_type, const std::string& name) {
        std::vector<TypePtr> parameters;
        std::vector<std::string> parameter_names;
        if (!match(")")) {
            if (peek().text == "void" && peek(1).text == ")") {
                take();
                take();
            } else {
                while (true) {
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
                }
                expect(")");
            }
        }

        auto found = functions_.find(name);
        if (found == functions_.end()) {
            functions_.emplace(name, FunctionSignature{name, result_type, parameters, false});
        } else if (found->second.defined) {
            error("function already defined: " + name);
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
        frame_size_ = 0;
        locals_.clear();
        return_label_ = ".Lreturn_" + name;
        emit(".global " + name);
        emit(name + ":");
        emit("    PUSH R7");
        emit("    ADJSP #-__FRAME_" + name + "__");
        emit("    LEA R7, [SP]");

        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const int offset = allocate_local(parameters[index]);
            locals_.emplace(parameter_names[index],
                            Variable{parameter_names[index], parameters[index], offset, "", false});
            emit("    STORE R" + std::to_string(index) + ", [R7 + " +
                 std::to_string(offset) + "]");
        }

        parse_block_contents();
        emit("    MOV R0, #0");
        emit(return_label_ + ":");
        emit("    ADJSP #__FRAME_" + name + "__");
        emit("    POP R7");
        emit("    RET");
        replace_all("__FRAME_" + name + "__", std::to_string(frame_size_));
    }

    void parse_block_contents() {
        while (!at_end() && peek().text != "}") {
            parse_statement();
        }
        expect("}");
    }

    void parse_statement() {
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
            while (!match("}")) {
                if (match("case")) {
                    const std::int64_t value = parse_constant_value();
                    expect(":");
                    const std::string next = new_label("case");
                    emit("    LOAD R0, [R7 + " + std::to_string(temp) + "]");
                    emit("    MOV R1, #" + std::to_string(value));
                    emit("    CMP R0, R1");
                    emit("    JNZ " + next);
                    break_labels_.push_back(end);
                    while (!at_end() && peek().text != "}" && peek().text != "case" &&
                           peek().text != "default") {
                        parse_statement();
                    }
                    break_labels_.pop_back();
                    emit("    JMP " + end);
                    emit(next + ":");
                    continue;
                }
                if (match("default")) {
                    expect(":");
                    break_labels_.push_back(end);
                    while (!at_end() && peek().text != "}" && peek().text != "case" &&
                           peek().text != "default") {
                        parse_statement();
                    }
                    break_labels_.pop_back();
                    emit("    JMP " + end);
                    continue;
                }
                error("expected case, default, or '}'");
            }
            emit(end + ":");
            return;
        }
        if (match("if")) {
            expect("(");
            gen_expression();
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
        if (match("while")) {
            const std::string start = new_label("while");
            const std::string end = new_label("endwhile");
            emit(start + ":");
            expect("(");
            gen_expression();
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
                gen_expression();
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
                gen_expression();
                expect(";");
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
        TypePtr actual = type;
        if (match("[")) {
            std::int64_t count = 0;
            if (peek().kind == TokenKind::Number) {
                count = take().value;
            }
            expect("]");
            actual = type_array(type, static_cast<int>(count));
        }
        const int offset = allocate_local(actual);
        locals_[name] = Variable{name, actual, offset, "", false};
        if (match("=")) {
            gen_lvalue_address(name);
            emit("    PUSH R0");
            gen_expression();
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
            TypePtr pointer = gen_expression();
            if (!pointer->is_pointer()) {
                error("dereference of a non-pointer");
            }
            type = pointer->element();
            bool address = true;
            type = apply_postfix(type, address);
            return address;
        }
        return false;
    }

    void parse_assignment_statement() {
        const bool looks_lvalue =
            (peek().kind == TokenKind::Identifier &&
             (peek(1).text == "=" || peek(1).text == "[" || peek(1).text == "." ||
              peek(1).text == "->")) ||
            peek().text == "*";
        if (looks_lvalue) {
            const std::size_t saved_position = position_;
            const std::size_t saved_text = text_.size();
            const int saved_label = label_counter_;
            TypePtr type;
            if (gen_lvalue(type) && match("=")) {
                emit("    PUSH R0");
                gen_expression();
                emit("    POP R1");
                gen_store(type);
                return;
            }
            position_ = saved_position;
            text_.resize(saved_text);
            label_counter_ = saved_label;
        }
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
        if (type->kind == Type::Kind::Char || type->kind == Type::Kind::Bool) {
            emit("    LOAD8S R0, [R0]");
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
        } else if (type->kind == Type::Kind::Struct || type->kind == Type::Kind::Union) {
            error("aggregate assignment is not supported yet");
        } else {
            emit("    STORE R0, [R1]");
        }
    }

    TypePtr gen_expression() {
        return parse_logical_or();
    }

    void gen_binary(const std::string& op, const TypePtr& left, const TypePtr& right) {
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
            emit("    DIV R0, R1, R0");
            return;
        }
        if (op == "%") {
            emit("    MOD R0, R1, R0");
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
            emit(std::string("    ") + (op == "<<" ? "SHL" : "SAR") + " R0, R1, R0");
            return;
        }
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
            op == ">=") {
            const std::string true_label = new_label("true");
            const std::string end_label = new_label("cmpend");
            emit("    CMP R1, R0");
            if (op == "==") emit("    JZ " + true_label);
            if (op == "!=") emit("    JNZ " + true_label);
            if (op == "<") emit("    JLT " + true_label);
            if (op == "<=") emit("    JLE " + true_label);
            if (op == ">") emit("    JGT " + true_label);
            if (op == ">=") emit("    JGE " + true_label);
            emit("    MOV R0, #0");
            emit("    JMP " + end_label);
            emit(true_label + ":");
            emit("    MOV R0, #1");
            emit(end_label + ":");
        }
    }

    void gen_binary_chain(TypePtr (Compiler::*next)(), const std::vector<std::string>& ops) {
        TypePtr left = (this->*next)();
        while (true) {
            bool matched = false;
            for (const std::string& op : ops) {
                if (peek().text == op) {
                    take();
                    emit("    PUSH R0");
                    TypePtr right = (this->*next)();
                    emit("    POP R1");
                    gen_binary(op, left, right);
                    left = (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                            op == ">" || op == ">=")
                               ? type_int()
                               : left;
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
        while (match("&&")) {
            const std::string false_label = new_label("andfalse");
            const std::string end_label = new_label("andend");
            emit("    CMP R0, #0");
            emit("    JZ " + false_label);
            parse_bit_or_real();
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
        while (match("||")) {
            const std::string true_label = new_label("ortrue");
            const std::string end_label = new_label("orend");
            emit("    CMP R0, #0");
            emit("    JNZ " + true_label);
            parse_logical_and();
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
            break;
        }
        return type;
    }

    TypePtr parse_unary() {
        if (match("-")) {
            TypePtr type = parse_unary();
            emit("    NEG R0, R0");
            return type;
        }
        if (match("!")) {
            parse_unary();
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
        if (peek().kind == TokenKind::Number) {
            const std::int64_t value = take().value;
            emit("    MOV R0, #" + std::to_string(value));
            return type_int();
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
                parse_unary();
                if (target->kind == Type::Kind::Char) {
                    emit("    AND R0, R0, #255");
                    emit("    SHL R0, R0, #24");
                    emit("    SAR R0, R0, #24");
                } else if (target->kind == Type::Kind::Bool) {
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
        if (peek().kind == TokenKind::Identifier) {
            const std::string name = take().text;
            if (match("(")) {
                std::vector<TypePtr> arguments;
                if (!match(")")) {
                    while (true) {
                        arguments.push_back(gen_expression());
                        emit("    PUSH R0");
                        if (!match(",")) {
                            break;
                        }
                    }
                    expect(")");
                }
                if (arguments.size() > 4) {
                    error("more than four arguments are not supported yet");
                }
                for (std::size_t index = arguments.size(); index > 0; --index) {
                    emit("    POP R" + std::to_string(index - 1));
                }
                emit("    CALL " + name);
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
                    emit("    PUSH R0");
                    TypePtr right = (this->*next)();
                    emit("    POP R1");
                    gen_binary(op, left, right);
                    left = (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                            op == ">" || op == ">=")
                               ? type_int()
                               : left;
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
    std::string return_label_;
    std::string* output_ = nullptr;
    int frame_size_ = 0;
    int label_counter_ = 0;
    int string_counter_ = 0;
};

} // namespace

Result compile(std::string_view source, std::string_view source_name) {
    Compiler compiler(source, source_name);
    return compiler.run();
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
        ".global malloc\n"
        "malloc:\n"
        "    LEA R1, [__heap_ptr]\n"
        "    LOAD R2, [R1]\n"
        "    ADD R3, R0, #3\n"
        "    AND R3, R3, #-4\n"
        "    ADD R3, R2, R3\n"
        "    STORE R3, [R1]\n"
        "    MOV R0, R2\n"
        "    RET\n"
        ".global free\n"
        "free:\n"
        "    RET\n"
        ".data\n"
        ".global __heap_ptr\n"
        "__heap_ptr:\n"
        "    .word __heap_start\n";
}

} // namespace kr32::cc
