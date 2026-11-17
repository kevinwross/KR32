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

// Whether the instruction writes the register named by its first operand. The ISA
// defines the roles per opcode family (docs/isa-v1.md §2): ALU, shift, MOV, LOAD,
// LEA, FLOAD and POP write it; STORE, FSTORE, PUSH, CMP, SETSP, the branches, and
// the reserved-operand system instructions do not. Exposed so a test can pin the
// table: classifying SETSP as a write once deleted the frame restore in front of a
// landing-pad jump.
[[nodiscard]] bool instruction_writes_first_operand(const std::string& text);

} // namespace kr32::assembler
