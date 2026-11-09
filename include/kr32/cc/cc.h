#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kr32::cc {

struct Diagnostic {
    int line = 0;
    int column = 0;
    std::string message;
};

struct Result {
    bool ok = false;
    std::string assembly;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] Result compile(std::string_view source,
                             std::string_view source_name = "<input>",
                             int source_id = 0, bool optimize = false);

// The M4 runtime: _start, putchar, and exit. It is assembled as a separate
// relocatable object and linked with the user object.
[[nodiscard]] std::string runtime_assembly();

} // namespace kr32::cc
