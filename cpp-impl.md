# KR32 C++ toolchain implementation plan (M9)

**Status:** approved and started 2026-11-02. Design approved in
[docs/cpp-plan.md](docs/cpp-plan.md); implementation runs in the `m9-cxx`
worktree/branch. Phase M9.1 (ABI, API, and test harness) is the current
subagent task.

**Design source of truth:** [docs/cpp-plan.md](docs/cpp-plan.md)
**Milestone checklist:** [implementation.md](implementation.md)
**Standalone driver:** `kr32-cxx` (`src/cxx/tool/main.cpp`)

## 1. Rules

- One source of design truth: `docs/cpp-plan.md`. This file tracks execution,
  status, gates, and discovered tasks; it does not invent ABI or language rules.
- Every phase ends with a commit, a green test gate, and a log entry in
  `implementation.md`.
- The M9 work uses a separate worktree/branch (`m9-cxx`) so UI work on `main`
  does not contend for the same files.
- The compiler module must not include SDL/UI/platform headers.
- Do not change the C ABI or existing C object behavior silently. ABI changes
  require `docs/abi.md` edits and regression tests.
- Unsupported language constructs must produce a clear diagnostic with a source
  location, not a crash or a host-compiler fallback.
- No host standard library or host C++ ABI compatibility is implied.
- Keep the standalone driver and library API free of filesystem assumptions;
  the driver reads/writes files, the library takes text and returns text/bytes.

## 2. Current baseline

Completed before this tracker:

- [x] M9 design plan drafted and reviewed: `docs/cpp-plan.md`.
- [x] Public API header: `include/kr32/cxx/cxx.h`.
- [x] Diagnostic-stub compiler implementation: `src/cxx/cxx.cpp`.
- [x] Standalone `kr32-cxx` driver: `src/cxx/tool/main.cpp`.
- [x] CMake target and `cxx_tool_help` CTest.
- [x] Front-end behavior is deliberately “not implemented” until phase 2.

Not started:

- [ ] ABI document extension and conformance tests.
- [ ] Lexer/parser/semantic analysis.
- [ ] Code generation, runtime, threads, matrices, debug integration.
- [ ] `m9-cxx` worktree/branch.
- [ ] Subagent assignment.

## 3. Milestone map

Status values: `not started`, `in progress`, `blocked`, `done`.

| ID | Scope | Gate | Status |
| --- | --- | --- | --- |
| M9.0 | Review this tracker, create `m9-cxx` worktree, freeze phase-1 files | tracker approved; clean worktree | done |
| M9.1 | ABI doc, API/driver polish, test harness | ABI tests + driver smoke green | in progress |
| M9.2 | Preprocessor, lexer, parser, declarations, diagnostics | parser unit/golden tests | not started |
| M9.3 | Functions, namespaces, references, overloads, name mangling | simple C++ links/runs | not started |
| M9.4 | Scalar `float`/`double`, FP calls, spills, varargs | FP ABI/varargs tests | not started |
| M9.5 | Classes, constructors/destructors, `this`, single inheritance | class demo runs; layout tests | not started |
| M9.6 | Virtual dispatch and RTTI | virtual demo; vtable/typeinfo tests | not started |
| M9.7 | Matrix library/intrinsics, tile allocation, transform stack | matrix/transform tests vs scalar reference | not started |
| M9.8 | Templates, COMDAT, static initialization | multi-TU template/static-init tests | not started |
| M9.9 | `new`/`delete`, exceptions, full cleanup | RAII/unwind tests | not started |
| M9.10 | Threads, TLS/TP, atomics, mutex/condvar, `std::thread` subset | two-core and TLS/atomic tests | not started |
| M9.11 | C++ debug info, GUI/DAP integration | C++ step/locals/members/this | not started |
| M9.12 | CLI integration, cross-platform gate, docs, tag `m9` | all gates on Windows/WSL/ARM64 | not started |

## 4. Phase tasks

### M9.0 — Worktree and integration boundary

- [x] Review and approve this tracker.
- [x] Create `m9-cxx` branch/worktree (for example
      `git worktree add ../KR32-m9-cxx -b m9-cxx`).
- [x] Confirm only `src/cxx/**`, `include/kr32/cxx/**`, `tests/cxx/**`, and
      `docs/` are touched by the C++ stream until integration.
- [x] Confirm the standalone executable name is `kr32-cxx`.
- [ ] Add a `KR32_BUILD_CXX_TOOL` option if the team wants the driver optional;
      otherwise keep it always built.
- [x] Record subagent ownership and handoff point.

**Gate:** tracker approved; worktree clean; no UI/shared-file changes pending.

### M9.1 — ABI, API, and test harness

- [ ] Extend `docs/abi.md` with the C++ ABI:
  - `float`/`double` sizes, alignment, aggregate layout
  - FP argument/return registers and callee-saved rules
  - varargs stack layout and `va_list`
  - `this`, vtable, RTTI, constructor/destructor variants
  - Itanium-like mangling grammar and examples
  - exception cleanup/landing-pad layout
  - thread pointer `R11`, TCB/TLS layout, per-thread FPR/TSP context
  - tile registers and matrix calling convention
- [ ] Add ABI layout/mangling unit tests (host-side data structure checks).
- [ ] Flesh out `kr32::cxx::Options`/`Result` only as needed; keep the API
      stable.
- [ ] Add driver tests for help, missing input, bad `--emit`, and `-o`.
- [ ] Make `--emit obj` contract explicit (container bytes vs error until the
      object emitter exists).
- [ ] Add `M9` CMake option documentation and install rules.

**Gate:** ABI tests, driver tests, and existing suite green on Windows/WSL.

### M9.2 — Preprocessor, lexer, parser, diagnostics

- [ ] Reuse/extend the C preprocessor; add C++ tokens (`::`, `->*`, templates,
      references, `new`, `delete`, `try`, `catch`, `throw`, lambdas).
- [ ] Build a C++ declaration/expression AST separate from the C parser.
- [ ] Track source locations for diagnostics and debug line records.
- [ ] Parse translation units, namespaces, classes, functions, declarations.
- [ ] Recover from common errors and emit sorted diagnostics.
- [ ] Add unit tests for tokens, precedence, declarations, and errors.
- [ ] Add a parser-only mode or test hook that does not emit code.

**Gate:** no crashes on malformed input; parser/AST tests pass.

### M9.3 — Functions, namespaces, references, overloads

- [ ] Build scopes, name lookup, namespace qualification, `using`.
- [ ] Function types, parameter passing, return types, references.
- [ ] Overload resolution for the supported subset.
- [ ] Itanium-like mangling for free functions and namespaces.
- [ ] Lower functions and calls to the IR/assembly path.
- [ ] Reuse the C frame shape initially, with 16-byte public call alignment.
- [ ] Integration tests: multiple functions, references, overloads, multi-TU.

**Gate:** simple C++ programs compile, link, and run through the existing tools.

### M9.4 — Scalar float and double

- [ ] Lex/parse float and double literals; implement round-to-nearest literal
      conversion.
- [ ] Type rules, usual arithmetic conversions, comparisons, casts.
- [ ] FPR allocation and spills via `FLOAD`/`FSTORE`.
- [ ] Lower arithmetic to `.S`/`.D` instructions.
- [ ] Implement `FCVT` conversions and NaN/infinity behavior.
- [ ] FP arguments `F0-F7`, returns `F0/F1`, callee-saved `F8-F15`.
- [ ] Stack-passed varargs with promoted doubles and `va_list`.
- [ ] `long double` alias for `double`.
- [ ] Tests: ABI calls, structs containing FP, spills, conversions, varargs.

**Gate:** FP ABI/varargs tests plus existing suite green.

### M9.5 — Classes, constructors/destructors, single inheritance

- [ ] Class layout, access control, member offsets, empty classes.
- [ ] Constructors, destructors, copy construction/assignment, `this`.
- [ ] Member functions and mangling; static members.
- [ ] Single inheritance, base layout, constructor/destructor order.
- [ ] Aggregate copy/return rules for the supported subset.
- [ ] Debug records for members and `this`.
- [ ] Tests: object lifetime, member access, inheritance, copy.

**Gate:** class/inheritance demo runs; layout/debug tests pass.

### M9.6 — Virtual dispatch and RTTI

- [ ] Vtable layout, virtual destructor slots, vptr placement.
- [ ] Virtual calls and overrides; abstract classes/pure virtuals.
- [ ] `type_info`, `typeid`, `dynamic_cast` for supported hierarchies.
- [ ] COMDAT/weak vtables and typeinfo.
- [ ] Tests: virtual calls, dynamic_cast, destructor dispatch.

**Gate:** virtual/RTTI tests link and run; no duplicate-symbol failures.

### M9.7 — Matrix and transform-stack codegen

- [ ] Add `kr32/matrix.hpp`: `vec4`, `mat4`, `matrix_mul`,
      `matrix_mul_add`, `transform`, `transpose`, `dot`, `push`, `pop`, `drop`.
- [ ] Register/define compiler intrinsics for `MMUL`, `MMAC`, `MVEC`,
      `MTRANS`, `VDOT`, `MPUSH`, `MPOP`, `MDROP`.
- [ ] Tile register allocator and spills; caller-saved `T0-T15`.
- [ ] Explicit per-thread transform-stack discipline and TSP context handling.
- [ ] Matrix constants via `MLOAD`/`VLOAD`; memory layout/alignment 16.
- [ ] Optimization pass: multiply/accumulate fusion, nested transform folding,
      transpose elimination.
- [ ] Tests: compare against scalar reference matrices; aliasing; stack push/pop.

**Gate:** matrix/transform tests pass on Windows/WSL.

### M9.8 — Templates and static initialization

- [ ] Parse template declarations and arguments for the supported subset.
- [ ] Instantiate function/class templates and deduplicate instances.
- [ ] Emit template symbols as weak/COMDAT; linker merging.
- [ ] Static initialization and destruction; `.init_array` equivalent.
- [ ] Thread-safe local statics (`__cxa_guard_*`) once threads exist.
- [ ] Tests: header-only templates across multiple TUs; static init order.

**Gate:** multi-TU template/static-init tests link and run.

### M9.9 — `new`/`delete`, exceptions, cleanup

- [ ] `operator new/delete`, array forms, allocation failure behavior.
- [ ] Constructor/destructor array helpers.
- [ ] Exception object layout, throw/catch/rethrow.
- [ ] Table-driven cleanup and full destructor unwinding.
- [ ] `__cxa_atexit`-style static destruction.
- [ ] Tests: RAII, nested try/catch, destructor unwinding, rethrow.

**Gate:** exception/cleanup tests pass with no leaks in the guest allocator.

### M9.10 — Threads, TLS, atomics, synchronization

- [ ] Define/implement host thread services: create/join/yield/exit, futex.
- [ ] TCB, `R11` thread pointer, TLS block, per-thread stack.
- [ ] Per-thread FPR and TSP context save/restore on switch/migration.
- [ ] `std::atomic` on LL/SC, initially sequentially consistent.
- [ ] `std::mutex`, `std::condition_variable`, `std::thread`,
      `std::this_thread` subset.
- [ ] Thread-safe static initialization.
- [ ] Tests: two-core counter, mutex, condvar, atomic, TLS, static-init race,
      deterministic scheduling.

**Gate:** multi-core/thread tests pass on the cluster; debug view shows threads.

### M9.11 — C++ debug information and GUI/DAP

- [ ] Extend debug records for namespaces, classes/members, `this`,
      template instances, lexical scopes, and function kinds.
- [ ] Emit source lines for C++ constructs.
- [ ] Update `DebugSession` variable/type/stack APIs.
- [ ] GUI: source lines, locals, class members, `this`, call stack, threads.
- [ ] DAP: variables/scopes/threads/stack for C++.
- [ ] Tests: breakpoint/step/locals/members on a C++ program.

**Gate:** GUI step-through of a `.cpp` file with lines, locals, members, `this`.

### M9.12 — CLI integration, cross-platform, tag

- [ ] Wire `kr32 cxx` and `kr32 build file.cpp` to `kr32::cxx::compile`.
- [ ] Auto-link the C++ runtime and matrix/FP support libraries.
- [ ] Add shell help and diagnostics.
- [ ] Update README, `docs/abi.md`, `docs/debug.md`, `docs/host.md`,
      `docs/ui.md` where needed.
- [ ] Run Windows MSVC Debug/Release/Ninja, WSL GCC/Clang/ASan, and ARM64.
- [ ] Tag `m9` only after every gate passes; update `implementation.md`.

**Gate:** full cross-platform M9 gate; tag.

## 5. Test matrix

| Area | Unit | Integration | Golden/ABI | Notes |
| --- | --- | --- | --- | --- |
| Lexer/parser | yes | — | golden diagnostics | malformed input |
| Mangling/ABI | yes | link/run | symbol fixtures | Itanium-like |
| Functions/overloads | yes | link/run | — | multi-TU |
| float/double | yes | run | literal/rounding | spills, NaN/inf |
| Varargs | yes | run | `va_list` | promoted doubles |
| Classes | yes | run | layout | members/this |
| Virtual/RTTI | yes | run | vtable | dynamic_cast |
| Matrices | yes | run | scalar reference | tile aliasing |
| Transform stack | yes | run | push/pop/drop | per-thread TSP |
| Templates | yes | multi-TU | COMDAT | weak merge |
| Static init | yes | run | order | guards |
| Exceptions | yes | run | unwind tables | destructor cleanup |
| Threads | yes | cluster | deterministic | TLS/atomics |
| Debug | yes | GUI/DAP | line/variable | members/this |

## 6. Known prerequisites / risks

- COMDAT/weak section groups and linker merging must be audited early.
- `.init_array`/`.fini_array` or an equivalent static-init mechanism is needed.
- Debug format extension is required before the GUI C++ gate.
- Thread creation has no existing guest syscall; host services must be designed
  with `docs/host.md`.
- `R11` TP is a C++ ABI reservation; ensure C codegen never allocates it.
- Transform stack is per thread; thread migration must restore TSP.
- No formatted FP I/O in the first gate; tests use integer/FP values and
  explicit formatting helpers if needed.
- Full ISO C++ and full STL remain out of scope.

## 7. Progress log

Newest at the bottom. Keep timestamps in `America/Los_Angeles`.

- **2026-11-02 09:30 -08:00** — `docs/cpp-plan.md` approved. This implementation
  tracker created; compiler implementation and `m9-cxx` worktree not started,
  pending review of this file.
- **2026-11-02 09:35 -08:00** — Tracker approved. `m9-cxx` worktree/branch
  created; phase M9.1 (ABI, API, and test harness) assigned to the first
  subagent. Phase M9.0 remains done; M9.1 is in progress.

## 8. Approval

- [x] Review this tracker.
- [x] Approve creation of the `m9-cxx` worktree/branch.
- [x] Assign the phase-1 subagent.
