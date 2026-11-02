#pragma once

#include "kr32/cxx/cxx.h"

#include <string>
#include <string_view>
#include <vector>

namespace kr32::cxx {

// A small, freestanding C++ preprocessor for the M9 subset. It handles
// comments, line splices, object/function macros, the common conditional
// directives, and the built-in `__cplusplus`/`__KR32__`/`__LINE__` macros. It
// preserves line numbers so later diagnostics point at the original source.
//
// Include handling deliberately produces a diagnostic until the freestanding
// header set lands with the runtime; it never falls back to host headers.
[[nodiscard]] std::string preprocess(std::string_view source, std::string_view source_name,
                                     std::vector<Diagnostic>& diagnostics);

} // namespace kr32::cxx
