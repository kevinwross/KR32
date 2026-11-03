#pragma once

#include "cxx/ast.h"
#include "cxx/lexer.h"
#include "kr32/cxx/cxx.h"

#include <memory>
#include <string_view>
#include <vector>

namespace kr32::cxx {

// Preprocess, lex, and parse one translation unit. Diagnostics are appended to
// `diagnostics`; the returned AST is always non-null and is partial after an
// error so tests can assert recovery behavior.
[[nodiscard]] std::shared_ptr<ast::TranslationUnit> parse(
    std::string_view source, std::string_view source_name,
    std::vector<Diagnostic>& diagnostics);

// Parse an already-preprocessed token stream (used by lexer/parser tests and
// later by include-expanded compilation).
[[nodiscard]] std::shared_ptr<ast::TranslationUnit> parse_tokens(
    std::vector<Token> tokens, std::string_view source_name,
    std::vector<Diagnostic>& diagnostics);

} // namespace kr32::cxx
