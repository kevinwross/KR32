#pragma once

#include "cxx/ast.h"
#include "kr32/cxx/cxx.h"

#include <string>
#include <string_view>
#include <vector>

namespace kr32::cxx {

// Itanium-like mangling for the M9 subset. There is no host ABI implication.
[[nodiscard]] std::string mangle_function(std::string_view qualified_name,
                                          const std::vector<ast::TypePtr>& parameter_types,
                                          bool extern_c, bool variadic = false);
[[nodiscard]] std::string mangle_variable(std::string_view qualified_name, bool extern_c);

// Lower a parsed translation unit to KR32 assembly. Returns an empty string and
// appends diagnostics when the unit uses a construct that is not implemented
// yet or fails semantic analysis.
[[nodiscard]] std::string generate_assembly(
    const ast::TranslationUnit& unit, std::vector<Diagnostic>& diagnostics,
    bool optimize = false);

} // namespace kr32::cxx
