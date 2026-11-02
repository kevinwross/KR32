#include "kr32/cxx/cxx.h"

#include "cxx/parser.h"

#include <utility>

namespace kr32::cxx {

Result compile(std::string_view source, const Options& options) {
    Result result;
    const std::shared_ptr<ast::TranslationUnit> unit =
        parse(source, options.source_name, result.diagnostics);
    if (!result.diagnostics.empty()) {
        return result;
    }
    (void)unit;

    // The lexer/parser/AST front end is in place; lowering and code generation
    // arrive in later M9 phases. A successful parse still reports that the
    // backend is missing rather than pretending to produce output.
    Diagnostic diagnostic;
    diagnostic.line = 1;
    diagnostic.column = 1;
    diagnostic.message =
        "C++ code generation is not implemented yet; the parser accepted the input";
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
}

} // namespace kr32::cxx
