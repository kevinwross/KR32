#pragma once

#include "kr32/cxx/cxx.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::cxx {

enum class TokenKind {
    End,
    Identifier,
    Keyword,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    CharacterLiteral,
    Punctuator,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;    // spelling, or decoded value for strings
    std::string raw;     // literal spelling including quotes/suffix
    std::int64_t integer = 0;
    double floating = 0.0;
    int line = 1;
    int column = 1;
};

[[nodiscard]] bool is_keyword(std::string_view text);
[[nodiscard]] std::string_view token_kind_name(TokenKind kind);

// Lex source text that has already been preprocessed. Comments and line
// splices are accepted here as well so the lexer is independently testable.
[[nodiscard]] std::vector<Token> lex(std::string_view source, std::string_view source_name,
                                     std::vector<Diagnostic>& diagnostics);

} // namespace kr32::cxx
