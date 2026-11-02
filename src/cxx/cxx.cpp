#include "kr32/cxx/cxx.h"

#include <utility>

namespace kr32::cxx {

Result compile(std::string_view source, const Options& options) {
    (void)source;
    (void)options;
    Result result;
    Diagnostic diagnostic;
    diagnostic.line = 1;
    diagnostic.column = 1;
    diagnostic.message =
        "the C++ front end is not implemented yet; see docs/cpp-plan.md";
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
}

} // namespace kr32::cxx
