#pragma once

#include <string>

namespace kr32::assembler {

struct OptimizeStats {
    int removed = 0;
    int rewritten = 0;
};

// Applies target-level peephole optimizations to generated KR32 assembly.
//
// The pass is intentionally conservative: it only forwards stack-slot stores
// within a basic block and rewrites constant materialization immediately
// consumed by a compare or ALU operation. Calls, labels, branches, and unknown
// memory writes reset the tracked state.
[[nodiscard]] OptimizeStats optimize_assembly(std::string& assembly);

} // namespace kr32::assembler
