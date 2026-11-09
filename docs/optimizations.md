# Compiler optimizations

`kr32 cc`, `kr32 cxx`, and `kr32 build` accept `-O0` (the default) and `-O1`.
`-O1` runs a conservative post-generation peephole pass over the assembly
produced by either front end. The pass is shared, so C and C++ receive the same
target-level cleanup.

## Current `-O1` passes

0. **Trivial-function inlining (C++ `-O1`)**
   - Inlines free, static-member, and member functions whose body is a
     single `return` expression.
   - Parameters are bound to fresh frame slots holding the evaluated
     arguments; the expression is generated at the call site.
   - Recursive and class-result calls are not inlined.

1. **Front-end constant folding and dead branches**
   - Integer constant expressions are folded before emission in both C and C++.
   - `1 + 2 * 3` becomes a single `MOV R0, #7`.
   - Constant right operands use immediate ALU and `CMP` forms, avoiding a
     register materialization and left-operand spill.
   - Scalar local reads in both C and C++ use `LOAD reg, [R7 + n]`
     directly instead of a `LEA` followed by an indirect load.
   - Scalar local assignments and declarations store directly to
     `[R7 + n]`, avoiding a temporary address register.
   - Scalar C++ returns keep the result in registers when no local
     destructors or active catch require preserving it across cleanup.
   - Reads of scalar `const` locals with constant initializers use the
     immediate value directly in both C and C++.
   - C and C++ `if` and loop comparisons branch directly from `CMP`
     instead of materializing a 0/1 boolean and testing it again.
   - Ordered comparison values in both compilers use `SLT`/`SLTU` directly.
   - Equality values use `XOR` plus `SLT #1` instead of branches.
   - Conditions comparing two local scalars load both into `R0`/`R1`
     directly instead of using `PUSH`/`POP`.
   - Logical `!` uses `SLTU #1`, checking both halves of a 64-bit value.
   - C and C++ comparison values between two local scalars load both
     directly; equality with zero skips the redundant `XOR #0`.
   - C branches directly at `-O0` for both immediate and register right
     operands; `-O1` folds remaining boolean materializations.
   - `do`/`while` conditions branch directly to the loop head in both C and
     C++.
   - Ternary conditions branch directly on a comparison in both languages.
   - C and C++ arithmetic on two simple locals keeps the left operand in
     `R1`, avoiding a temporary frame slot.
   - Local `++`/`--` updates load and store the frame slot directly instead
     of computing its address first.
   - Local compound assignments (`+=`, `-=`, shifts, bitwise) load, combine,
     and store the frame slot directly.
   - C and C++ calls whose arguments are simple locals or constants load
     them directly into argument registers, without temporary slots.
   - C++ member calls keep `this` in `R4` and load simple arguments directly
     into `R1`–`R3`, avoiding temporary slots.
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

7. **Frame address arithmetic and folding**
   - Folds `LEA R, [R7 + n]` plus `ADD/SUB R, R, #off` into one adjusted
     `LEA`.
   - Folds `LEA R, [R7 + n]` followed by `LOAD Rd, [R]` into a direct
     `LOAD Rd, [R7 + n]`.
   - Folds `LEA R, [R7 + n]` followed by `STORE Rs, [R]` into a direct
     `STORE Rs, [R7 + n]`.
   - Removes the `LEA` only when its register is provably dead.

8. **Pointer load offsets**
   - Folds `ADD/SUB reg, reg, #off` followed by `LOAD reg, [reg]` into one
     offset load.
   - Skips the transformation if a conditional branch immediately follows.
   - Folds `ADD/SUB reg, reg, #off` plus `STORE value, [reg]` only when the
     register is overwritten immediately afterward.

9. **Zero frame adjustments**
   - Removes `ADJSP #0` and `ADJSP #-0` from functions with no locals.

10. **Comparison value folding**
   - Rewrites ordered comparisons materialized as `0`/`1` into `SLT`/`SLTU`
     plus an optional `XOR #1`.
   - Equality comparisons keep the branch form.

11. **Boolean branch folding**
   - Recognizes a comparison materialized to `0`/`1` and immediately tested
     again, and replaces the sequence with one inverse conditional branch.
   - Applies to C condition code and other front ends that materialize booleans.

12. **Unreachable instructions**
   - Removes non-label instructions after `RET`, unconditional `JMP`, or
     `HALT` until the next label or directive.

13. **Repeated frame loads**
   - Removes a repeated `LOAD` from the same frame slot into the same
     register when no store, call, unconditional jump, or register write
     intervenes.

14. **Symbol address folding**
   - Folds `LEA R, [symbol]` plus `LOAD R, [R]` into `LOAD R, [symbol]`.
   - Folds stores through a symbol address when the address register is
     immediately overwritten.

15. **Immediate add chains**
   - Combines adjacent `ADD`/`SUB reg, reg, #imm` pairs into one
     adjustment.
   - Skips the fold when a conditional branch immediately follows.

16. **Load copy folding**
   - Retargets `LOAD R, [mem]` into the destination of a following
     `MOV Rd, R` when `R` is immediately overwritten.

17. **CFG and liveness analysis**
   - Splits each function into basic blocks and computes register and
     frame-slot live-in/live-out sets.
   - Accounts for conditional and unconditional branches, calls, returns,
     and C++ exception barriers.

18. **Liveness-based dead stores and moves**
   - Removes frame stores whose slot is not live on any successor path.
   - Removes `MOV` destinations that are dead after the instruction.
   - Keeps stores in functions with exception landing paths.

19. **Straight-line copy coalescing**
   - Propagates a `MOV Rd, Rs` into block-local reads of `Rd` and removes
     the copy when `Rd` is not live out.
   - Stops at calls, source-register writes, and block boundaries.

20. **Leaf scalar register promotion**
   - Promotes eligible scalar frame slots in call-free, exception-free
     functions to `R12`-`R15`.
   - Replaces loads and stores with register moves and rewrites adjacent
     consumers, adding one entry load per promoted slot.
   - Requires at least one removable load and avoids slots whose address
     escapes.

21. **Loop comparison branch folding**
   - Folds `SLT`/`SLTU` (optionally with `XOR #1`) plus `CMP #0` and
     `JZ`/`JNZ` back into a direct `CMP` and conditional branch.
   - Also folds equality materialization (`XOR`, `SLTU #1`) plus a zero test.
   - This removes two instructions from common materialized loop conditions.

## Safety model

The pass is deliberately conservative:

- CFG-based passes may cross labels and ordinary branches, but not calls or
  C++ exception landing regions.
- Unknown memory writes clear local forwarding knowledge.
- Calls and returns count argument and return registers as reads.
- Leaf register promotion only runs in functions with no calls or exception
  landing paths.
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

1. Loop-invariant load hoisting for loops with calls outside the body.
2. Broader inlining for C and multi-statement C++ functions.
3. Tail-call optimization, dead-function removal, and nothrow call metadata.
