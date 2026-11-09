# Compiler optimizations

`kr32 cc`, `kr32 cxx`, and `kr32 build` accept `-O0` (the default) and `-O1`.
`-O1` runs a conservative post-generation peephole pass over the assembly
produced by either front end. The pass is shared, so C and C++ receive the same
target-level cleanup.

## Current `-O1` passes

0. **Front-end constant folding and dead branches**
   - Integer constant expressions are folded before emission in both C and C++.
   - `1 + 2 * 3` becomes a single `MOV R0, #7`.
   - Constant right operands use immediate ALU and `CMP` forms, avoiding a
     register materialization and left-operand spill.
   - Scalar local reads in both C and C++ use `LOAD reg, [R7 + n]`
     directly instead of a `LEA` followed by an indirect load.
   - Scalar local assignments store directly to `[R7 + n]`, avoiding a
     temporary address register.
   - C and C++ `if (0)` bodies and `while (0)` loops are not emitted.
   - Only the required language constant-evaluation contexts were previously
     folded; ordinary expressions now use the same evaluation.

1. **Frame-slot forwarding**
   - Tracks `STORE reg, [R7 + n]` within a basic block.
   - Replaces a later `LOAD ..., [R7 + n]` with a register move when the stored
     value is still live.
   - Removes the load when the source and destination are the same register.
   - Calls, labels, branches, and unknown memory stores invalidate the state.

2. **Immediate consumer folding**
   - Propagates `MOV reg, #imm` through register copies.
   - Rewrites `CMP reg, reg` to `CMP reg, #imm`.
   - Rewrites immediate forms of `ADD`, `SUB`, `AND`, `OR`, `XOR`, and shifts.
   - Example:
     ```text
     MOV R4, #1
     MOV R5, R4
     CMP R3, R5
     ```
     becomes:
     ```text
     CMP R3, #1
     ```

3. **Dead and overwritten moves**
   - Removes `MOV Rd, Rd`.
   - Removes a move whose destination is overwritten before any read.
   - Removes moves whose destination is never read in the function.

4. **Unreachable statements**
   - Skips statements after `return`, `break`, `continue`, or `goto` until
     the next label.

5. **Redundant jumps**
   - Removes an unconditional jump whose target is the next label.

6. **Dead frame stores**
   - Removes a `STORE` to a frame slot that is never loaded or addressed in the
     function.
   - Keeps stores to slots whose address is taken with `LEA`.

7. **Frame address folding**
   - Folds `LEA R, [R7 + n]` followed by `LOAD Rd, [R]` into a direct
     `LOAD Rd, [R7 + n]`.
   - Folds `LEA R, [R7 + n]` followed by `STORE Rs, [R]` into a direct
     `STORE Rs, [R7 + n]`.
   - Removes the `LEA` only when its register is provably dead.

## Safety model

The pass is deliberately conservative:

- No optimization crosses a label, call, or branch boundary.
- Unknown memory writes clear frame-slot knowledge.
- Calls and returns count argument and return registers as reads.
- `-O0` remains the default, so existing debug behavior is unchanged.
- Line markers are kept so source-level debugging continues to work.

## Verification

The full C and C++ test suite can be run with optimization enabled:

```powershell
$env:KR32_TEST_OPTIMIZE='1'
ctest --test-dir build/windows-msvc -C Debug -j24 --output-on-failure
```

Unset the variable (or run without it) to exercise the normal `-O0` path.

## Planned next steps

1. More aggressive spill elimination with local value numbering.
2. Register allocation across straight-line regions.
3. Inlining and loop optimizations only after the above are stable.
