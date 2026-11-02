# KR32 C++ toolchain implementation plan (M9)

**Status:** approved and started 2026-11-02. Design approved in
[docs/cpp-plan.md](docs/cpp-plan.md); implementation runs in the `m9-cxx`
worktree/branch. Phases M9.1 (ABI/API/test harness), M9.2 (preprocessor,
lexer, parser, AST, diagnostics), M9.3 (scopes, name lookup, overloads,
mangling, integer code generation), M9.4 (scalar `float`/`double`, FP calls,
conversions, and stack varargs), M9.5 (classes, constructors/destructors,
single inheritance), M9.6 (virtual dispatch and RTTI), M9.7 (matrix and
transform-stack codegen), M9.8 (function templates, weak symbols, and static
initialization), and M9.9 (`new`/`delete`, exceptions, cleanup) are complete;
phase M9.10 (threads, TLS, atomics, synchronization) is in progress with
stage-1 host thread/futex services, stage-2 TCB/TLS/context, and stage-3
compiler `thread_local`/atomics plus the synchronization header surface done.

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

Completed in M9.1:

- [x] C++ ABI revision 1 documented in `docs/abi.md`, with machine-checkable
      constants in `include/kr32/cxx/abi.h`.
- [x] Host-side ABI layout/constant tests and API smoke tests under `tests/cxx`,
      wired into `kr32_tests` and linked against `kr32_cxx`.
- [x] Driver error-path tests (missing input, bad `--emit`, missing `-o` value).
- [x] `--emit obj` output contract made explicit; `KR32_BUILD_CXX_TOOL` option
      documented and install rules conditioned on it.

Completed in M9.2:

- [x] C++ preprocessor subset (`src/cxx/preprocessor.*`): comments, line
      splices, object/function macros, `#if`/`#ifdef`/`#elif`/`#else`/`#endif`,
      built-in macros, and explicit diagnostics for unavailable headers.
- [x] Lexer (`src/cxx/lexer.*`): C++ keywords, identifiers, integer/float/
      character/string literals, raw strings, comments, and the full punctuator
      set with source locations.
- [x] AST (`src/cxx/ast.*`) for declarations, types, expressions, statements,
      classes, namespaces, templates, lambdas, and initializer lists.
- [x] Recursive-descent parser (`src/cxx/parser.*`) with error recovery and
      sorted diagnostics; `compile()` now runs preprocess → lex → parse and
      reports the missing backend only after a successful parse.
- [x] Host parser tests under `tests/cxx/test_cxx_parser.cpp`, including a
      malformed-input corpus.

Completed in M9.3:

- [x] Symbol collection, namespace scopes, name lookup, `using` declarations and
      `using namespace`, and overload resolution for integer/pointer/reference
      parameters (`src/cxx/codegen.*`).
- [x] Itanium-like mangling for free functions, namespaces, and globals, with
      unit tests for the documented examples.
- [x] Direct assembly lowering for functions, calls, locals, globals, arrays,
      pointers, references, integer arithmetic/comparisons, and control flow;
      `compile()` now returns assembly and `ok`.
- [x] C frame shape with 16-byte public call alignment.
- [x] Codegen tests: functions, recursion, namespaces/overloads, references,
      arrays/loops, globals, `extern "C"`, cross-TU linking, and unsupported
      class diagnostics.

Completed in M9.4:

- [x] Ported the main-branch `.float`/`.double` data directives and
      object-assembler FP instruction support (`FLOAD`/`FSTORE`, FP arithmetic,
      compare, and `FCVT`), with assembler tests.
- [x] Scalar `float`/`double` literals, usual arithmetic conversions, mixed
      int/FP expressions, comparisons, casts, and `FCVT` conversions.
- [x] FPR staging and spills through the frame; FP arguments in `F0..F7`,
      results in `F0`, and no use of callee-saved `F8..F15` yet.
- [x] `long double` follows the `double` alias path and mangles as `d`.
- [x] Stack-passed variadic arguments with `float` promoted to `double`, plus
      `va_list`/`__builtin_va_start`/`__builtin_va_arg`/`__builtin_va_end`.
- [x] FP codegen tests: calls, mixed argument banks, local spills, globals,
      conversions, references to `double`, `float` returns, and integer/double
      varargs.

Completed in M9.5:

- [x] Class registry and layout: base at offset zero, member natural alignment,
      empty-class size/alignment, FP members, nested namespaces.
- [x] `this` in `R0`, unqualified member resolution, `obj.member`,
      `ptr->member`, member calls, and static member calls.
- [x] Constructors (default and argument forms, member initializer lists),
      destructors at scope exit and return paths, object construction order,
      and memberwise copy construction/assignment.
- [x] Single inheritance with base constructor/destructor order.
- [x] Static data members and static member functions.
- [x] Tests: layout/member access, constructors/`this`, destructors,
      inheritance, pointer/FP members, static members, and copy.

Completed in M9.6:

- [x] Vtable address-point layout, vptr initialization in constructors and
      destructors, inherited/overridden slot assignment, and virtual destructor
      slots.
- [x] Virtual member calls through pointers/references, abstract-class
      rejection, and `__cxa_pure_virtual` stubs.
- [x] `_ZTI`/`_ZTS` typeinfo emission, `typeid` expressions, and pointer
      `dynamic_cast` using the single-inheritance typeinfo chain.
- [x] Tests: virtual override dispatch, successful/failed `dynamic_cast`,
      `typeid` comparison, and abstract-class diagnostics.

Completed in M9.7:

- [x] Ported tile/transform-stack instruction encoding into the object
      assembler (`MMUL`/`MMAC`/`MVEC`/`MTRANS`/`VDOT`/`MLOAD`/`MSTORE`/
      `MPUSH`/`MPOP`/`MDROP`), with encoding tests.
- [x] `include/kr32/matrix.hpp` plus freestanding preprocessor injection for
      `kr32::vec4`/`kr32::mat4` and the matrix/transform functions.
- [x] Lowered matrix/transform calls to tile instructions, including
      `MVEC`+`MTRANS` handling for the column-vector result convention.
- [x] Tests: matrix multiply, multiply-add, transpose, transform, dot, aliasing,
      and transform-stack push/pop/drop.

Completed in M9.8:

- [x] Function template parsing (explicit arguments retained), instantiation by
      explicit arguments or deduction, stable instantiated declarations, and
      deduplication by mangled symbol.
- [x] Weak symbol emission for template instantiations and inline functions,
      with `.weak` object-assembler support; multi-TU duplicate definitions link
      with first-definition resolution.
- [x] Non-constant global initialization before `main` in declaration order.
- [x] Tests: explicit/deduced function templates, weak template/inline symbols
      across TUs, and static initialization order.

Completed in M9.9:

- [x] `new`/`delete` for scalars, objects, and scalar arrays, including
      constructor/destructor calls.
- [x] R7 frame-chain unwinder with `.cpp_except` call-site ranges, cleanup
      landing pads, restored `SP`/`R7`, and `_Unwind_Resume`.
- [x] Static construction and reverse-order destruction for global objects,
      `__cxa_atexit` registry, and reverse destruction.
- [x] Class exceptions with refcounted `__cxa_allocate_exception`/
      `begin_catch`/`end_catch`, `.cpp_except` LSDA catch tables, and
      `__kr32_personality_v0` catch selection.
- [x] Constructor-completion cleanup for class arrays whose element constructor
      throws.

In progress:

- [x] Host thread/futex services in `vm::ClusterHost` over `core::Cluster`
      (M9.10 stage 1): create/join/yield/exit, futex wait/wake, syscall vectors,
      and tests.
- [x] Guest TCB at `R11`, per-thread stacks/TLS, guest join, and FPR/TSP
      context save-restore (M9.10 stage 2).
- [x] Compiler `thread_local`, LL/SC atomics, and thread/mutex/condvar header
      surface (M9.10 stage 3).
- [x] Compiler `thread_local` (R11-relative TLS), LL/SC atomics, and
      thread/mutex/condvar header surface (M9.10 stage 3).

Not started:

- [ ] Guest TCB/TLS, FPR/TSP context save/restore, compiler `thread_local`/
      atomics, mutex/condvar/thread headers (M9.10 remaining), then debug and
      CLI integration (M9.11-M9.12). Class templates, COMDAT section groups,
      static destruction, guarded local statics, a full tile allocator/spill
      pass, matrix optimization/fusion, and by-value class parameters/returns
      remain deferred.

## 3. Milestone map

Status values: `not started`, `in progress`, `blocked`, `done`.

| ID | Scope | Gate | Status |
| --- | --- | --- | --- |
| M9.0 | Review this tracker, create `m9-cxx` worktree, freeze phase-1 files | tracker approved; clean worktree | done |
| M9.1 | ABI doc, API/driver polish, test harness | ABI tests + driver smoke green | done |
| M9.2 | Preprocessor, lexer, parser, declarations, diagnostics | parser unit/golden tests | done |
| M9.3 | Functions, namespaces, references, overloads, name mangling | simple C++ links/runs | done |
| M9.4 | Scalar `float`/`double`, FP calls, spills, varargs | FP ABI/varargs tests | done |
| M9.5 | Classes, constructors/destructors, `this`, single inheritance | class demo runs; layout tests | done |
| M9.6 | Virtual dispatch and RTTI | virtual demo; vtable/typeinfo tests | done |
| M9.7 | Matrix library/intrinsics, tile allocation, transform stack | matrix/transform tests vs scalar reference | done |
| M9.8 | Templates, COMDAT, static initialization | multi-TU template/static-init tests | done |
| M9.9 | `new`/`delete`, exceptions, full cleanup | RAII/unwind tests | done |
| M9.10 | Threads, TLS/TP, atomics, mutex/condvar, `std::thread` subset | two-core and TLS/atomic tests | in progress |
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
- [x] Add a `KR32_BUILD_CXX_TOOL` option if the team wants the driver optional;
      otherwise keep it always built. (Added in M9.1 as a default-`ON` option;
      the `kr32_cxx` library is always built.)
- [x] Record subagent ownership and handoff point.

**Gate:** tracker approved; worktree clean; no UI/shared-file changes pending.

### M9.1 — ABI, API, and test harness

- [x] Extend `docs/abi.md` with the C++ ABI:
  - `float`/`double` sizes, alignment, aggregate layout
  - FP argument/return registers and callee-saved rules
  - varargs stack layout and `va_list`
  - `this`, vtable, RTTI, constructor/destructor variants
  - Itanium-like mangling grammar and examples
  - exception cleanup/landing-pad layout
  - thread pointer `R11`, TCB/TLS layout, per-thread FPR/TSP context
  - tile registers and matrix calling convention
- [x] Add ABI layout/constant unit tests (host-side data structure checks) and
      mirror the frozen constants in `include/kr32/cxx/abi.h`. Mangling fixture
      tests land with the mangler in M9.3.
- [x] Flesh out `kr32::cxx::Options`/`Result` only as needed; keep the API
      stable. (Documented the output contract; the shapes are unchanged.)
- [x] Add driver tests for help, missing input, bad `--emit`, and `-o`.
- [x] Make `--emit obj` contract explicit (container bytes vs error until the
      object emitter exists).
- [x] Add `M9` CMake option documentation and install rules.

**Gate:** ABI tests, driver tests, and existing suite green on Windows/WSL.
**Status:** done 2026-11-02. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 115 test cases and 4473 assertions. The
ABI review decisions were accepted 2026-11-02; all recommended choices are in
`docs/abi.md`, and the exact TCB offsets remain deferred to M9.10.

### M9.2 — Preprocessor, lexer, parser, diagnostics

- [x] Reuse/extend the C preprocessor rules; add C++ tokens (`::`, `->*`,
      templates, references, `new`, `delete`, `try`, `catch`, `throw`, lambdas).
- [x] Build a C++ declaration/expression AST separate from the C parser.
- [x] Track source locations for diagnostics and debug line records.
- [x] Parse translation units, namespaces, classes, functions, declarations.
- [x] Recover from common errors and emit sorted diagnostics.
- [x] Add unit tests for tokens, precedence, declarations, and errors.
- [x] Add a parser-only mode or test hook that does not emit code
      (`kr32::cxx::parse`/`parse_tokens`, used by `tests/cxx`).

**Gate:** no crashes on malformed input; parser/AST tests pass.
**Status:** done 2026-11-02. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 131 test cases and 4617 assertions,
including the malformed-input corpus. M9.3 is next.

### M9.3 — Functions, namespaces, references, overloads

- [x] Build scopes, name lookup, namespace qualification, `using`.
- [x] Function types, parameter passing, return types, references.
- [x] Overload resolution for the supported subset.
- [x] Itanium-like mangling for free functions and namespaces.
- [x] Lower functions and calls to the IR/assembly path. (Direct assembly
      lowering for now; a shared optimization IR remains a later refactor.)
- [x] Reuse the C frame shape initially, with 16-byte public call alignment.
- [x] Integration tests: multiple functions, references, overloads, multi-TU.

**Gate:** simple C++ programs compile, link, and run through the existing tools.
**Status:** done 2026-11-02. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 142 test cases and 4646 assertions.
M9.4 (scalar `float`/`double`) is next.

### M9.4 — Scalar float and double

- [x] Lex/parse float and double literals; implement round-to-nearest literal
      conversion.
- [x] Type rules, usual arithmetic conversions, comparisons, casts.
- [x] FPR staging and spills via `FLOAD`/`FSTORE`.
- [x] Lower arithmetic to `.S`/`.D` instructions.
- [x] Implement `FCVT` conversions and NaN/infinity behavior. (ISA semantics;
      NaN comparison and zero-division NaN are covered by a codegen test.)
- [x] FP arguments `F0-F7`, returns `F0` (`F1` reserved), no callee-saved
      `F8-F15` allocation yet.
- [x] Stack-passed varargs with promoted doubles and `va_list`.
- [x] `long double` alias for `double` (including mangling as `d`).
- [x] Tests: ABI calls, spills, conversions, varargs, `long double`, NaN.
- [ ] Structs containing FP fields move to M9.5 with aggregate layout and member
      access.

**Gate:** FP ABI/varargs tests plus existing suite green.
**Status:** done 2026-11-02. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 153 test cases and 4689 assertions.
M9.5 (classes, constructors/destructors, single inheritance) is next.

### M9.5 — Classes, constructors/destructors, single inheritance

- [x] Class layout, member offsets, empty classes. (Access is recorded by the
      parser; enforcement is deferred.)
- [x] Constructors, destructors, `this`, and memberwise copy
      construction/assignment.
- [x] Member functions and mangling; static data members and static member
      functions.
- [x] Single inheritance, base layout at offset zero, constructor/destructor
      order.
- [x] Aggregate memberwise copy/assignment for locals; by-value aggregate
      parameters/returns remain deferred to the ABI aggregate work.
- [ ] Debug records for members and `this` move to M9.11 with the debug format.
- [x] Tests: object lifetime, member access, inheritance, FP members, static
      members, and copy.

**Gate:** class/inheritance demo runs; layout/debug tests pass.
**Status:** done 2026-11-02 for the codegen gate; debug member/`this` records are
tracked in M9.11. Windows MSVC Debug and WSL GCC headless both pass 21/21 CTest
tests; `kr32_tests` reports 159 test cases and 4702 assertions. M9.6 (virtual
dispatch/RTTI) is next.

### M9.6 — Virtual dispatch and RTTI

- [x] Vtable layout, virtual destructor slots, vptr placement. (Address point at
      `_ZTV+8`, typeinfo at `vptr-8`, offset-to-top at `vptr-4`; both virtual
      destructor slots currently use the D1 symbol until `new`/`delete` needs
      D0.)
- [x] Virtual calls and overrides; abstract classes/pure virtuals.
- [x] `type_info`, `typeid`, and pointer `dynamic_cast` for single inheritance;
      reference `dynamic_cast` remains tied to exception support in M9.9.
- [ ] COMDAT/weak vtables and typeinfo move to the M9.8 COMDAT work.
- [x] Tests: virtual calls, failed/successful `dynamic_cast`, `typeid`
      comparison, and abstract-class rejection.

**Gate:** virtual/RTTI tests link and run; no duplicate-symbol failures.
**Status:** done 2026-11-02 for the single-TU codegen gate; weak/COMDAT vtable
merging is tracked in M9.8. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 162 test cases and 4711 assertions.
M9.7 (matrix/transform codegen) is next.

### M9.7 — Matrix and transform-stack codegen

- [x] Add `include/kr32/matrix.hpp`: `vec4`, `mat4`, `matrix_mul`,
      `matrix_mul_add`, `transform`, `transpose`, `dot`, `push`, `pop`, `drop`;
      the freestanding preprocessor injects the same declarations.
- [x] Lower the library calls to `MMUL`, `MMAC`, `MVEC`, `MTRANS`, `VDOT`,
      `MPUSH`, `MPOP`, and `MDROP`; the object assembler now encodes all tile
      and transform-stack instructions.
- [x] Tile scratch discipline uses caller-saved `T0-T3`; a full tile register
      allocator and spill pass remain deferred.
- [x] Explicit per-thread transform-stack discipline and TSP handling; the VM
      supplies TSP and native thread context restore is M9.10.
- [x] Matrix/vector memory layout with 16-byte alignment and `MLOAD`/`VLOAD`
      constants via runtime loads.
- [ ] Optimization pass: multiply/accumulate fusion, nested transform folding,
      transpose elimination (correctness-first lowering is in place).
- [x] Tests: tile object encodings, matrix multiply/add/transpose, transform,
      dot, aliasing, and transform-stack push/pop/drop.

**Gate:** matrix/transform tests pass on Windows/WSL.
**Status:** done 2026-11-02. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 165 test cases and 4735 assertions.
M9.8 (templates/static initialization) is next.

### M9.8 — Templates and static initialization

- [x] Parse template declarations and explicit template arguments for the
      supported function-template subset.
- [x] Instantiate function templates with explicit arguments and argument
      deduction, deduplicate by mangled symbol, and keep a stable declaration
      store for instantiated bodies. Class templates are deferred with a clear
      diagnostic.
- [x] Emit template instantiations and inline functions as weak symbols, and
      let the linker keep the first definition; COMDAT section-group merging is
      deferred.
- [x] Static initialization for non-constant globals before `main`, in
      declaration order; `.init_array` section emission, static destruction,
      and `__cxa_guard_*` local statics remain deferred.
- [ ] Thread-safe local statics (`__cxa_guard_*`) once threads exist.
- [x] Tests: explicit/deduced function templates, weak template/inline symbols
      across TUs, and static initialization order.

**Gate:** multi-TU template/static-init tests link and run.
**Status:** done 2026-11-02 for the supported function-template/weak/static-init
scope; class templates, COMDAT groups, static destruction, and guarded local
statics are deferred and documented above. Windows MSVC Debug and WSL GCC
headless both pass 21/21 CTest tests; `kr32_tests` reports 169 test cases and
4745 assertions. M9.9 (`new`/`delete` and exceptions) is next.

### M9.9 — `new`/`delete`, exceptions, cleanup

- [x] `new`/`delete` lowering to `malloc`/`free` for scalars, objects, and
      scalar arrays, including constructor and destructor calls. Allocation
      failure behavior and class-array constructor/destructor helpers remain
      deferred.
- [x] Constructor/destructor array helpers: `new[]` stores a count cookie,
      default-constructs each class element, and `delete[]` destroys elements in
      order before freeing the cookie base.
- [x] Scalar/pointer/class exception propagation, catch by type/value/
      reference, catch-all, nested try/catch, `throw;`, cross-function
      propagation, and destructor cleanup using an R7 frame-chain unwinder.
- [x] `.cpp_except` function descriptors and call-site ranges, cleanup landing
      pads, restored `SP`/`R7`, `_Unwind_Resume`, and
      `__kr32_personality_v0` table-driven catch selection.
- [x] Class exception header allocation, refcounted
      `__cxa_begin_catch`/`__cxa_end_catch` destruction, `__cxa_rethrow`, and
      `__cxa_allocate_exception`.
- [x] Constructor-completion cleanup for class arrays and conditional scopes;
      a throwing element constructor destroys constructed elements and frees
      the cookie before unwinding.
- [x] `__cxa_atexit` registry with fixed-capacity function/argument tables and
      reverse-order invocation from `__kr32_cxx_fini`, plus reverse-order static
      object destruction.
- [x] Tests: scalar/object/array `new`, `delete`, destructor invocation, class
      arrays, throwing array constructors, nested/cross-function try/catch,
      rethrow, multi-frame RAII cleanup, class exceptions, refcounted
      destruction, ABI metadata, and static destruction order.

**Gate:** exception/cleanup tests pass with no leaks in the guest allocator.
**Status:** done 2026-11-02. Windows MSVC Debug and WSL GCC headless both pass
21/21 CTest tests; `kr32_tests` reports 178 test cases and 4802 assertions.
M9.10 (threads, TLS, atomics, synchronization) is next.

### M9.10 — Threads, TLS, atomics, synchronization

- [x] Host thread/futex services in `vm::ClusterHost` over `core::Cluster`:
      create/join/yield/exit, futex wait/wake, one thread per core, and
      `thread_create`/`yield`/`exit`/`futex`/`self` syscall vectors. Guest
      blocking `thread_join` remains stage 2.
- [x] TCB at `R11`, frozen offsets, static TLS block, per-thread stacks, and
      guest `thread_join`/`yield`/`exit` servicing.
- [x] Per-thread FPR and TSP context save/restore (GPRs, SP, PC, FLAGS, all 16
      FPRs, TSP), with a 128-byte FPR save area addressed from the TCB.
- [x] `std::atomic`-class LL/SC lowering for integer load/store/fetch-add/
      compare-exchange, with `.hpp` header surface and object-assembler LL/SC/
      FENCE support.
- [x] `kr32::thread`/`mutex`/`condition_variable`/`atomic_int` header surface
      over the thread/futex syscalls; the mutex lock/unlock path is tested
      single-threaded.
- [ ] Thread-safe static initialization (`__cxa_guard_*`).
- [x] Tests: two-core LL/SC counter, `thread_local` R11 access, atomic
      builtins, header-surface mutex/atomic, TCB/context, and thread join.

**Gate:** multi-core/thread tests pass on the cluster; debug view shows threads.
**Status:** in progress 2026-11-02. Stages 1-3 are implemented: host
thread/futex services, guest TCB/TLS/context, compiler `thread_local`/LL/SC
atomics, and the thread/mutex/condvar header surface. Remaining: thread-safe
function-local static guards, initialized TLS images, multi-thread
mutex/condvar tests, and the full `std::thread` subset. Windows MSVC Debug and
WSL GCC headless both pass 21/21 CTest tests; `kr32_tests` reports 191 test
cases and 4917 assertions.

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
- **2026-11-02 09:44 -08:00** — Completed M9.1. Extended `docs/abi.md` to C++
  ABI revision 1 (FP layout/registers, varargs, `this`/mangling, vtable/RTTI,
  exceptions/cleanup, TP/TCB/TLS, tiles/transform stack, weak/COMDAT, static
  initialization) and added `include/kr32/cxx/abi.h`. Added host ABI/API tests
  under `tests/cxx`, driver error-path tests, the `--emit obj` output contract,
  and the `KR32_BUILD_CXX_TOOL` option/install rules. Windows MSVC Debug and WSL
  GCC headless both pass 21/21 CTest tests; `kr32_tests` reports 115 test cases
  and 4473 assertions. M9.1 is done; M9.2 (front end) is next.
- **2026-11-02 10:50 -08:00** — Accepted all M9.1 ABI review recommendations:
  `this` in R0, void ctor/dtor returns with C3 reserved, uniform 8-byte vararg
  slots, the 8/16-byte aggregate rule, the address-point vtable/RTTI layout,
  `.cpp_except` + `__kr32_personality_v0` landing pads, direction-only TCB
  offsets (exact offsets deferred to M9.10), EBO disabled, `long double` as a
  `double` alias, the bare-pointer `va_list`, and `KR32_BUILD_CXX_TOOL` default
  `ON`. No ABI contract change; `docs/abi.md` now records the approved review
  decisions. M9.1 remains done.
- **2026-11-02 11:10 -08:00** — Completed M9.2. Added the C++ preprocessor,
  lexer, AST, and recursive-descent parser under `src/cxx` (`preprocessor.*`,
  `lexer.*`, `ast.*`, `parser.*`), wired `compile()` through preprocess → lex →
  parse, and added host parser tests including a malformed-input corpus.
  Windows MSVC Debug and WSL GCC headless both pass 21/21 CTest tests;
  `kr32_tests` reports 131 test cases and 4617 assertions. M9.3 (semantic
  analysis, mangling, integer code generation) is next.
- **2026-11-02 11:25 -08:00** — Completed M9.3. Added symbol collection, namespace
  scopes, `using`/`using namespace`, overload resolution, Itanium-like mangling,
  and direct assembly lowering for functions, calls, locals, globals, arrays,
  pointers, references, integer operations, and control flow. `compile()` now
  returns assembly on success. Added codegen tests for recursion, namespaces and
  overloads, references, arrays, globals, `extern "C"`, cross-TU linking, and
  unsupported-class diagnostics. Windows/WSL both pass 21/21 CTest tests;
  `kr32_tests` reports 142 test cases and 4646 assertions. M9.4 (scalar
  `float`/`double`) is next; the main-branch `.float`/`.double` assembler
  directives and FP instruction support are available for it.
- **2026-11-02 11:32 -08:00** — Completed M9.4. Ported the main-branch
  object-assembler FP support (`.float`/`.double`, `FLOAD`/`FSTORE`, FP
  arithmetic/compare, `FCVT`) and added scalar `float`/`double` codegen:
  literals, mixed arithmetic/conversions, FP arguments in `F0-F7`, results in
  `F0`, frame spills, FP globals, references to `double`, `long double` as a
  `double` alias, NaN comparisons, and stack-passed variadic arguments with
  promoted doubles plus `va_list` builtins. Added assembler and codegen tests.
  Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 153 test cases
  and 4689 assertions. M9.5 (classes, constructors/destructors, single
  inheritance) is next; FP struct fields follow the M9.5 aggregate layout.
- **2026-11-02 12:15 -08:00** — Completed M9.5. Added class registry/layout
  (base at offset zero, natural member alignment, empty classes, FP members),
  `this` in `R0`, member access and calls, constructors with member initializer
  lists, destructors at scope exit and return paths, single inheritance with
  base constructor/destructor order, static members, and memberwise copy
  construction/assignment. Added class codegen tests. Windows/WSL both pass
  21/21 CTest tests; `kr32_tests` reports 159 test cases and 4702 assertions.
  M9.6 (virtual dispatch and RTTI) is next; by-value class parameters/returns
  and debug member/`this` records remain deferred.
- **2026-11-02 12:20 -08:00** — Completed M9.6. Added vtable address-point
  layout, vptr installation in constructors/destructors, inherited/overridden
  virtual slots, virtual calls through pointers, abstract-class rejection/pure
  virtual stubs, `_ZTI`/`_ZTS` typeinfo, `typeid`, and pointer `dynamic_cast`.
  Added virtual/RTTI codegen tests. Windows/WSL both pass 21/21 CTest tests;
  `kr32_tests` reports 162 test cases and 4711 assertions. Weak/COMDAT vtable
  merging moves to M9.8; M9.7 (matrices/transform codegen) is next.
- **2026-11-02 12:26 -08:00** — Completed M9.7. Ported tile and
  transform-stack instruction encoding into the object assembler, added
  `include/kr32/matrix.hpp` and freestanding preprocessor injection for
  `kr32::vec4`/`mat4`, and lowered matrix multiply/add/transpose, vector
  transform, dot product, and transform-stack push/pop/drop to tile
  instructions. Tests cover tile encodings, matrix operations, aliasing, and
  stack discipline. Windows/WSL both pass 21/21 CTest tests; `kr32_tests`
  reports 165 test cases and 4735 assertions. M9.8 (templates and static
  initialization) is next; a full tile allocator/spill and optimization pass
  remain deferred.
- **2026-11-02 12:31 -08:00** — Completed M9.8 for function templates, weak
  symbols, and static initialization. Added explicit template-argument capture,
  template instantiation by explicit arguments/deduction with deduplication,
  weak emission for template/inline symbols, `.weak` object-assembler support,
  and pre-main initialization of non-constant globals in declaration order.
  Added multi-TU template/inline and static-init tests. Windows/WSL both pass
  21/21 CTest tests; `kr32_tests` reports 169 test cases and 4745 assertions.
  Class templates, COMDAT section groups, static destruction, and guarded local
  statics remain deferred; M9.9 (`new`/`delete` and exceptions) is next.
- **2026-11-02 12:36 -08:00** — Started M9.9. Added `new`/`delete` lowering to
  `malloc`/`free` for scalars, objects, and scalar arrays, including constructor
  and destructor calls. Added new/delete tests. Windows/WSL both pass 21/21
  CTest tests; `kr32_tests` reports 171 test cases and 4753 assertions.
  Exceptions, table-driven cleanup/full unwinding, class-array helpers, and
  static destruction remain; M9.9 stays in progress.
- **2026-11-02 12:51 -08:00** — Extended M9.9 with scalar/pointer exceptions.
  Added throw, catch-by-type, catch-all, nested try/catch, `throw;`, cross-
  function propagation via per-call pending checks, and destructor unwinding
  for live locals/caller frames. Added nested, rethrow, RAII, and cross-function
  tests. Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 174 test
  cases and 4765 assertions. M9.9 remains in progress for the table-driven
  `.cpp_except`/LSDA/`__cxa_*` ABI runtime, class exceptions, class arrays,
  static destruction, and constructor-completion flags.
- **2026-11-02 12:56 -08:00** — Added class-array `new[]`/`delete[]` support:
  count cookie, default construction of every element, reverse destruction on
  delete[], and cookie-base freeing. Windows/WSL both pass 21/21 CTest tests;
  `kr32_tests` reports 174 test cases and 4767 assertions. M9.9 remains in
  progress for the table-driven ABI exception runtime, class exceptions, static
  destruction, and constructor-completion flags.
- **2026-11-02 13:01 -08:00** — Added static construction and reverse-order
  destruction for global objects via `__kr32_cxx_fini`, plus
  `kr32/runtime.hpp` declarations with C-linkage runtime symbols. Windows/WSL
  both pass 21/21 CTest tests; `kr32_tests` reports 175 test cases and 4769
  assertions. M9.9 remains in progress for the table-driven ABI exception
  runtime, class exceptions, and constructor-completion flags.
- **2026-11-02 13:17 -08:00** — Added `.cpp_except` LSDA records and
  `__kr32_personality_v0` table-driven catch selection, class exception
  objects with `__cxa_allocate_exception`/`begin_catch`/`end_catch`,
  constructor-completion cleanup for throwing class-array constructors, and a
  fixed-capacity `__cxa_atexit` registry. Added class-exception, throwing-array,
  and ABI-metadata tests. Windows/WSL both pass 21/21 CTest tests; `kr32_tests`
  reports 177 test cases and 4798 assertions. M9.9 remains in progress for the
  true R7-chain unwinder/cleanup landing pads, exception-object destruction, and
  general temporary completion flags.
- **2026-11-02 13:33 -08:00** — Completed M9.9. Replaced the pending-flag path
  with an R7 frame-chain unwinder using `.cpp_except` function descriptors and
  call-site ranges, cleanup landing pads, restored `SP`/`R7`, and
  `_Unwind_Resume`; added a `SETSP` stack-restore instruction. Class exception
  objects now use refcounted `__cxa_begin_catch`/`__cxa_end_catch` destruction
  and `__cxa_rethrow`, and class-array constructor cleanup resumes through
  handlers. Added multi-frame cleanup and refcounted-destruction tests.
  Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 178 test cases
  and 4802 assertions. M9.10 (threads/TLS/atomics) is next.
- **2026-11-02 13:52 -08:00** — Completed M9.10 stage 1: `vm::ClusterHost`
  over `core::Cluster` with one guest thread per core, create/join/yield/exit,
  futex wait/wake, and syscall vectors 44..50. Added `test_threads.cpp` for
  create/join, `thread_create`, futex wait/wake/mismatch, and `thread_self`.
  Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 183 test cases
  and 4841 assertions. M9.10 remains in progress for guest TCB/TLS, FPR/TSP
  context, compiler `thread_local`/atomics, and synchronization headers.
- **2026-11-02 14:03 -08:00** — Completed M9.10 stage 2: exact TCB layout at
  `R11` (self/id/stack/TSP/FPR-save/exception/TLS), per-thread stacks, guest
  blocking `thread_join`, and full GPR/SP/PC/FLAGS/FPR/TSP context
  save-restore. Added TCB-layout, context, and guest-join tests. Windows/WSL
  both pass 21/21 CTest tests; `kr32_tests` reports 186 test cases and 4885
  assertions. M9.10 remains in progress for compiler `thread_local`/atomics and
  synchronization headers.
- **2026-11-02 14:16 -08:00** — Completed M9.10 stage 3: compiler
  `thread_local` via R11-relative TLS, LL/SC atomic load/store/fetch-add/
  compare-exchange, object-assembler LL/SC/FENCE, and the
  `kr32/thread.hpp` thread/mutex/condition_variable/atomic surface. Added
  two-core LL/SC counter, TLS, atomic, and header tests. Windows/WSL both pass
  21/21 CTest tests; `kr32_tests` reports 191 test cases and 4917 assertions.
  M9.10 remains in progress for thread-safe static guards, initialized TLS
  images, and multi-thread mutex/condvar tests.

## 8. Approval

- [x] Review this tracker.
- [x] Approve creation of the `m9-cxx` worktree/branch.
- [x] Assign the phase-1 subagent.
