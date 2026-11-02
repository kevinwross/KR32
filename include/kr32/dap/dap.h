#pragma once

#include <string>

namespace kr32::dap {

struct Options {
    std::string image_path;
    int port = 0;          // 0 = stdio
    bool stdio = true;
};

// Runs a Debug Adapter Protocol endpoint. `initialize`, `launch`,
// `setBreakpoints`, `configurationDone`, threads/stack/scopes/variables,
// continue, next/stepIn/stepOut, evaluate, readMemory, and disconnect are
// implemented over the shared DebugSession.
[[nodiscard]] int run(const Options& options);

} // namespace kr32::dap
