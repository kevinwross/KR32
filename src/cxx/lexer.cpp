#include "cxx/lexer.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <unordered_set>

namespace kr32::cxx {

namespace {

const std::unordered_set<std::string>& keywords() {
    static const std::unordered_set<std::string> value = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
        "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "requires", "return",
        "short", "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw", "true",
        "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
        "override", "final"};
    return value;
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_identifier_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

const std::vector<std::string>& punctuators() {
    static const std::vector<std::string> value = {
        ">>=", "<<=", "->*", "...", "<=>", "::", "->", "++", "--", "<<", ">>",
        "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=",
        "|=", "^=", ".*", "##", "#", "{", "}", "[", "]", "(", ")", ";", ":", ",",
        ".", "?", "~", "!", "+", "-", "*", "/", "%", "^", "&", "|", "=", "<",
        ">"};
    return value;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string decode_escapes(const std::string& raw, bool& ok) {
    std::string result;
    ok = true;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const char c = raw[index];
        if (c != '\\') {
            result.push_back(c);
            continue;
        }
        if (index + 1 >= raw.size()) {
            ok = false;
            break;
        }
        const char escaped = raw[++index];
        switch (escaped) {
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'a': result.push_back('\a'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'v': result.push_back('\v'); break;
        case '0': result.push_back('\0'); break;
        case '\\': result.push_back('\\'); break;
        case '\'': result.push_back('\''); break;
        case '"': result.push_back('"'); break;
        case '?': result.push_back('?'); break;
        case 'x': {
            int value = 0;
            int digits = 0;
            while (index + 1 < raw.size() && hex_value(raw[index + 1]) >= 0) {
                value = value * 16 + hex_value(raw[++index]);
                ++digits;
            }
            if (digits == 0) {
                ok = false;
            } else {
                result.push_back(static_cast<char>(value & 0xFF));
            }
            break;
        }
        default:
            if (escaped >= '0' && escaped <= '7') {
                int value = escaped - '0';
                int digits = 1;
                while (digits < 3 && index + 1 < raw.size() && raw[index + 1] >= '0' &&
                       raw[index + 1] <= '7') {
                    value = value * 8 + (raw[++index] - '0');
                    ++digits;
                }
                result.push_back(static_cast<char>(value & 0xFF));
            } else {
                result.push_back(escaped);
            }
            break;
        }
    }
    return result;
}

} // namespace

bool is_keyword(std::string_view text) {
    return keywords().find(std::string(text)) != keywords().end();
}

std::string_view token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::End: return "end";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Keyword: return "keyword";
    case TokenKind::IntegerLiteral: return "integer-literal";
    case TokenKind::FloatLiteral: return "float-literal";
    case TokenKind::StringLiteral: return "string-literal";
    case TokenKind::CharacterLiteral: return "character-literal";
    case TokenKind::Punctuator: return "punctuator";
    }
    return "unknown";
}

namespace {

class Lexer {
public:
    Lexer(std::string_view source, std::string_view source_name,
          std::vector<Diagnostic>& diagnostics)
        : source_(source), diagnostics_(diagnostics) {
        (void)source_name;
    }

    std::vector<Token> run() {
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

    bool starts_with(std::string_view text) const {
        return source_.substr(position_, text.size()) == text;
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

    void advance(std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            advance();
        }
    }

    void error(int line, int column, std::string message) {
        diagnostics_.push_back({line, column, std::move(message)});
    }

    void skip_space_and_comments() {
        while (position_ < source_.size()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
                c == '\v') {
                advance();
                continue;
            }
            if (c == '\\' && peek(1) == '\n') {
                advance(2);
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                while (position_ < source_.size() && peek() != '\n') {
                    advance();
                }
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                const int start_line = line_;
                const int start_column = column_;
                advance(2);
                bool closed = false;
                while (position_ < source_.size()) {
                    if (peek() == '*' && peek(1) == '/') {
                        advance(2);
                        closed = true;
                        break;
                    }
                    advance();
                }
                if (!closed) {
                    error(start_line, start_column, "unterminated block comment");
                }
                continue;
            }
            break;
        }
    }

    static std::string strip_separators(const std::string& text) {
        std::string result;
        result.reserve(text.size());
        for (const char c : text) {
            if (c != '\'') {
                result.push_back(c);
            }
        }
        return result;
    }

    Token number() {
        Token token;
        token.line = line_;
        token.column = column_;
        const std::size_t start = position_;
        bool is_float = false;

        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            advance(2);
            while (std::isxdigit(static_cast<unsigned char>(peek())) != 0 ||
                   peek() == '\'') {
                advance();
            }
            if (peek() == '.') {
                is_float = true;
                advance();
                while (std::isxdigit(static_cast<unsigned char>(peek())) != 0 ||
                       peek() == '\'') {
                    advance();
                }
            }
            if (peek() == 'p' || peek() == 'P') {
                is_float = true;
                advance();
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                    advance();
                }
            }
        } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
            advance(2);
            while (peek() == '0' || peek() == '1' || peek() == '\'') {
                advance();
            }
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
                   peek() == '\'') {
                advance();
            }
            if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0) {
                is_float = true;
                advance();
                while (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
                       peek() == '\'') {
                    advance();
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                is_float = true;
                advance();
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                    advance();
                }
            }
        }

        // Integer/float suffixes. `f`/`F` forces the floating interpretation.
        std::string suffix;
        while (std::isalpha(static_cast<unsigned char>(peek())) != 0) {
            const char c = peek();
            if (c == 'f' || c == 'F') {
                is_float = true;
            }
            suffix.push_back(c);
            advance();
        }

        token.raw = std::string(source_.substr(start, position_ - start));
        std::string text = strip_separators(token.raw);
        if (!suffix.empty() && text.size() >= suffix.size()) {
            text.resize(text.size() - suffix.size());
        }
        token.text = text;

        if (is_float) {
            errno = 0;
            char* end = nullptr;
            token.floating = std::strtod(text.c_str(), &end);
            if (end == nullptr || *end != '\0') {
                error(token.line, token.column, "invalid floating literal: " + token.raw);
                token.floating = 0.0;
            }
            token.kind = TokenKind::FloatLiteral;
            return token;
        }

        errno = 0;
        char* end = nullptr;
        int base = 10;
        std::string digits = text;
        if (digits.size() > 2 && digits[0] == '0' &&
            (digits[1] == 'b' || digits[1] == 'B')) {
            std::uint64_t value = 0;
            for (std::size_t index = 2; index < digits.size(); ++index) {
                value = value * 2 + (digits[index] == '1' ? 1U : 0U);
            }
            token.integer = static_cast<std::int64_t>(value);
            token.kind = TokenKind::IntegerLiteral;
            return token;
        }
        if (digits.size() > 1 && digits[0] == '0') {
            base = 8;
            // strtoull with base 0 understands the 0x and 0 prefixes; use 0 for
            // hexadecimal and octal, but binary was handled above.
            base = (digits.size() > 2 && (digits[1] == 'x' || digits[1] == 'X')) ? 0 : 0;
        } else {
            base = 0;
        }
        const unsigned long long value = std::strtoull(digits.c_str(), &end, base);
        if (end == nullptr || *end != '\0') {
            error(token.line, token.column, "invalid integer literal: " + token.raw);
            token.integer = 0;
        } else {
            token.integer = static_cast<std::int64_t>(value);
        }
        token.kind = TokenKind::IntegerLiteral;
        return token;
    }

    Token quoted(char quote, const std::string& prefix) {
        Token token;
        token.line = line_;
        token.column = column_;
        const std::size_t start = position_;
        advance(); // opening quote
        std::string raw_value;
        bool closed = false;
        while (position_ < source_.size()) {
            const char c = peek();
            if (c == '\\') {
                raw_value.push_back(c);
                advance();
                if (position_ < source_.size()) {
                    raw_value.push_back(peek());
                    advance();
                }
                continue;
            }
            if (c == quote) {
                advance();
                closed = true;
                break;
            }
            if (c == '\n' && quote != '\'') {
                break;
            }
            raw_value.push_back(c);
            advance();
        }
        if (!closed) {
            error(token.line, token.column, "unterminated literal");
        }
        token.raw = std::string(source_.substr(start, position_ - start));
        bool ok = true;
        const std::string decoded = decode_escapes(raw_value, ok);
        if (!ok) {
            error(token.line, token.column, "invalid escape sequence");
        }
        if (quote == '"') {
            token.kind = TokenKind::StringLiteral;
            token.text = decoded;
        } else {
            token.kind = TokenKind::CharacterLiteral;
            token.integer = decoded.empty() ? 0 : static_cast<std::int64_t>(
                                                       static_cast<unsigned char>(decoded[0]));
            if (decoded.size() > 1) {
                error(token.line, token.column, "multi-character literal");
            }
            token.text = decoded;
        }
        (void)prefix;
        return token;
    }

    Token raw_string() {
        Token token;
        token.line = line_;
        token.column = column_;
        const std::size_t start = position_;
        advance(); // R
        advance(); // "
        std::string delimiter;
        while (position_ < source_.size() && peek() != '(' && delimiter.size() < 16) {
            delimiter.push_back(peek());
            advance();
        }
        if (peek() != '(') {
            error(token.line, token.column, "invalid raw string literal");
            token.kind = TokenKind::StringLiteral;
            token.text.clear();
            token.raw = std::string(source_.substr(start, position_ - start));
            return token;
        }
        advance(); // (
        const std::string terminator = ")" + delimiter + "\"";
        const std::size_t content_start = position_;
        const std::size_t end = source_.find(terminator, position_);
        if (end == std::string_view::npos) {
            error(token.line, token.column, "unterminated raw string literal");
            token.kind = TokenKind::StringLiteral;
            token.text = std::string(source_.substr(content_start));
            position_ = source_.size();
            token.raw = std::string(source_.substr(start));
            return token;
        }
        token.text = std::string(source_.substr(content_start, end - content_start));
        while (position_ < end + terminator.size()) {
            advance();
        }
        token.raw = std::string(source_.substr(start, position_ - start));
        token.kind = TokenKind::StringLiteral;
        return token;
    }

    Token next() {
        skip_space_and_comments();
        Token token;
        token.line = line_;
        token.column = column_;
        if (position_ >= source_.size()) {
            token.kind = TokenKind::End;
            return token;
        }
        const char c = peek();

        // Raw and prefixed string/character literals.
        if (c == 'R' && peek(1) == '"') {
            return raw_string();
        }
        if ((c == 'u' || c == 'U' || c == 'L') && peek(1) == '\'') {
            advance();
            return quoted('\'', std::string(1, c));
        }
        if ((c == 'u' || c == 'U' || c == 'L') && peek(1) == '"') {
            advance();
            return quoted('"', std::string(1, c));
        }
        if (c == 'u' && peek(1) == '8' && peek(2) == '"') {
            advance(2);
            return quoted('"', "u8");
        }
        if (c == 'u' && peek(1) == '8' && peek(2) == '\'') {
            advance(2);
            return quoted('\'', "u8");
        }

        if (is_identifier_start(c)) {
            const std::size_t start = position_;
            while (is_identifier_continue(peek())) {
                advance();
            }
            const std::string text(source_.substr(start, position_ - start));
            token.kind = is_keyword(text) ? TokenKind::Keyword : TokenKind::Identifier;
            token.text = text;
            token.raw = text;
            return token;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0)) {
            return number();
        }
        if (c == '"') {
            return quoted('"', "");
        }
        if (c == '\'') {
            return quoted('\'', "");
        }
        for (const std::string& punctuator : punctuators()) {
            if (starts_with(punctuator)) {
                token.kind = TokenKind::Punctuator;
                token.text = punctuator;
                token.raw = punctuator;
                advance(punctuator.size());
                return token;
            }
        }
        // Unknown byte: consume it so the parser can report and recover.
        token.kind = TokenKind::Punctuator;
        token.text = std::string(1, c);
        token.raw = token.text;
        error(token.line, token.column, "unexpected character: " +
                                            std::string(1, c));
        advance();
        return token;
    }

    std::string_view source_;
    std::vector<Diagnostic>& diagnostics_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace

std::vector<Token> lex(std::string_view source, std::string_view source_name,
                       std::vector<Diagnostic>& diagnostics) {
    Lexer lexer(source, source_name, diagnostics);
    return lexer.run();
}

} // namespace kr32::cxx
