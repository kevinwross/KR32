#include "kr32/cxx/cxx.h"

#include "cxx/codegen.h"
#include "cxx/parser.h"

#include <memory>
#include <utility>

namespace kr32::cxx {

Result compile(std::string_view source, const Options& options) {
    Result result;
    const std::shared_ptr<ast::TranslationUnit> unit =
        parse(source, options.source_name, result.diagnostics);
    if (!result.diagnostics.empty()) {
        return result;
    }

    if (options.emit == Emit::Object) {
        Diagnostic diagnostic;
        diagnostic.line = 1;
        diagnostic.column = 1;
        diagnostic.message =
            "relocatable object output is not implemented yet; use --emit asm";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    result.assembly = generate_assembly(*unit, result.diagnostics);
    if (!result.diagnostics.empty()) {
        result.assembly.clear();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace kr32::cxx
