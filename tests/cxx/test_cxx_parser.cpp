#include "doctest.h"

#include "cxx/ast.h"
#include "cxx/lexer.h"
#include "cxx/parser.h"
#include "cxx/preprocessor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace kr32::cxx;

std::vector<Token> tokenize(std::string_view source,
                            std::vector<Diagnostic>& diagnostics) {
    return lex(source, "<test>", diagnostics);
}

std::shared_ptr<ast::TranslationUnit> parse_source(
    std::string_view source, std::vector<Diagnostic>& diagnostics) {
    return parse(source, "<test>", diagnostics);
}

} // namespace

TEST_CASE("lexer recognizes keywords, identifiers, and literals") {
    std::vector<Diagnostic> diagnostics;
    const auto tokens = tokenize(
        "class Widget { int value = 0x2A; float f = 1.5e2f; char c = '\\n'; "
        "const char* s = \"hi\"; };",
        diagnostics);

    CHECK(diagnostics.empty());
    REQUIRE(tokens.size() > 10);
    CHECK(tokens[0].kind == TokenKind::Keyword);
    CHECK(tokens[0].text == "class");
    CHECK(tokens[1].kind == TokenKind::Identifier);
    CHECK(tokens[1].text == "Widget");
    CHECK(tokens[2].text == "{");
    bool saw_hex = false;
    bool saw_float = false;
    bool saw_char = false;
    bool saw_string = false;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::IntegerLiteral && token.integer == 42) {
            saw_hex = true;
        }
        if (token.kind == TokenKind::FloatLiteral && token.floating > 149.0 &&
            token.floating < 151.0) {
            saw_float = true;
        }
        if (token.kind == TokenKind::CharacterLiteral && token.integer == '\n') {
            saw_char = true;
        }
        if (token.kind == TokenKind::StringLiteral && token.text == "hi") {
            saw_string = true;
        }
    }
    CHECK(saw_hex);
    CHECK(saw_float);
    CHECK(saw_char);
    CHECK(saw_string);
}

TEST_CASE("lexer handles multi-character punctuators") {
    std::vector<Diagnostic> diagnostics;
    const auto tokens = tokenize("a >>= b; c...d; e::f; g->*h; i<=>j;", diagnostics);
    CHECK(diagnostics.empty());

    std::vector<std::string> punctuators;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Punctuator) {
            punctuators.push_back(token.text);
        }
    }
    CHECK(std::find(punctuators.begin(), punctuators.end(), ">>=") !=
          punctuators.end());
    CHECK(std::find(punctuators.begin(), punctuators.end(), "...") !=
          punctuators.end());
    CHECK(std::find(punctuators.begin(), punctuators.end(), "::") !=
          punctuators.end());
    CHECK(std::find(punctuators.begin(), punctuators.end(), "->*") !=
          punctuators.end());
    CHECK(std::find(punctuators.begin(), punctuators.end(), "<=>") !=
          punctuators.end());
}

TEST_CASE("lexer skips comments and reports unterminated input") {
    std::vector<Diagnostic> diagnostics;
    const auto tokens = tokenize(
        "int a; // line\n/* block\ncomment */ int b;", diagnostics);
    CHECK(diagnostics.empty());
    int ints = 0;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Keyword && token.text == "int") {
            ++ints;
        }
    }
    CHECK(ints == 2);

    std::vector<Diagnostic> bad_diagnostics;
    tokenize("int a = \"unterminated;", bad_diagnostics);
    CHECK_FALSE(bad_diagnostics.empty());
}

TEST_CASE("preprocessor expands macros and preserves line count") {
    std::vector<Diagnostic> diagnostics;
    const std::string preprocessed = preprocess(
        "#define N 3\n#define ADD(a, b) ((a) + (b))\n"
        "int values[N];\nint sum = ADD(1, 2);\n",
        "<test>", diagnostics);
    CHECK(diagnostics.empty());
    const auto tokens = tokenize(preprocessed, diagnostics);
    CHECK(diagnostics.empty());

    bool saw_three = false;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::IntegerLiteral && token.integer == 3) {
            saw_three = true;
        }
    }
    CHECK(saw_three);

    // The `int` on the third line of the source is still on line 3.
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Keyword && token.text == "int") {
            CHECK(token.line == 3);
            break;
        }
    }
}

TEST_CASE("preprocessor conditional directives") {
    std::vector<Diagnostic> diagnostics;
    const std::string text = preprocess(
        "#define ENABLED 1\n"
        "#ifdef ENABLED\nint yes;\n#else\nint no;\n#endif\n"
        "#if 0\nint zero;\n#endif\n"
        "#if defined(ENABLED) && ENABLED\nint both;\n#endif\n",
        "<test>", diagnostics);
    CHECK(diagnostics.empty());
    CHECK(text.find("int yes;") != std::string::npos);
    CHECK(text.find("int both;") != std::string::npos);
    CHECK(text.find("int no;") == std::string::npos);
    CHECK(text.find("int zero;") == std::string::npos);
}

TEST_CASE("preprocessor reports unavailable includes") {
    std::vector<Diagnostic> diagnostics;
    const std::string text = preprocess("#include <vector>\nint x;\n", "<test>", diagnostics);
    (void)text;
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().message.find("header") != std::string::npos);
}

TEST_CASE("parser survives a malformed input corpus") {
    const std::vector<std::string> cases = {
        "",
        "class",
        "struct S {",
        "int f(",
        "int f( { return 1; }",
        "int value = ;",
        "for (;;",
        "while (",
        "template <",
        "template <typename T>",
        "namespace {",
        "extern \"C\" {",
        "class C { public: C( };",
        "int a[;",
        "a::",
        "#if 0\nint x;\n",
        "int x = 1 + ;",
        "void f() { if (x) { } else",
    };
    for (const std::string& source : cases) {
        std::vector<Diagnostic> diagnostics;
        const auto unit = parse_source(source, diagnostics);
        CHECK(static_cast<bool>(unit));
        // Every rejected input must explain itself; an empty translation unit is
        // the only accepted input in this corpus.
        if (!source.empty()) {
            CHECK_FALSE(diagnostics.empty());
        }
    }
}

TEST_CASE("parser builds a function declaration and body") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "int add(int a, int b) { int c = a + b; return c; }", diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 1);
    const ast::Decl& function = *unit->declarations[0];
    CHECK(function.kind == ast::DeclKind::Function);
    CHECK(function.name.to_string() == "add");
    CHECK(function.type->is_builtin(ast::BuiltinKind::Int));
    CHECK(function.parameters.size() == 2);
    CHECK(function.parameters[0].name.to_string() == "a");
    REQUIRE(static_cast<bool>(function.body));
    CHECK(function.body->kind == ast::StmtKind::Compound);
    REQUIRE(function.body->statements.size() == 2);
    CHECK(function.body->statements[0]->kind == ast::StmtKind::Declaration);
    CHECK(function.body->statements[1]->kind == ast::StmtKind::Return);
}

TEST_CASE("parser respects arithmetic precedence") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source("int value = 1 + 2 * 3;", diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 1);
    const ast::Decl& variable = *unit->declarations[0];
    REQUIRE(static_cast<bool>(variable.initializer));
    REQUIRE(variable.initializer->kind == ast::ExprKind::Binary);
    CHECK(variable.initializer->text == "+");
    REQUIRE(variable.initializer->children.size() == 2);
    CHECK(variable.initializer->children[1]->kind == ast::ExprKind::Binary);
    CHECK(variable.initializer->children[1]->text == "*");
}

TEST_CASE("parser handles namespaces and qualified declarations") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "namespace ns { int add(int a, int b); class Widget { public: int value; }; }",
        diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 1);
    const ast::Decl& ns = *unit->declarations[0];
    CHECK(ns.kind == ast::DeclKind::Namespace);
    CHECK(ns.name.to_string() == "ns");
    REQUIRE(ns.children.size() == 2);
    CHECK(ns.children[0]->kind == ast::DeclKind::Function);
    CHECK(ns.children[0]->name.to_string() == "add");
    CHECK(ns.children[1]->kind == ast::DeclKind::Class);
    CHECK(ns.children[1]->name.to_string() == "Widget");
}

TEST_CASE("parser builds classes, constructors, destructors, and virtuals") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "class Widget { public: Widget(); ~Widget(); virtual int draw() const = 0; "
        "private: int value; };",
        diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 1);
    const ast::Decl& widget = *unit->declarations[0];
    CHECK(widget.kind == ast::DeclKind::Class);
    CHECK(widget.name.to_string() == "Widget");
    REQUIRE(widget.children.size() == 4);
    CHECK(widget.children[0]->is_constructor);
    CHECK(widget.children[1]->is_destructor);
    CHECK(widget.children[2]->kind == ast::DeclKind::Function);
    CHECK(widget.children[2]->name.to_string() == "draw");
    CHECK(widget.children[2]->is_virtual);
    CHECK(widget.children[2]->is_const);
    CHECK(widget.children[2]->is_pure);
    CHECK(widget.children[3]->kind == ast::DeclKind::Variable);
    CHECK(widget.children[3]->access == ast::Access::Private);
}

TEST_CASE("parser builds control-flow statements") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "void run(int n) { int sum = 0; for (int i = 0; i < n; ++i) { sum += i; } "
        "while (sum > 0) { --sum; } if (sum == 0) { return; } else { return; } }",
        diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 1);
    const ast::Decl& function = *unit->declarations[0];
    REQUIRE(static_cast<bool>(function.body));
    REQUIRE(function.body->statements.size() == 4);
    CHECK(function.body->statements[0]->kind == ast::StmtKind::Declaration);
    CHECK(function.body->statements[1]->kind == ast::StmtKind::For);
    CHECK(function.body->statements[1]->body->kind == ast::StmtKind::Compound);
    CHECK(function.body->statements[2]->kind == ast::StmtKind::While);
    CHECK(function.body->statements[3]->kind == ast::StmtKind::If);
    CHECK(static_cast<bool>(function.body->statements[3]->else_body));
}

TEST_CASE("parser handles range-for and lambdas") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "void run(int* first, int* last) { for (int value : first) { } "
        "auto f = [](int x) { return x + 1; }; }",
        diagnostics);
    REQUIRE(diagnostics.empty());
    const ast::Decl& function = *unit->declarations[0];
    REQUIRE(static_cast<bool>(function.body));
    REQUIRE(function.body->statements.size() == 2);
    CHECK(function.body->statements[0]->kind == ast::StmtKind::RangeFor);
    CHECK(static_cast<bool>(function.body->statements[0]->declaration));
    CHECK(function.body->statements[1]->kind == ast::StmtKind::Declaration);
    const ast::Decl& lambda_variable = *function.body->statements[1]->declaration;
    REQUIRE(static_cast<bool>(lambda_variable.initializer));
    CHECK(lambda_variable.initializer->kind == ast::ExprKind::Lambda);
}

TEST_CASE("parser handles function templates") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "template <typename T> T maximum(T a, T b) { return a > b ? a : b; }",
        diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 1);
    const ast::Decl& function = *unit->declarations[0];
    CHECK(function.is_template);
    CHECK(function.template_parameters.size() == 1);
    CHECK(function.name.to_string() == "maximum");
    REQUIRE(static_cast<bool>(function.body));
    REQUIRE(function.body->statements.size() == 1);
    REQUIRE(static_cast<bool>(function.body->statements[0]->expression));
    CHECK(function.body->statements[0]->expression->kind == ast::ExprKind::Conditional);
}

TEST_CASE("parser handles extern C and static_assert") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "extern \"C\" { int c_function(int); } static_assert(1 == 1, \"ok\");",
        diagnostics);
    REQUIRE(diagnostics.empty());
    REQUIRE(unit->declarations.size() == 2);
    CHECK(unit->declarations[0]->is_extern_c);
    CHECK(unit->declarations[0]->name.to_string() == "c_function");
    CHECK(unit->declarations[1]->kind == ast::DeclKind::StaticAssert);
}

TEST_CASE("parser recovers from malformed input") {
    std::vector<Diagnostic> diagnostics;
    const auto unit = parse_source(
        "int broken( { return 1; } int good = 2;", diagnostics);
    CHECK_FALSE(diagnostics.empty());
    CHECK(static_cast<bool>(unit));
    bool saw_good = false;
    for (const auto& declaration : unit->declarations) {
        if (declaration->name.to_string() == "good") {
            saw_good = true;
        }
    }
    CHECK(saw_good);
}

