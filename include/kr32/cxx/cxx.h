#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::cxx {

enum class Emit {
    Assembly,
    Object,
};

struct Options {
    Emit emit = Emit::Assembly;
    std::string source_name = "<input>";
    bool exceptions = true;
    bool threads = true;
};

struct Diagnostic {
    int line = 0;
    int column = 0;
    std::string message;
};

struct Result {
    bool ok = false;
    std::string assembly;
    std::vector<std::uint8_t> object;
    std::vector<Diagnostic> diagnostics;
};

// Compile one C++ translation unit to KR32 assembly or a relocatable object.
// The function is filesystem-free; the caller supplies source text and writes
// any output. This API is intentionally stable so the standalone driver and
// the later `kr32 cxx`/`kr32 build file.cpp` paths can share it.
[[nodiscard]] Result compile(std::string_view source, const Options& options = {});

} // namespace kr32::cxx
