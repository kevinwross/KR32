#include "kr32/cc/cc.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

    std::string result;
    std::istringstream lines(without_comments);
    std::string line;
    while (std::getline(lines, line)) {
        std::string trimmed = line;
        const std::size_t first = trimmed.find_first_not_of(" \t");
        if (first != std::string::npos && trimmed[first] == '#') {
            const std::string directive = trimmed.substr(first);
            if (directive.rfind("#include", 0) == 0) {
                if (directive.find("kr32.h") != std::string::npos ||
                    directive.find("stdio.h") != std::string::npos) {
                    result += "int putchar(int c);\nvoid exit(int code);\n";
                }
            }
            result.push_back('\n');
            continue;
        }
        result += line;
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

struct Type {
    enum class Kind { Void, Char, Int, Pointer, Array };

    Kind kind = Kind::Int;
    TypePtr base;
    int count = 0;

    [[nodiscard]] int size() const {
        switch (kind) {
        case Kind::Void: return 1;
        case Kind::Char: return 1;
        case Kind::Int: return 4;
        case Kind::Pointer: return 4;
        case Kind::Array: return base ? base->size() * count : 0;
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
        return text == "int" || text == "char" || text == "void" || text == "const" ||
               text == "unsigned" || text == "signed" || text == "static" ||
               text == "extern" || text == "volatile" || text == "register";
    }

    TypePtr parse_type() {
        bool is_const = false;
        (void)is_const;
        while (peek().text == "const" || peek().text == "unsigned" ||
               peek().text == "signed" || peek().text == "static" ||
               peek().text == "extern" || peek().text == "volatile" ||
               peek().text == "register") {
            take();
        }
        TypePtr base;
        if (match("int")) {
            base = type_int();
        } else if (match("char")) {
            base = type_char();
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
        TypePtr type = parse_type();
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
        if (match("{")) {
            parse_block_contents();
            return;
        }
        if (match(";")) {
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

    void parse_assignment_statement() {
        // Only identifier, *expr, and identifier[index] lvalues are supported.
        if (peek().kind == TokenKind::Identifier && peek(1).text == "=") {
            const std::string name = take().text;
            expect("=");
            gen_lvalue_address(name);
            emit("    PUSH R0");
            gen_expression();
            emit("    POP R1");
            const Variable* variable = lookup(name);
            gen_store(variable != nullptr ? variable->type : type_int());
            return;
        }
        if (peek().kind == TokenKind::Identifier && peek(1).text == "[") {
            const std::string name = take().text;
            const Variable* variable = lookup(name);
            if (variable == nullptr) {
                error("unknown variable: " + name);
            }
            gen_lvalue_address(name);
            TypePtr type = variable->type;
            while (match("[")) {
                emit("    PUSH R0");
                gen_expression();
                expect("]");
                emit("    POP R1");
                if (!type->is_pointer()) {
                    error("subscript of a non-array");
                }
                const int size = type->element() ? type->element()->size() : 1;
                if (size > 1) {
                    emit("    MUL R0, R0, #" + std::to_string(size));
                }
                emit("    ADD R0, R1, R0");
                type = type->element();
            }
            expect("=");
            emit("    PUSH R0");
            gen_expression();
            emit("    POP R1");
            gen_store(type);
            return;
        }
        if (peek().text == "*") {
            take();
            gen_expression();
            emit("    PUSH R0");
            if (!match("=")) {
                error("expected '='");
            }
            gen_expression();
            emit("    POP R1");
            gen_store(type_int());
            return;
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
        if (type->kind == Type::Kind::Char) {
            emit("    LOAD8S R0, [R0]");
        } else if (type->kind == Type::Kind::Array) {
            // An array expression is its address; nothing to load.
        } else {
            emit("    LOAD R0, [R0]");
        }
    }

    void gen_store(const TypePtr& type) {
        if (type->kind == Type::Kind::Char) {
            emit("    STORE8 R0, [R1]");
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

    TypePtr apply_postfix(TypePtr type) {
        while (match("[")) {
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
            gen_load(type);
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
            return apply_postfix(type_pointer(type_char()));
        }
        if (match("(")) {
            TypePtr type = gen_expression();
            expect(")");
            return apply_postfix(type);
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
                error("unknown variable: " + name);
            }
            const TypePtr type = variable->type;
            gen_lvalue_address(name);
            gen_load(type);
            return apply_postfix(type);
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
        "    HALT\n";
}

} // namespace kr32::cc
