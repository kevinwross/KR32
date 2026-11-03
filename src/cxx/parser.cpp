#include "cxx/parser.h"

#include "cxx/lexer.h"
#include "cxx/preprocessor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kr32::cxx {

namespace {

using namespace ast;

enum class Context {
    Global,
    Namespace,
    Class,
    Block,
    ForInit,
};

struct Specifiers {
    TypePtr type;
    bool is_const = false;
    bool is_volatile = false;
    bool is_static = false;
    bool is_virtual = false;
    bool is_inline = false;
    bool is_explicit = false;
    bool is_constexpr = false;
    bool is_friend = false;
    bool is_mutable = false;
    bool is_thread_local = false;
    bool is_typedef = false;
    bool is_typename = false;
    bool is_auto = false;
    bool is_extern = false;
};

struct Declarator {
    Name name;
    TypePtr type;
    std::vector<Param> parameters;
    std::vector<TypePtr> template_arguments;
    std::vector<TypePtr> owner_template_arguments;
    bool variadic = false;
    bool is_function = false;
    bool is_const = false;
    bool is_volatile = false;
    bool is_override = false;
    bool is_final = false;
    bool is_pure = false;
    bool is_defaulted = false;
    bool is_deleted = false;
    bool is_operator = false;
    bool is_constructor = false;
    bool is_destructor = false;
    TypePtr trailing_return;
};

bool is_decl_specifier_text(std::string_view text) {
    static const std::unordered_set<std::string> specifiers = {
        "void", "bool", "char", "int", "float", "double", "long", "short",
        "signed", "unsigned", "auto", "wchar_t", "char16_t", "char32_t",
        "const", "volatile", "static", "virtual", "inline", "explicit",
        "constexpr", "consteval", "constinit", "friend", "mutable",
        "thread_local", "register", "typedef", "typename", "class", "struct",
        "union", "enum", "decltype", "alignas"};
    return specifiers.find(std::string(text)) != specifiers.end();
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string_view source_name,
           std::vector<Diagnostic>& diagnostics)
        : tokens_(std::move(tokens)), source_name_(source_name),
          diagnostics_(diagnostics) {
        known_types_.insert("size_t");
        known_types_.insert("va_list");
        known_types_.insert("int8_t");
        known_types_.insert("int16_t");
        known_types_.insert("int32_t");
        known_types_.insert("uint8_t");
        known_types_.insert("uint16_t");
        known_types_.insert("uint32_t");
        known_types_.insert("ptrdiff_t");
    }

    std::shared_ptr<TranslationUnit> run() {
        auto unit = std::make_shared<TranslationUnit>();
        unit->source_name = source_name_;
        while (!at_end()) {
            if (consume(";")) {
                continue;
            }
            const std::size_t before = position_;
            std::vector<DeclPtr> declarations;
            parse_declaration(Context::Global, declarations);
            for (DeclPtr& declaration : declarations) {
                unit->declarations.push_back(std::move(declaration));
            }
            if (position_ == before) {
                error(peek(), "unable to parse declaration");
                advance();
                synchronize_declaration();
            }
        }
        return unit;
    }

private:
    const Token& peek(std::size_t ahead = 0) const {
        const std::size_t index = position_ + ahead;
        static const Token end;
        return index < tokens_.size() ? tokens_[index] : end;
    }

    bool at_end() const { return peek().kind == TokenKind::End; }

    bool at(std::string_view text) const { return peek().text == text; }

    bool at_kind(TokenKind kind) const { return peek().kind == kind; }

    bool at_identifier() const { return peek().kind == TokenKind::Identifier; }

    bool at_any(const std::vector<std::string_view>& texts) const {
        for (const std::string_view text : texts) {
            if (at(text)) {
                return true;
            }
        }
        return false;
    }

    const Token& advance() {
        if (position_ + 1 < tokens_.size()) {
            return tokens_[position_++];
        }
        position_ = tokens_.size() - 1;
        return tokens_[position_];
    }

    bool consume(std::string_view text) {
        if (at(text)) {
            advance();
            return true;
        }
        return false;
    }

    bool expect(std::string_view text, std::string_view context) {
        if (consume(text)) {
            return true;
        }
        error(peek(), "expected '" + std::string(text) + "' " + std::string(context));
        return false;
    }

    void error(const Token& token, std::string message) {
        diagnostics_.push_back({token.line, token.column, std::move(message)});
    }

    void synchronize_declaration() {
        int braces = 0;
        while (!at_end()) {
            if (at(";") && braces == 0) {
                advance();
                return;
            }
            if (at("}") && braces == 0) {
                return;
            }
            if (at("{")) {
                ++braces;
            } else if (at("}")) {
                --braces;
                if (braces < 0) {
                    return;
                }
            }
            advance();
        }
    }

    void synchronize_statement() {
        int depth = 0;
        while (!at_end()) {
            if (at(";") && depth == 0) {
                advance();
                return;
            }
            if (at("}") && depth == 0) {
                return;
            }
            if (at("(") || at("[") || at("{")) {
                ++depth;
            } else if (at(")") || at("]") || at("}")) {
                if (depth == 0) {
                    return;
                }
                --depth;
            }
            advance();
        }
    }

    bool is_builtin_specifier() const {
        return at_any({"void", "bool", "char", "int", "float", "double", "long",
                       "short", "signed", "unsigned", "auto", "wchar_t",
                       "char16_t", "char32_t"});
    }

    bool is_declaration_specifier() const {
        return is_decl_specifier_text(peek().text);
    }

    bool is_known_type(std::string_view name) const {
        return known_types_.find(std::string(name)) != known_types_.end();
    }

    void add_known_type(const Name& name) {
        if (!name.parts.empty()) {
            known_types_.insert(name.parts.back());
        }
    }

    static bool is_assignment_operator(std::string_view text) {
        static const std::unordered_set<std::string> operators = {
            "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|="};
        return operators.find(std::string(text)) != operators.end();
    }

    static int binary_precedence(std::string_view text) {
        static const std::unordered_map<std::string, int> table = {
            {"||", 1},  {"&&", 2},  {"|", 3},   {"^", 4},   {"&", 5},
            {"==", 6},  {"!=", 6},  {"<", 7},   {"<=", 7},  {">", 7},
            {">=", 7},  {"<<", 8},  {">>", 8},  {"+", 9},   {"-", 9},
            {"*", 10},  {"/", 10},  {"%", 10},  {".*", 11}, {"->*", 11}};
        const auto found = table.find(std::string(text));
        return found == table.end() ? -1 : found->second;
    }

    ExprPtr make_expr(ExprKind kind, const Token& token) {
        auto expression = std::make_unique<Expr>();
        expression->kind = kind;
        expression->location = {token.line, token.column};
        return expression;
    }

    Name parse_qualified_name(bool allow_template_arguments, TypePtr* out_type = nullptr) {
        Name name;
        if (consume("::")) {
            name.global = true;
        }
        if (!at_identifier() && peek().kind != TokenKind::Keyword) {
            error(peek(), "expected name");
            return name;
        }
        while (true) {
            name.parts.push_back(peek().text);
            advance();
            if (out_type != nullptr && at("<") && allow_template_arguments) {
                (*out_type)->template_arguments = parse_template_type_arguments();
            }
            if (!consume("::")) {
                break;
            }
        }
        return name;
    }

    // Consume a template closing '>' even when the lexer produced '>>'.
    bool consume_template_close() {
        if (consume(">")) {
            return true;
        }
        if (at(">>")) {
            Token second = tokens_[position_];
            second.text = ">";
            second.raw = ">";
            tokens_[position_].text = ">";
            tokens_[position_].raw = ">";
            tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(position_ + 1),
                           second);
            advance();
            return true;
        }
        error(peek(), "expected '>' to close template argument list");
        return false;
    }

    std::vector<TypePtr> parse_template_type_arguments() {
        std::vector<TypePtr> arguments;
        if (!expect("<", "in template argument list")) {
            return arguments;
        }
        while (!at_end() && !at(">") && !at(">>")) {
            if (peek().kind == TokenKind::IntegerLiteral ||
                peek().kind == TokenKind::FloatLiteral ||
                peek().kind == TokenKind::CharacterLiteral ||
                peek().kind == TokenKind::StringLiteral || at("true") ||
                at("false") || at("nullptr")) {
                const BuiltinKind kind = peek().kind == TokenKind::FloatLiteral
                                             ? BuiltinKind::Double
                                             : BuiltinKind::Int;
                TypePtr argument = Type::make_builtin(kind);
                if (peek().kind == TokenKind::IntegerLiteral ||
                    peek().kind == TokenKind::CharacterLiteral ||
                    at("true") || at("false")) {
                    argument->has_template_integer_value = true;
                    argument->template_integer_value =
                        peek().kind == TokenKind::IntegerLiteral ||
                                peek().kind == TokenKind::CharacterLiteral
                            ? peek().integer
                            : (at("true") ? 1 : 0);
                }
                arguments.push_back(std::move(argument));
                advance();
            } else {
                arguments.push_back(parse_type_id());
            }
            if (!consume(",")) {
                break;
            }
        }
        consume_template_close();
        return arguments;
    }

    void skip_attributes() {
        while (at("[") && peek(1).text == "[") {
            advance();
            advance();
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (at("[")) {
                    ++depth;
                } else if (at("]")) {
                    --depth;
                }
                advance();
            }
            if (at("]")) {
                advance();
            }
        }
    }

    void skip_balanced(std::string_view open, std::string_view close) {
        if (!consume(open)) {
            return;
        }
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (at(open)) {
                ++depth;
            } else if (at(close)) {
                --depth;
            }
            advance();
        }
    }

    Specifiers parse_specifiers() {
        Specifiers specifiers;
        bool have_unsigned = false;
        bool have_long = false;
        bool have_long_long = false;
        bool have_short = false;
        while (!at_end()) {
            skip_attributes();
            if (at("const")) {
                specifiers.is_const = true;
                advance();
                continue;
            }
            if (at("volatile")) {
                specifiers.is_volatile = true;
                advance();
                continue;
            }
            if (at("static")) {
                specifiers.is_static = true;
                advance();
                continue;
            }
            if (at("virtual")) {
                specifiers.is_virtual = true;
                advance();
                continue;
            }
            if (at("inline")) {
                specifiers.is_inline = true;
                advance();
                continue;
            }
            if (at("explicit")) {
                specifiers.is_explicit = true;
                advance();
                continue;
            }
            if (at("constexpr") || at("consteval") || at("constinit")) {
                specifiers.is_constexpr = true;
                advance();
                continue;
            }
            if (at("friend")) {
                specifiers.is_friend = true;
                advance();
                continue;
            }
            if (at("mutable")) {
                specifiers.is_mutable = true;
                advance();
                continue;
            }
            if (at("thread_local")) {
                specifiers.is_thread_local = true;
                advance();
                continue;
            }
            if (at("register")) {
                advance();
                continue;
            }
            if (at("extern")) {
                specifiers.is_extern = true;
                advance();
                if (peek().kind == TokenKind::StringLiteral) {
                    advance();
                }
                continue;
            }
            if (at("typedef")) {
                specifiers.is_typedef = true;
                advance();
                continue;
            }
            if (at("typename")) {
                specifiers.is_typename = true;
                advance();
                continue;
            }
            if (at("alignas")) {
                advance();
                skip_balanced("(", ")");
                continue;
            }
            if (at("decltype")) {
                advance();
                skip_balanced("(", ")");
                specifiers.type = Type::make_builtin(BuiltinKind::Auto);
                continue;
            }
            if (at("unsigned")) {
                have_unsigned = true;
                advance();
                continue;
            }
            if (at("signed")) {
                advance();
                continue;
            }
            if (at("short")) {
                have_short = true;
                advance();
                continue;
            }
            if (at("long")) {
                if (have_long) {
                    have_long_long = true;
                }
                have_long = true;
                advance();
                continue;
            }
            if (at("void") || at("bool") || at("char") || at("int") || at("float") ||
                at("double") || at("auto") || at("wchar_t") || at("char16_t") ||
                at("char32_t")) {
                const std::string text = peek().text;
                advance();
                if (text == "void") {
                    specifiers.type = Type::make_builtin(BuiltinKind::Void);
                } else if (text == "bool") {
                    specifiers.type = Type::make_builtin(BuiltinKind::Bool);
                } else if (text == "char") {
                    specifiers.type = Type::make_builtin(BuiltinKind::Char);
                } else if (text == "float") {
                    specifiers.type = Type::make_builtin(BuiltinKind::Float);
                } else if (text == "double") {
                    specifiers.type = Type::make_builtin(
                        have_long ? BuiltinKind::LongDouble : BuiltinKind::Double);
                } else if (text == "auto") {
                    specifiers.is_auto = true;
                    specifiers.type = Type::make_builtin(BuiltinKind::Auto);
                } else if (text == "wchar_t" || text == "char16_t" ||
                           text == "char32_t") {
                    specifiers.type = Type::make_builtin(BuiltinKind::Int);
                } else {
                    specifiers.type = Type::make_builtin(have_unsigned
                                                             ? BuiltinKind::UnsignedInt
                                                             : BuiltinKind::Int);
                }
                continue;
            }
            if (at("class") || at("struct") || at("union") || at("enum")) {
                if (is_class_or_enum_definition()) {
                    std::vector<DeclPtr> definitions;
                    if (at("enum")) {
                        DeclPtr definition = parse_enum_definition();
                        add_known_type(definition->name);
                        definitions.push_back(std::move(definition));
                    } else {
                        DeclPtr definition = parse_class_definition();
                        add_known_type(definition->name);
                        definitions.push_back(std::move(definition));
                    }
                    pending_definitions_.push_back(std::move(definitions.back()));
                    specifiers.type = Type::make_named(pending_definitions_.back()->name);
                } else {
                    advance();
                    if (at_identifier() || at("::")) {
                        TypePtr named = Type::make_named(parse_qualified_name(false));
                        specifiers.type = named;
                    } else {
                        specifiers.type = Type::make_builtin(BuiltinKind::Int);
                    }
                }
                continue;
            }
            if (at("::") ||
                (at_identifier() && !specifiers.type && !have_long &&
                 !have_short && !have_unsigned)) {
                TypePtr named = Type::make_named(Name{});
                named->name = parse_qualified_name(true, &named);
                specifiers.type = named;
                continue;
            }
            break;
        }
        if (!specifiers.type) {
            if (have_unsigned) {
                specifiers.type = Type::make_builtin(BuiltinKind::UnsignedInt);
            } else if (have_long) {
                specifiers.type = Type::make_builtin(BuiltinKind::Int);
            } else if (have_short) {
                specifiers.type = Type::make_builtin(BuiltinKind::Int);
            } else {
                specifiers.type = Type::make_builtin(BuiltinKind::Int);
            }
        } else if (specifiers.type->kind == TypeKind::Builtin &&
                   specifiers.type->builtin == BuiltinKind::Int && have_unsigned) {
            specifiers.type->builtin = BuiltinKind::UnsignedInt;
        }
        if (have_long_long && specifiers.type->kind == TypeKind::Builtin) {
            if (specifiers.type->builtin == BuiltinKind::Int) {
                specifiers.type->builtin = BuiltinKind::LongLong;
            } else if (specifiers.type->builtin == BuiltinKind::UnsignedInt) {
                specifiers.type->builtin = BuiltinKind::UnsignedLongLong;
            }
        }
        if (specifiers.is_const) {
            specifiers.type->is_const = true;
        }
        if (specifiers.is_volatile) {
            specifiers.type->is_volatile = true;
        }
        return specifiers;
    }

    bool is_class_or_enum_definition() const {
        std::size_t index = 1; // skip class/struct/union/enum
        if (peek(index).text == "class" || peek(index).text == "struct") {
            ++index;
        }
        if (peek(index).kind == TokenKind::Identifier) {
            ++index;
        }
        while (peek(index).text == "::") {
            ++index;
            if (peek(index).kind == TokenKind::Identifier) {
                ++index;
            }
        }
        if (peek(index).text == "final") {
            ++index;
        }
        return peek(index).text == "{" || peek(index).text == ":";
    }

    bool looks_like_parameter_type() const {
        if (!at("(")) {
            return false;
        }
        if (peek(1).text == ")" || peek(1).text == "...") {
            return true;
        }
        if (is_decl_specifier_text(peek(1).text) || peek(1).text == "::") {
            return true;
        }
        if (peek(1).kind == TokenKind::Identifier) {
            if (is_known_type(peek(1).text)) {
                return true;
            }
            if (peek(2).kind == TokenKind::Identifier ||
                peek(2).kind == TokenKind::Keyword) {
                return true;
            }
            const std::string_view next = peek(2).text;
            return next == "::" || next == "*" || next == "&" || next == "&&" ||
                   next == "<";
        }
        return false;
    }

    bool parse_declarator(TypePtr base, Declarator& declarator, bool allow_function,
                          bool allow_abstract) {
        TypePtr type = std::move(base);
        bool leading = true;
        while (leading) {
            if (consume("*")) {
                bool pointer_const = false;
                bool pointer_volatile = false;
                while (at("const") || at("volatile")) {
                    pointer_const = pointer_const || at("const");
                    pointer_volatile = pointer_volatile || at("volatile");
                    advance();
                }
                type = Type::make_pointer(type, pointer_const, pointer_volatile);
                continue;
            }
            if (consume("&")) {
                type = Type::make_lvalue_ref(type, type->is_const);
                continue;
            }
            if (consume("&&")) {
                type = Type::make_rvalue_ref(type);
                continue;
            }
            leading = false;
        }

        if (at("~")) {
            advance();
            declarator.is_destructor = true;
        }
        if (at("operator")) {
            declarator.is_operator = true;
            declarator.name.parts.push_back(parse_operator_name());
        } else if (at_identifier() || peek().kind == TokenKind::Keyword ||
                   at("::")) {
            if (at("::")) {
                declarator.name = parse_qualified_name(false);
            } else {
                while (true) {
                    declarator.name.parts.push_back(peek().text);
                    advance();
                    std::vector<TypePtr> component_arguments;
                    if (at("<")) {
                        component_arguments = parse_template_type_arguments();
                    }
                    if (consume("::")) {
                        if (!component_arguments.empty()) {
                            declarator.owner_template_arguments = component_arguments;
                        }
                        if (!at_identifier() && peek().kind != TokenKind::Keyword) {
                            error(peek(), "expected name after '::'");
                            break;
                        }
                        continue;
                    }
                    if (!component_arguments.empty()) {
                        declarator.template_arguments = component_arguments;
                    }
                    break;
                }
            }
        } else if (!allow_abstract) {
            error(peek(), "expected declarator name");
            return false;
        }

        if (at("(") && allow_function && looks_like_parameter_type()) {
            declarator.is_function = true;
            parse_parameter_list(declarator.parameters, declarator.variadic);
            parse_function_suffix(declarator);
        }
        std::vector<std::uint32_t> dimensions;
        while (at("[")) {
            advance();
            std::uint32_t size = 0;
            if (!at("]")) {
                const Token& token = peek();
                if (token.kind == TokenKind::IntegerLiteral) {
                    size = static_cast<std::uint32_t>(token.integer);
                    advance();
                } else {
                    // Parse and discard a non-literal bound for now.
                    parse_assignment_expression();
                }
            }
            expect("]", "in array declarator");
            dimensions.push_back(size);
        }
        // The first declarator dimension is the outermost array:
        // int a[10][5][2] is array<10, array<5, array<2, int>>>.
        for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
            type = Type::make_array(type, *it);
        }
        declarator.type = type;
        return true;
    }

    void parse_function_suffix(Declarator& declarator) {
        while (!at_end()) {
            if (at("const")) {
                declarator.is_const = true;
                advance();
            } else if (at("volatile")) {
                declarator.is_volatile = true;
                advance();
            } else if (at("&") || at("&&")) {
                advance();
            } else if (at("noexcept")) {
                advance();
                if (at("(")) {
                    skip_balanced("(", ")");
                }
            } else if (at("override")) {
                declarator.is_override = true;
                advance();
            } else if (at("final")) {
                declarator.is_final = true;
                advance();
            } else if (at("->")) {
                advance();
                declarator.trailing_return = parse_type_id();
            } else {
                break;
            }
        }
        if (at("=")) {
            advance();
            if (consume("0")) {
                declarator.is_pure = true;
            } else if (at("default")) {
                declarator.is_defaulted = true;
                advance();
            } else if (at("delete")) {
                declarator.is_deleted = true;
                advance();
            }
        }
    }

    std::string parse_operator_name() {
        expect("operator", "in operator declaration");
        std::string name = "operator";
        if (at("(")) {
            advance();
            std::string inner;
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (at("(")) {
                    ++depth;
                } else if (at(")")) {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                if (!inner.empty()) {
                    inner += " ";
                }
                inner += peek().text;
                advance();
            }
            expect(")", "in operator()");
            return "operator()" + inner;
        }
        if (at("[")) {
            advance();
            expect("]", "in operator[]");
            return "operator[]";
        }
        if (at("new") || at("delete")) {
            name += " ";
            name += peek().text;
            advance();
            if (at("[") && peek(1).text == "]") {
                advance();
                advance();
                name += "[]";
            }
            return name;
        }
        if (is_builtin_specifier() || at("::") || at_identifier()) {
            TypePtr type = parse_type_id();
            return "operator " + (type ? type->to_string() : std::string("?"));
        }
        if (at_end() || at(";") || at("{") || at("(") || at(")")) {
            return "operator";
        }
        name += peek().text;
        advance();
        return name;
    }

    void parse_parameter_list(std::vector<Param>& parameters, bool& variadic) {
        if (!expect("(", "in parameter list")) {
            return;
        }
        if (consume(")")) {
            return;
        }
        if (at("void") && peek(1).text == ")") {
            advance();
            advance();
            return;
        }
        while (!at_end()) {
            if (consume("...")) {
                variadic = true;
                break;
            }
            Param parameter;
            parameter.location = {peek().line, peek().column};
            Specifiers specifiers = parse_specifiers();
            Declarator declarator;
            parse_declarator(specifiers.type, declarator, false, true);
            parameter.type = declarator.type;
            parameter.name = declarator.name;
            if (consume("=")) {
                parameter.default_value = parse_assignment_expression();
            }
            parameters.push_back(std::move(parameter));
            if (!consume(",")) {
                break;
            }
        }
        expect(")", "after parameter list");
    }

    TypePtr parse_type_id() {
        Specifiers specifiers = parse_specifiers();
        TypePtr type = specifiers.type;
        while (!at_end()) {
            if (consume("*")) {
                bool pointer_const = false;
                while (at("const") || at("volatile")) {
                    pointer_const = pointer_const || at("const");
                    advance();
                }
                type = Type::make_pointer(type, pointer_const);
            } else if (consume("&")) {
                type = Type::make_lvalue_ref(type, type->is_const);
            } else if (consume("&&")) {
                type = Type::make_rvalue_ref(type);
            } else {
                break;
            }
        }
        return type;
    }

    void parse_declaration(Context context, std::vector<DeclPtr>& output) {
        skip_attributes();
        if (at("template")) {
            parse_template_declaration(context, output);
            return;
        }
        if (at("namespace")) {
            parse_namespace(output);
            return;
        }
        if (at("using")) {
            parse_using(output);
            return;
        }
        if (at("static_assert")) {
            parse_static_assert(output);
            return;
        }
        if (at("extern") && peek(1).kind == TokenKind::StringLiteral) {
            parse_extern_c(context, output);
            return;
        }
        if (context == Context::Class) {
            if (at("public") && peek(1).text == ":") {
                advance();
                advance();
                access_stack_.back() = Access::Public;
                return;
            }
            if (at("private") && peek(1).text == ":") {
                advance();
                advance();
                access_stack_.back() = Access::Private;
                return;
            }
            if (at("protected") && peek(1).text == ":") {
                advance();
                advance();
                access_stack_.back() = Access::Protected;
                return;
            }
            if (at("friend")) {
                advance();
                std::vector<DeclPtr> friends;
                parse_declaration(context, friends);
                for (DeclPtr& declaration : friends) {
                    declaration->is_friend = true;
                    output.push_back(std::move(declaration));
                }
                return;
            }
            if (!class_stack_.empty() && at("~") &&
                peek(1).text == class_stack_.back()) {
                parse_constructor_or_destructor(true, output);
                return;
            }
            bool explicit_constructor = false;
            if (at("explicit") && !class_stack_.empty() &&
                (peek(1).text == class_stack_.back() || peek(1).text == "~")) {
                explicit_constructor = true;
                advance();
            }
            if (!class_stack_.empty() && at_identifier() &&
                peek().text == class_stack_.back() && peek(1).text == "(") {
                parse_constructor_or_destructor(false, output, explicit_constructor);
                return;
            }
        }
        if (at("class") || at("struct") || at("union") || at("enum")) {
            if (is_class_or_enum_definition()) {
                if (at("enum")) {
                    DeclPtr definition = parse_enum_definition();
                    add_known_type(definition->name);
                    output.push_back(std::move(definition));
                } else {
                    DeclPtr definition = parse_class_definition();
                    add_known_type(definition->name);
                    output.push_back(std::move(definition));
                }
                consume(";");
                return;
            }
            // Forward declaration: `class S;` / `struct S;`
            if (!at("enum")) {
                std::size_t index = 1;
                if (peek(index).kind == TokenKind::Identifier &&
                    peek(index + 1).text == ";") {
                    auto declaration = std::make_shared<Decl>();
                    declaration->kind = DeclKind::Class;
                    declaration->name.parts.push_back(peek(index).text);
                    declaration->location = {peek().line, peek().column};
                    add_known_type(declaration->name);
                    advance();
                    advance();
                    advance();
                    output.push_back(std::move(declaration));
                    return;
                }
            }
        }

        Specifiers specifiers = parse_specifiers();
        while (!pending_definitions_.empty()) {
            output.push_back(std::move(pending_definitions_.front()));
            pending_definitions_.erase(pending_definitions_.begin());
        }
        if (at(";")) {
            advance();
            return;
        }
        while (!at_end()) {
            Declarator declarator;
            if (!parse_declarator(specifiers.type, declarator, true, false)) {
                synchronize_declaration();
                return;
            }
            DeclPtr declaration = std::make_shared<Decl>();
            declaration->location = {peek().line, peek().column};
            declaration->name = declarator.name;
            declaration->type = declarator.type;
            declaration->parameters = std::move(declarator.parameters);
            declaration->specialization_arguments = declarator.template_arguments;
            declaration->owner_template_arguments =
                declarator.owner_template_arguments;
            declaration->variadic = declarator.variadic;
            declaration->is_const = declarator.is_const || specifiers.is_const;
            declaration->is_volatile = declarator.is_volatile || specifiers.is_volatile;
            declaration->is_static = specifiers.is_static;
            declaration->is_virtual = specifiers.is_virtual;
            declaration->is_inline = specifiers.is_inline;
            declaration->is_explicit = specifiers.is_explicit;
            declaration->is_constexpr = specifiers.is_constexpr;
            declaration->is_friend = specifiers.is_friend;
            declaration->is_mutable = specifiers.is_mutable;
            declaration->is_thread_local = specifiers.is_thread_local;
            declaration->is_typedef = specifiers.is_typedef;
            declaration->is_extern = specifiers.is_extern;
            declaration->is_operator = declarator.is_operator;
            declaration->is_override = declarator.is_override;
            declaration->is_final = declarator.is_final;
            declaration->is_pure = declarator.is_pure;
            declaration->is_defaulted = declarator.is_defaulted;
            declaration->is_deleted = declarator.is_deleted;
            declaration->access = access_stack_.empty() ? Access::Public
                                                        : access_stack_.back();

            if (declarator.is_destructor) {
                declaration->kind = DeclKind::Function;
                declaration->is_destructor = true;
                declaration->type = Type::make_builtin(BuiltinKind::Void);
            } else if (declarator.is_constructor) {
                declaration->kind = DeclKind::Function;
                declaration->is_constructor = true;
                declaration->type = Type::make_builtin(BuiltinKind::Void);
            } else if (declarator.is_function) {
                declaration->kind = DeclKind::Function;
                if (declarator.trailing_return) {
                    declaration->type = declarator.trailing_return;
                }
            } else if (specifiers.is_typedef) {
                declaration->kind = DeclKind::TypeAlias;
            } else {
                declaration->kind = DeclKind::Variable;
            }

            if (declaration->kind == DeclKind::Function) {
                if (at(":")) {
                    parse_constructor_initializers(declaration->initializers);
                }
                if (at("{")) {
                    declaration->body = parse_compound_statement();
                } else {
                    expect(";", "after function declaration");
                }
                output.push_back(std::move(declaration));
                return;
            }

            if (at("(") && declaration->kind == DeclKind::Variable) {
                auto call = std::make_unique<Expr>();
                call->kind = ExprKind::Call;
                call->location = {peek().line, peek().column};
                auto callee = std::make_unique<Expr>();
                callee->kind = ExprKind::Name;
                if (declaration->type && declaration->type->kind == TypeKind::Named) {
                    callee->name = declaration->type->name;
                }
                call->children.push_back(std::move(callee));
                call->arguments = parse_argument_list();
                declaration->initializer = std::move(call);
            } else if (consume("=")) {
                if (at("default")) {
                    declaration->is_defaulted = true;
                    advance();
                } else if (at("delete")) {
                    declaration->is_deleted = true;
                    advance();
                } else {
                    declaration->initializer = parse_initializer_expression();
                }
            } else if (at("{")) {
                declaration->initializer = parse_initializer_list_expression();
            }
            output.push_back(std::move(declaration));
            if (!consume(",")) {
                break;
            }
        }
        expect(";", "after declaration");
    }

    void parse_constructor_or_destructor(bool destructor,
                                         std::vector<DeclPtr>& output,
                                         bool is_explicit = false) {
        Declarator declarator;
        declarator.is_function = true;
        declarator.is_destructor = destructor;
        if (destructor) {
            advance(); // ~
            declarator.name.parts.push_back(peek().text);
            advance();
        } else {
            declarator.name.parts.push_back(peek().text);
            advance();
        }
        declarator.is_constructor = !destructor;
        parse_parameter_list(declarator.parameters, declarator.variadic);
        parse_function_suffix(declarator);
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Function;
        declaration->name = declarator.name;
        declaration->type = Type::make_builtin(BuiltinKind::Void);
        declaration->parameters = std::move(declarator.parameters);
        declaration->variadic = declarator.variadic;
        declaration->is_constructor = declarator.is_constructor;
        declaration->is_destructor = declarator.is_destructor;
        declaration->is_explicit = is_explicit;
        declaration->access = access_stack_.empty() ? Access::Public
                                                    : access_stack_.back();
        if (at(":")) {
            parse_constructor_initializers(declaration->initializers);
        }
        if (at("{")) {
            declaration->body = parse_compound_statement();
        } else {
            expect(";", "after constructor declaration");
        }
        output.push_back(std::move(declaration));
    }

    void parse_constructor_initializers(std::vector<Initializer>& initializers) {
        expect(":", "before constructor initializer list");
        while (!at_end() && !at("{")) {
            Initializer initializer;
            initializer.name = parse_qualified_name(false);
            if (at("(")) {
                initializer.arguments = parse_argument_list();
            } else if (at("{")) {
                auto list = parse_initializer_list_expression();
                initializer.arguments.push_back(std::move(list));
            }
            for (const Name& base : current_bases_) {
                if (base.to_string() == initializer.name.to_string()) {
                    initializer.is_base = true;
                }
            }
            initializers.push_back(std::move(initializer));
            if (!consume(",")) {
                break;
            }
        }
    }

    void parse_template_declaration(Context context, std::vector<DeclPtr>& output) {
        expect("template", "at template declaration");
        std::vector<DeclPtr> parameters;
        bool specialization = false;
        if (expect("<", "after template")) {
            if (at(">")) {
                specialization = true;
            } else {
                while (!at_end() && !at(">") && !at(">>")) {
                    auto parameter = std::make_shared<Decl>();
                    parameter->kind = DeclKind::Parameter;
                    parameter->location = {peek().line, peek().column};
                    if (at("typename") || at("class")) {
                        advance();
                        if (at_identifier()) {
                            parameter->name.parts.push_back(peek().text);
                            advance();
                        }
                    } else {
                        Specifiers specifiers = parse_specifiers();
                        parameter->type = specifiers.type;
                        if (parameter->type && parameter->type->kind == TypeKind::Builtin &&
                            parameter->type->is_integral()) {
                            parameter->is_non_type_template_parameter = true;
                        }
                        if (at_identifier()) {
                            parameter->name.parts.push_back(peek().text);
                            advance();
                        }
                    }
                    if (consume("=")) {
                        parameter->condition = parse_assignment_expression();
                    }
                    parameters.push_back(std::move(parameter));
                    if (!consume(",")) {
                        break;
                    }
                }
            }
            consume_template_close();
        }
        std::vector<DeclPtr> declarations;
        for (const DeclPtr& parameter : parameters) {
            if (parameter && !parameter->name.parts.empty()) {
                add_known_type(parameter->name);
            }
        }
        std::vector<TypePtr> specialization_arguments;
        if (at("class") || at("struct") || at("union")) {
            DeclPtr definition = parse_class_definition(&specialization_arguments);
            add_known_type(definition->name);
            declarations.push_back(std::move(definition));
            consume(";");
        } else if (at("enum")) {
            DeclPtr definition = parse_enum_definition();
            add_known_type(definition->name);
            declarations.push_back(std::move(definition));
            consume(";");
        } else {
            parse_declaration(context, declarations);
        }
        for (DeclPtr& declaration : declarations) {
            declaration->is_template = true;
            declaration->is_specialization = specialization;
            declaration->template_parameters = parameters;
            declaration->specialization_arguments = specialization_arguments;
            output.push_back(std::move(declaration));
        }
    }

    void parse_namespace(std::vector<DeclPtr>& output) {
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Namespace;
        declaration->location = {peek().line, peek().column};
        expect("namespace", "at namespace");
        if (at("inline")) {
            advance();
        }
        if (at_identifier()) {
            declaration->name.parts.push_back(peek().text);
            advance();
            while (consume("::")) {
                if (at_identifier()) {
                    declaration->name.parts.push_back(peek().text);
                    advance();
                }
            }
        }
        if (!expect("{", "to open namespace")) {
            synchronize_declaration();
            return;
        }
        while (!at_end() && !at("}")) {
            if (consume(";")) {
                continue;
            }
            const std::size_t before = position_;
            std::vector<DeclPtr> declarations;
            parse_declaration(Context::Namespace, declarations);
            for (DeclPtr& child : declarations) {
                add_known_type(child->name);
                declaration->children.push_back(std::move(child));
            }
            if (position_ == before) {
                advance();
            }
        }
        expect("}", "to close namespace");
        output.push_back(std::move(declaration));
    }

    void parse_using(std::vector<DeclPtr>& output) {
        expect("using", "at using declaration");
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Using;
        declaration->location = {peek().line, peek().column};
        if (at("namespace")) {
            advance();
            declaration->is_using_namespace = true;
            declaration->name = parse_qualified_name(false);
            expect(";", "after using namespace");
            output.push_back(std::move(declaration));
            return;
        }
        if (at("typename")) {
            advance();
        }
        Name name = parse_qualified_name(false);
        if (consume("=")) {
            declaration->kind = DeclKind::TypeAlias;
            declaration->name = name;
            declaration->type = parse_type_id();
            expect(";", "after type alias");
            add_known_type(name);
            output.push_back(std::move(declaration));
            return;
        }
        declaration->name = name;
        expect(";", "after using declaration");
        output.push_back(std::move(declaration));
    }

    void parse_static_assert(std::vector<DeclPtr>& output) {
        expect("static_assert", "at static_assert");
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::StaticAssert;
        declaration->location = {peek().line, peek().column};
        expect("(", "after static_assert");
        declaration->condition = parse_assignment_expression();
        if (consume(",")) {
            declaration->message = parse_assignment_expression();
        }
        expect(")", "to close static_assert");
        expect(";", "after static_assert");
        output.push_back(std::move(declaration));
    }

    void parse_extern_c(Context context, std::vector<DeclPtr>& output) {
        expect("extern", "at extern linkage");
        advance(); // string literal
        if (consume("{")) {
            while (!at_end() && !at("}")) {
                std::vector<DeclPtr> declarations;
                parse_declaration(context, declarations);
                for (DeclPtr& declaration : declarations) {
                    declaration->is_extern_c = true;
                    output.push_back(std::move(declaration));
                }
            }
            expect("}", "to close extern \"C\" block");
            return;
        }
        std::vector<DeclPtr> declarations;
        parse_declaration(context, declarations);
        for (DeclPtr& declaration : declarations) {
            declaration->is_extern_c = true;
            output.push_back(std::move(declaration));
        }
    }

    DeclPtr parse_class_definition(
        std::vector<TypePtr>* specialization_arguments = nullptr) {
        const bool is_union = at("union");
        const bool is_class = at("class");
        advance(); // class/struct/union
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Class;
        declaration->location = {peek().line, peek().column};
        if (at_identifier()) {
            declaration->name.parts.push_back(peek().text);
            advance();
            while (consume("::")) {
                if (at_identifier()) {
                    declaration->name.parts.push_back(peek().text);
                    advance();
                }
            }
        }
        if (specialization_arguments != nullptr && at("<")) {
            *specialization_arguments = parse_template_type_arguments();
        }
        (void)is_class;
        (void)is_union;
        if (at("final")) {
            declaration->is_final = true;
            advance();
        }
        if (consume(":")) {
            while (!at_end() && !at("{")) {
                if (at("public") || at("private") || at("protected")) {
                    advance();
                }
                if (at("virtual")) {
                    advance();
                }
                declaration->base_names.push_back(parse_qualified_name(false));
                if (!consume(",")) {
                    break;
                }
            }
        }
        if (!expect("{", "to open class body")) {
            return declaration;
        }
        class_stack_.push_back(declaration->name.parts.empty()
                                   ? std::string()
                                   : declaration->name.parts.back());
        access_stack_.push_back(is_class ? Access::Private : Access::Public);
        current_bases_ = declaration->base_names;
        while (!at_end() && !at("}")) {
            if (consume(";")) {
                continue;
            }
            const std::size_t before = position_;
            std::vector<DeclPtr> members;
            parse_declaration(Context::Class, members);
            for (DeclPtr& member : members) {
                declaration->children.push_back(std::move(member));
            }
            if (position_ == before) {
                advance();
            }
        }
        expect("}", "to close class body");
        consume(";");
        access_stack_.pop_back();
        class_stack_.pop_back();
        current_bases_.clear();
        return declaration;
    }

    DeclPtr parse_enum_definition() {
        expect("enum", "at enum");
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Enum;
        declaration->location = {peek().line, peek().column};
        if (at("class") || at("struct")) {
            declaration->is_enum_class = true;
            advance();
        }
        if (at_identifier()) {
            declaration->name.parts.push_back(peek().text);
            advance();
        }
        if (consume(":")) {
            declaration->type = parse_type_id();
        } else {
            declaration->type = Type::make_builtin(BuiltinKind::Int);
        }
        if (!expect("{", "to open enum body")) {
            return declaration;
        }
        while (!at_end() && !at("}")) {
            if (consume(",")) {
                continue;
            }
            EnumMember member;
            member.location = {peek().line, peek().column};
            if (!at_identifier()) {
                error(peek(), "expected enumerator name");
                advance();
                continue;
            }
            member.name.parts.push_back(peek().text);
            advance();
            if (consume("=")) {
                member.value = parse_assignment_expression();
            }
            declaration->enum_members.push_back(std::move(member));
            if (!consume(",")) {
                break;
            }
        }
        expect("}", "to close enum body");
        return declaration;
    }

    bool is_declaration_start() const {
        if (is_declaration_specifier() || at("using") || at("typedef")) {
            return true;
        }
        if (at("::")) {
            return true;
        }
        if (!at_identifier()) {
            return false;
        }
        const std::string& name = peek().text;
        if (is_known_type(name)) {
            const std::string_view next = peek(1).text;
            return next == "::" || next == "*" || next == "&" ||
                   next == "&&" || next == "<" ||
                   peek(1).kind == TokenKind::Identifier ||
                   peek(1).kind == TokenKind::Keyword;
        }
        // `Foo x;`, `Foo* x;`, `Foo::Bar x;`, `Foo<T> x;`
        std::size_t index = 1;        if (peek(index).text == "::") {
            while (peek(index).text == "::") {
                ++index;
                if (peek(index).kind != TokenKind::Identifier &&
                    peek(index).kind != TokenKind::Keyword) {
                    return false;
                }
                ++index;
            }
        }
        if (peek(index).text == "<") {
            int depth = 0;
            while (index < tokens_.size()) {
                if (tokens_[index].text == "<") {
                    ++depth;
                } else if (tokens_[index].text == ">") {
                    --depth;
                    if (depth == 0) {
                        ++index;
                        break;
                    }
                } else if (tokens_[index].text == ">>") {
                    depth -= 2;
                    if (depth <= 0) {
                        ++index;
                        break;
                    }
                }
                ++index;
            }
        }
        const std::string_view next = peek(index).text;
        if (next == "*" || next == "&" || next == "&&") {
            return is_known_type(name);
        }
        return peek(index).kind == TokenKind::Identifier ||
               peek(index).kind == TokenKind::Keyword;
    }

    StmtPtr parse_compound_statement() {
        auto statement = std::make_shared<Stmt>();
        statement->kind = StmtKind::Compound;
        statement->location = {peek().line, peek().column};
        if (!expect("{", "to open block")) {
            return statement;
        }
        while (!at_end() && !at("}")) {
            if (consume(";")) {
                continue;
            }
            const std::size_t before = position_;
            if (is_declaration_start()) {
                std::vector<DeclPtr> declarations;
                parse_declaration(Context::Block, declarations);
                for (DeclPtr& declaration : declarations) {
                    auto declaration_statement = std::make_shared<Stmt>();
                    declaration_statement->kind = StmtKind::Declaration;
                    declaration_statement->location = declaration->location;
                    declaration_statement->declaration = std::move(declaration);
                    statement->statements.push_back(std::move(declaration_statement));
                }
            } else {
                statement->statements.push_back(parse_statement());
            }
            if (position_ == before) {
                advance();
                synchronize_statement();
            }
        }
        expect("}", "to close block");
        return statement;
    }

    StmtPtr parse_statement() {
        auto statement = std::make_shared<Stmt>();
        statement->location = {peek().line, peek().column};
        if (at("{")) {
            return parse_compound_statement();
        }
        if (consume(";")) {
            statement->kind = StmtKind::Empty;
            return statement;
        }
        if (consume("if")) {
            statement->kind = StmtKind::If;
            expect("(", "after if");
            if (is_declaration_start()) {
                std::vector<DeclPtr> declarations;
                parse_declaration(Context::Block, declarations);
                if (!declarations.empty()) {
                    statement->declaration = declarations.front();
                }
            }
            statement->condition = parse_expression();
            expect(")", "after if condition");
            statement->body = parse_statement();
            if (consume("else")) {
                statement->else_body = parse_statement();
            }
            return statement;
        }
        if (consume("while")) {
            statement->kind = StmtKind::While;
            expect("(", "after while");
            statement->condition = parse_expression();
            expect(")", "after while condition");
            statement->body = parse_statement();
            return statement;
        }
        if (consume("do")) {
            statement->kind = StmtKind::DoWhile;
            statement->body = parse_statement();
            expect("while", "after do body");
            expect("(", "after do-while while");
            statement->condition = parse_expression();
            expect(")", "after do-while condition");
            expect(";", "after do-while");
            return statement;
        }
        if (consume("for")) {
            return parse_for_statement();
        }
        if (consume("switch")) {
            statement->kind = StmtKind::Switch;
            expect("(", "after switch");
            statement->condition = parse_expression();
            expect(")", "after switch condition");
            statement->body = parse_statement();
            return statement;
        }
        if (consume("case")) {
            statement->kind = StmtKind::Case;
            statement->expression = parse_expression();
            expect(":", "after case label");
            return statement;
        }
        if (consume("default")) {
            statement->kind = StmtKind::Default;
            expect(":", "after default label");
            return statement;
        }
        if (consume("break")) {
            statement->kind = StmtKind::Break;
            expect(";", "after break");
            return statement;
        }
        if (consume("continue")) {
            statement->kind = StmtKind::Continue;
            expect(";", "after continue");
            return statement;
        }
        if (consume("return")) {
            statement->kind = StmtKind::Return;
            if (!at(";")) {
                statement->expression = parse_expression();
            }
            expect(";", "after return");
            return statement;
        }
        if (consume("throw")) {
            statement->kind = StmtKind::Throw;
            if (!at(";")) {
                statement->expression = parse_expression();
            }
            expect(";", "after throw");
            return statement;
        }
        if (consume("try")) {
            statement->kind = StmtKind::Try;
            statement->body = parse_compound_statement();
            while (consume("catch")) {
                CatchHandler handler;
                handler.location = {peek().line, peek().column};
                expect("(", "after catch");
                if (!consume("...")) {
                    Specifiers specifiers = parse_specifiers();
                    Declarator declarator;
                    parse_declarator(specifiers.type, declarator, false, true);
                    auto parameter = std::make_shared<Decl>();
                    parameter->kind = DeclKind::Parameter;
                    parameter->type = declarator.type;
                    parameter->name = declarator.name;
                    handler.parameter = std::move(parameter);
                }
                expect(")", "after catch parameter");
                handler.body = parse_compound_statement();
                statement->handlers.push_back(std::move(handler));
            }
            return statement;
        }
        if (consume("goto")) {
            statement->kind = StmtKind::Goto;
            statement->label = peek().text;
            advance();
            expect(";", "after goto");
            return statement;
        }
        if (at_identifier() && peek(1).text == ":") {
            statement->kind = StmtKind::Label;
            statement->label = peek().text;
            advance();
            advance();
            return statement;
        }
        if (is_declaration_start()) {
            std::vector<DeclPtr> declarations;
            parse_declaration(Context::Block, declarations);
            statement->kind = StmtKind::Declaration;
            if (!declarations.empty()) {
                statement->declaration = declarations.front();
            }
            return statement;
        }
        statement->kind = StmtKind::Expression;
        statement->expression = parse_expression();
        expect(";", "after expression statement");
        return statement;
    }

    DeclPtr parse_range_declaration() {
        Specifiers specifiers = parse_specifiers();
        Declarator declarator;
        parse_declarator(specifiers.type, declarator, false, false);
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Variable;
        declaration->location = {peek().line, peek().column};
        declaration->name = declarator.name;
        declaration->type = declarator.type;
        declaration->is_const = specifiers.is_const;
        declaration->is_volatile = specifiers.is_volatile;
        return declaration;
    }

    StmtPtr parse_for_statement() {
        auto statement = std::make_shared<Stmt>();
        statement->kind = StmtKind::For;
        statement->location = {peek().line, peek().column};
        expect("(", "after for");
        if (looks_like_range_for()) {
            statement->kind = StmtKind::RangeFor;
            if (is_declaration_start()) {
                statement->declaration = parse_range_declaration();
            } else {
                statement->condition = parse_expression();
            }
            expect(":", "in range-for");
            statement->expression = parse_expression();
            expect(")", "after range-for");
            statement->body = parse_statement();
            return statement;
        }
        if (consume(";")) {
            // empty init
        } else if (is_declaration_start()) {
            std::vector<DeclPtr> declarations;
            parse_declaration(Context::ForInit, declarations);
            if (!declarations.empty()) {
                statement->declaration = declarations.front();
            }
        } else {
            statement->expression = parse_expression();
            expect(";", "after for initializer");
        }
        if (!at(";")) {
            statement->condition = parse_expression();
        }
        expect(";", "after for condition");
        if (!at(")")) {
            statement->increment = parse_expression();
        }
        expect(")", "after for");
        statement->body = parse_statement();
        return statement;
    }

    bool looks_like_range_for() const {
        int parens = 0;
        int brackets = 0;
        for (std::size_t index = position_; index < tokens_.size(); ++index) {
            const std::string& text = tokens_[index].text;
            if (text == "(") {
                ++parens;
            } else if (text == ")") {
                if (parens == 0) {
                    return false;
                }
                --parens;
            } else if (text == "[") {
                ++brackets;
            } else if (text == "]") {
                --brackets;
            } else if (text == ";" && parens == 0 && brackets == 0) {
                return false;
            } else if (text == ":" && parens == 0 && brackets == 0) {
                return true;
            }
        }
        return false;
    }

    ExprPtr parse_expression() { return parse_assignment_expression(); }

    ExprPtr parse_assignment_expression() {
        ExprPtr left = parse_conditional_expression();
        if (is_assignment_operator(peek().text)) {
            const Token token = peek();
            advance();
            auto expression = make_expr(ExprKind::Assign, token);
            expression->text = token.text;
            expression->children.push_back(std::move(left));
            expression->children.push_back(parse_assignment_expression());
            return expression;
        }
        return left;
    }

    ExprPtr parse_conditional_expression() {
        ExprPtr condition = parse_binary_expression(1);
        if (consume("?")) {
            auto expression = std::make_unique<Expr>();
            expression->kind = ExprKind::Conditional;
            expression->children.push_back(std::move(condition));
            expression->children.push_back(parse_expression());
            expect(":", "in conditional expression");
            expression->children.push_back(parse_assignment_expression());
            return expression;
        }
        return condition;
    }

    ExprPtr parse_binary_expression(int minimum_precedence) {
        ExprPtr left = parse_unary_expression();
        while (!at_end()) {
            const int precedence = binary_precedence(peek().text);
            if (precedence < minimum_precedence) {
                break;
            }
            const Token token = peek();
            advance();
            auto expression = make_expr(ExprKind::Binary, token);
            expression->text = token.text;
            expression->children.push_back(std::move(left));
            expression->children.push_back(parse_binary_expression(precedence + 1));
            left = std::move(expression);
        }
        return left;
    }

    ExprPtr parse_unary_expression() {
        const Token token = peek();
        if (at("++") || at("--") || at("+") || at("-") || at("!") || at("~") ||
            at("*") || at("&")) {
            advance();
            auto expression = make_expr(ExprKind::Unary, token);
            expression->text = token.text;
            expression->is_prefix = true;
            expression->children.push_back(parse_unary_expression());
            return expression;
        }
        if (consume("sizeof")) {
            auto expression = make_expr(ExprKind::Sizeof, token);
            if (at("(") && looks_like_type_inside_parentheses()) {
                advance();
                expression->type = parse_type_id();
                expect(")", "after sizeof type");
            } else {
                expression->children.push_back(parse_unary_expression());
            }
            return expression;
        }
        if (consume("alignof")) {
            auto expression = make_expr(ExprKind::Alignof, token);
            expect("(", "after alignof");
            expression->type = parse_type_id();
            expect(")", "after alignof type");
            return expression;
        }
        if (consume("new")) {
            return parse_new_expression(token);
        }
        if (consume("delete")) {
            auto expression = make_expr(ExprKind::Delete, token);
            if (at("[") && peek(1).text == "]") {
                advance();
                advance();
                expression->text = "[]";
            }
            expression->children.push_back(parse_unary_expression());
            return expression;
        }
        if (consume("typeid")) {
            auto expression = make_expr(ExprKind::Typeid, token);
            if (at("(") && looks_like_type_inside_parentheses()) {
                advance();
                expression->type = parse_type_id();
                expect(")", "after typeid type");
            } else {
                expect("(", "after typeid");
                expression->children.push_back(parse_expression());
                expect(")", "after typeid operand");
            }
            return expression;
        }
        if (at("static_cast") || at("dynamic_cast") || at("reinterpret_cast") ||
            at("const_cast")) {
            const std::string kind = peek().text;
            advance();
            auto expression = make_expr(ExprKind::Cast, token);
            expression->text = kind;
            expect("<", "in cast expression");
            expression->type = parse_type_id();
            consume_template_close();
            expect("(", "after cast type");
            expression->children.push_back(parse_expression());
            expect(")", "after cast operand");
            return expression;
        }
        if (at("(") && looks_like_type_inside_parentheses()) {
            advance();
            auto expression = make_expr(ExprKind::Cast, token);
            expression->type = parse_type_id();
            expect(")", "after cast type");
            expression->children.push_back(parse_unary_expression());
            return expression;
        }
        if (at("co_await")) {
            advance();
            return parse_unary_expression();
        }
        return parse_postfix_expression();
    }

    bool looks_like_type_inside_parentheses() const {
        std::size_t index = position_;
        if (index >= tokens_.size() || tokens_[index].text != "(") {
            return false;
        }
        ++index;
        if (index >= tokens_.size()) {
            return false;
        }
        const Token& first = tokens_[index];
        if (is_decl_specifier_text(first.text)) {
            return true;
        }
        if (first.text == "::") {
            return true;
        }
        if (first.kind == TokenKind::Identifier) {
            if (is_known_type(first.text)) {
                return true;
            }
            if (index + 1 < tokens_.size()) {
                const std::string& next = tokens_[index + 1].text;
                if (next == "::" || next == "*" || next == "&" || next == "&&" ||
                    next == "<") {
                    return true;
                }
            }
        }
        return false;
    }

    ExprPtr parse_new_expression(const Token& token) {
        auto expression = make_expr(ExprKind::New, token);
        if (at("(")) {
            expression->arguments = parse_argument_list();
        }
        expression->type = parse_type_id();
        if (at("[")) {
            advance();
            expression->children.push_back(parse_expression());
            expect("]", "after new array bound");
        } else if (at("(")) {
            expression->arguments = parse_argument_list();
        }
        return expression;
    }

    ExprPtr parse_postfix_expression() {
        ExprPtr expression = parse_primary_expression();
        while (!at_end()) {
            if (at("(")) {
                if (expression->kind == ExprKind::Name &&
                    expression->name.to_string() == "__builtin_va_arg") {
                    auto call = std::make_unique<Expr>();
                    call->kind = ExprKind::Call;
                    call->location = {peek().line, peek().column};
                    call->children.push_back(std::move(expression));
                    expect("(", "in va_arg");
                    call->arguments.push_back(parse_assignment_expression());
                    expect(",", "in va_arg");
                    call->type = parse_type_id();
                    expect(")", "after va_arg type");
                    expression = std::move(call);
                    continue;
                }
                auto call = std::make_unique<Expr>();
                call->kind = ExprKind::Call;
                call->location = {peek().line, peek().column};
                call->children.push_back(std::move(expression));
                call->arguments = parse_argument_list();
                expression = std::move(call);
                continue;
            }
            if (consume("[")) {
                auto index = std::make_unique<Expr>();
                index->kind = ExprKind::Index;
                index->children.push_back(std::move(expression));
                index->children.push_back(parse_expression());
                expect("]", "after index expression");
                expression = std::move(index);
                continue;
            }
            if (at(".") || at("->")) {
                const bool arrow = at("->");
                advance();
                auto member = std::make_unique<Expr>();
                member->kind = ExprKind::Member;
                member->is_arrow = arrow;
                member->location = {peek().line, peek().column};
                member->children.push_back(std::move(expression));
                if (at("~")) {
                    member->name.parts.push_back("~" + peek(1).text);
                    advance();
                    advance();
                } else if (at("template")) {
                    advance();
                    member->name.parts.push_back(peek().text);
                    advance();
                } else {
                    member->name.parts.push_back(peek().text);
                    advance();
                }
                expression = std::move(member);
                continue;
            }
            if (at("++") || at("--")) {
                const Token op = peek();
                advance();
                auto unary = make_expr(ExprKind::Unary, op);
                unary->text = op.text;
                unary->is_prefix = false;
                unary->children.push_back(std::move(expression));
                expression = std::move(unary);
                continue;
            }
            break;
        }
        return expression;
    }

    ExprPtr parse_primary_expression() {
        const Token token = peek();
        if (token.kind == TokenKind::IntegerLiteral) {
            advance();
            auto expression = make_expr(ExprKind::IntegerLiteral, token);
            expression->integer = token.integer;
            expression->text = token.raw;
            return expression;
        }
        if (token.kind == TokenKind::FloatLiteral) {
            advance();
            auto expression = make_expr(ExprKind::FloatLiteral, token);
            expression->floating = token.floating;
            expression->text = token.raw;
            return expression;
        }
        if (token.kind == TokenKind::CharacterLiteral) {
            advance();
            auto expression = make_expr(ExprKind::CharacterLiteral, token);
            expression->integer = token.integer;
            expression->text = token.raw;
            return expression;
        }
        if (token.kind == TokenKind::StringLiteral) {
            advance();
            std::string value = token.text;
            while (peek().kind == TokenKind::StringLiteral) {
                value += peek().text;
                advance();
            }
            auto expression = make_expr(ExprKind::StringLiteral, token);
            expression->text = std::move(value);
            return expression;
        }
        if (consume("true") || consume("false")) {
            auto expression = make_expr(ExprKind::BoolLiteral, token);
            expression->integer = token.text == "true" ? 1 : 0;
            return expression;
        }
        if (consume("nullptr")) {
            return make_expr(ExprKind::NullptrLiteral, token);
        }
        if (consume("this")) {
            return make_expr(ExprKind::This, token);
        }
        if (at("[")) {
            return parse_lambda_expression();
        }
        if (at("{")) {
            return parse_initializer_list_expression();
        }
        if (at("(")) {
            advance();
            ExprPtr expression = parse_expression();
            expect(")", "after parenthesized expression");
            return expression;
        }
        if (at("::") || at_identifier() || peek().kind == TokenKind::Keyword) {
            auto expression = make_expr(ExprKind::Name, token);
            expression->name = parse_qualified_name(false);
            if (at("<") && looks_like_template_arguments()) {
                expression->template_type_arguments =
                    parse_template_type_arguments();
                if (consume("::") && at_identifier()) {
                    expression->name.parts.push_back(peek().text);
                    advance();
                }
            }
            return expression;
        }
        error(token, "expected expression");
        advance();
        return make_expr(ExprKind::Unknown, token);
    }

    bool looks_like_template_arguments() const {
        if (!at("<")) {
            return false;
        }
        std::size_t index = position_ + 1;
        int depth = 1;
        while (index < tokens_.size() && depth > 0) {
            const std::string& text = tokens_[index].text;
            if (text == "<") {
                ++depth;
            } else if (text == ">") {
                --depth;
            } else if (text == ">>") {
                depth -= 2;
            } else if (text == ";" || text == "{" || text == "}") {
                return false;
            }
            ++index;
        }
        return depth <= 0;
    }

    ExprPtr parse_lambda_expression() {
        const Token token = peek();
        auto expression = make_expr(ExprKind::Lambda, token);
        expect("[", "at lambda capture");
        std::string capture;
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (at("[")) {
                ++depth;
            } else if (at("]")) {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            capture += peek().text;
            advance();
        }
        expect("]", "after lambda capture");
        expression->text = capture;
        if (at("(")) {
            std::vector<Param> parameters;
            bool variadic = false;
            parse_parameter_list(parameters, variadic);
            for (Param& parameter : parameters) {
                auto declaration = std::make_shared<Decl>();
                declaration->kind = DeclKind::Parameter;
                declaration->type = parameter.type;
                declaration->name = parameter.name;
                expression->parameters.push_back(std::move(declaration));
            }
        }
        while (at("mutable") || at("constexpr") || at("noexcept")) {
            advance();
            if (at("(")) {
                skip_balanced("(", ")");
            }
        }
        if (consume("->")) {
            expression->type = parse_type_id();
        }
        expression->body = parse_compound_statement();
        return expression;
    }

    ExprPtr parse_initializer_list_expression() {
        const Token token = peek();
        auto expression = make_expr(ExprKind::InitializerList, token);
        expect("{", "at initializer list");
        if (consume("}")) {
            return expression;
        }
        while (!at_end()) {
            expression->children.push_back(parse_initializer_expression());
            if (!consume(",")) {
                break;
            }
            if (at("}")) {
                break;
            }
        }
        expect("}", "after initializer list");
        return expression;
    }

    ExprPtr parse_initializer_expression() {
        if (at("{")) {
            return parse_initializer_list_expression();
        }
        if (at(".")) {
            advance();
            if (at_identifier()) {
                const std::string field = peek().text;
                advance();
                expect("=", "after field designator");
                ExprPtr value = parse_assignment_expression();
                value->has_field_designator = true;
                value->field_designator = field;
                return value;
            }
            error(peek(), "expected field name after designator");
            return parse_assignment_expression();
        }
        if (at("[") && peek(1).kind == TokenKind::IntegerLiteral) {
            advance();
            if (peek().kind == TokenKind::IntegerLiteral) {
                const int index = static_cast<int>(peek().integer);
                advance();
                expect("]", "after array designator");
                expect("=", "after array designator");
                ExprPtr value = parse_assignment_expression();
                value->has_index_designator = true;
                value->index_designator = index;
                return value;
            }
            error(peek(), "array designator requires an integer literal");
            return parse_assignment_expression();
        }
        return parse_assignment_expression();
    }

    std::vector<ExprPtr> parse_argument_list() {
        std::vector<ExprPtr> arguments;
        if (!expect("(", "in argument list")) {
            return arguments;
        }
        if (consume(")")) {
            return arguments;
        }
        while (!at_end()) {
            arguments.push_back(parse_initializer_expression());
            if (!consume(",")) {
                break;
            }
        }
        expect(")", "after argument list");
        return arguments;
    }

    std::vector<Token> tokens_;
    std::string source_name_;
    std::vector<Diagnostic>& diagnostics_;
    std::size_t position_ = 0;
    std::vector<std::string> class_stack_;
    std::vector<Access> access_stack_;
    std::vector<Name> current_bases_;
    std::unordered_set<std::string> known_types_;
    std::vector<DeclPtr> pending_definitions_;
};

} // namespace

std::shared_ptr<TranslationUnit> parse_tokens(std::vector<Token> tokens,
                                              std::string_view source_name,
                                              std::vector<Diagnostic>& diagnostics) {
    Parser parser(std::move(tokens), source_name, diagnostics);
    std::shared_ptr<TranslationUnit> unit = parser.run();
    std::stable_sort(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic& left, const Diagnostic& right) {
                         if (left.line != right.line) {
                             return left.line < right.line;
                         }
                         if (left.column != right.column) {
                             return left.column < right.column;
                         }
                         return left.message < right.message;
                     });
    return unit;
}

std::shared_ptr<TranslationUnit> parse(std::string_view source,
                                       std::string_view source_name,
                                       std::vector<Diagnostic>& diagnostics) {
    std::string preprocessed = preprocess(source, source_name, diagnostics);
    std::vector<Token> tokens = lex(preprocessed, source_name, diagnostics);
    return parse_tokens(std::move(tokens), source_name, diagnostics);
}

} // namespace kr32::cxx
