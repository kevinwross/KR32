# KR32 C++ M9 — Handoff

**Post-merge status:** this branch was merged into `main` as `29942c7` (plus the
handoff merge). Both C and C++ front ends now share the C runtime/libc. Public
`Emit::Object` and `SETSP` disassembly were fixed on main. The `m9-p0` branch
fixes §5 P0 items 1–6 and the debug-side plumbing for item 7; see §1.1.

**Status:** M9.1–M9.12 are committed on `m9-cxx`; annotated tag **`m9`** points at
**`9707b55`** (M9.12). P0 fixes are on `m9-p0`, branched from `main`; the `m9`
tag was not moved.

**Important framing:** `m9` is a milestone snapshot, not a claim that every
deferred ABI/feature item is complete. The shipped subset is usable for the
covered demos/tests, but the gaps below are real and several are correctness
issues, especially across multiple translation units.

## 1. Snapshot

| Item | Value |
| --- | --- |
| Branch | `m9-p0` |
| HEAD | this P0 commit (after the merge) |
| M9 tag | `9707b55` |
| Tag type | annotated; peeled commit `9707b55` |
| Windows gate | MSVC Debug, 29/29 CTest |
| WSL gate | GCC 15.2 headless, 29/29 CTest |
| Unit/doctest counts | 222 test cases / 5317 assertions |
| UI build | OFF in this environment (no SDL gate) |
| Worktree | clean |

### 1.1 P0 status on `m9-p0`

| P0 item | Status | Evidence |
| --- | --- | --- |
| 1. Multi-TU harness | fixed | `compile_and_run_many`; 3-TU startup/symbol test |
| 2. Per-TU static init/fini | fixed | `.init_array`/`.fini_array`, linker bounds, main loops; cross-TU ctor/dtor tests |
| 3. Merged TLS layout | fixed | per-TU `.tls` data + link-time offset cells; distinct-TU TLS test |
| 4. Merged EH metadata | fixed | `.eh_frame_array` table array; cross-TU throw/catch test |
| 5. Blocking guard | fixed | futex wait/wake guard; two-thread local-static race test |
| 6. Class static locals | fixed | construct at static address, guard, destructor registration; test |
| 7. Live thread snapshots | backend done, session injection partial | `ClusterHost::debug_threads`, `Session` provider/refresh, DAP/GUI refresh; no ClusterHost-backed GUI/DAP session yet |

Windows and WSL both pass 29/29 CTest with 222 doctest cases / 5317 assertions.

Phase commits (newest first):

| Commit | Phase |
| --- | --- |
| `9707b55` | M9.12 CLI, docs, Windows/WSL gate, tag `m9` |
| `4c2e43e` | M9.11 C++ class/member/`this` debug records + thread snapshots |
| `ff05520` | M9.10 guarded statics, initialized TLS, thread tests |
| `1cdca3a` | M9.10 `thread_local`, LL/SC atomics, thread headers |
| `7a2a52b` | M9.10 TCB/TLS/context |
| `6f272c6` | M9.10 cluster host thread/futex services |
| `ecddb5b` | M9.9 complete frame-chain unwinder/cleanup |
| `de02121` | M9.9 ABI exception metadata/class exceptions |
| `a90c1b0` | M9.9 static construction/destruction |
| `ace7db3` | M9.9 class arrays |
| `a6ba193` | M9.9 scalar/pointer exceptions |
| `412607a` | M9.9 `new`/`delete` |
| `fc66431` | M9.8 function templates/weak/static init |
| `1521275` | M9.7 matrices/transform stack |
| `d6f940c` | M9.6 virtual dispatch/RTTI |
| `4fca6be` | M9.5 classes/inheritance |
| `5dbfa6b` | M9.4 FP/varargs |
| `4739f13` | M9.3 integer semantic/codegen |
| `5553859` | M9.2 preprocessor/lexer/parser |
| `9b2ce28`,`9ad97be` | M9.1 ABI/API harness |

## 2. What actually shipped

- **Compiler front end:** preprocessor (comments, splices, macros,
  conditionals, built-in `kr32/matrix.hpp`, `kr32/runtime.hpp`,
  `kr32/thread.hpp` injection), C++ lexer/parser/AST for the supported
  C++11-flavoured subset, sorted diagnostics.
- **Code generation:** integer/FP expressions, overloads, Itanium-like
  mangling, globals, functions, classes/constructors/destructors, single
  inheritance, member access, `this` in `R0`, static members, memberwise copy,
  vtables/virtual dispatch, `typeid`, pointer `dynamic_cast`, abstract classes.
- **Matrices:** `kr32::vec4`/`mat4`, `matrix_mul`, `matrix_mul_add`,
  `transpose`, `transform`, `dot`, push/pop/drop; tensor tile instructions and
  transform-stack object-assembler support.
- **Templates:** function templates by explicit arguments/deduction,
  weak symbols, multi-TU inline/template linking, basic static init.
- **Allocation/exceptions:** `new`/`delete`/`new[]`/`delete[]` (including class
  arrays), custom `.cpp_except` function descriptors and call-site ranges, an
  R7-chain unwinder, cleanup landing pads, `_Unwind_Resume`, personality-based
  catch selection, class exceptions with refcounted
  `__cxa_begin_catch`/`__cxa_end_catch`, `__cxa_rethrow`, `__cxa_atexit`
  registry, reverse static destruction, guarded function-local statics.
- **Threads/TLS/atomics:** `vm::ClusterHost` over `core::Cluster`, one guest
  thread per core, create/join/yield/exit/futex syscalls 44–50, TCB at `R11`
  with frozen offsets, per-thread stack/TLS/FP/TSP context save-restore,
  `thread_local` via `R11`, initialized `.tls` images, LL/SC atomics
  (`__atomic_load_n`/`store_n`/`fetch_add`/`compare_exchange_n`), and a
  `kr32/thread.hpp` surface (`thread`, `mutex`, `condition_variable`,
  `atomic_int`).
- **Debug/CLI:** C++ `.debug_info` source/line/function/local/global/class/
  member/`this`/type records, `debug::ThreadInfo` snapshots, DAP thread listing
  and member expansion, GUI thread/member display, `kr32 cxx`,
  `kr32 build file.cpp` with runtime auto-link, and `examples/hello.cpp`.

## 3. Verification commands

Windows (from the repository root):

```powershell
cmake -S . -B build/windows-msvc -DKR32_BUILD_UI=OFF -DKR32_BUILD_TESTS=ON -DKR32_BUILD_CXX_TOOL=ON
cmake --build build/windows-msvc --config Debug --parallel
ctest --test-dir build/windows-msvc -C Debug --output-on-failure
.\build\windows-msvc\tests\Debug\kr32_tests.exe
```

WSL:

```bash
cmake -S . -B build/linux-gcc-cxx -DKR32_BUILD_UI=OFF -DKR32_BUILD_TESTS=ON -DKR32_BUILD_CXX_TOOL=ON
cmake --build build/linux-gcc-cxx -j 8
ctest --test-dir build/linux-gcc-cxx --output-on-failure
./build/linux-gcc-cxx/tests/kr32_tests
```

Smoke test:

```powershell
.\build\windows-msvc\Debug\kr32.exe build examples\hello.cpp --run -o build\hello-cxx.k32
# HALTED exit=42
```

## 4. Gaps and caveats

### 4.1 Correctness gaps that should be fixed before calling M9 production-ready

Resolved on `m9-p0` (regression tests added):

- Multi-TU static initialization and destruction (`init_array`/`fini_array`).
- Multi-TU TLS layout (link-time TLS offsets, no per-TU header collision).
- Multi-TU exception metadata (`eh_frame_array` merged tables).
- Blocking `__cxa_guard_acquire` with futex wait/wake.
- Class-typed function-local statics (construction, guard, destructor).

Remaining:

1. **DAP/GUI still do not own a live `ClusterHost`.** The snapshot/provider
   plumbing, `ClusterHost::debug_threads()`, and DAP/GUI refresh hooks exist,
   and tests exercise a live provider, but no GUI/DAP session currently creates
   and advances a `ClusterHost`. Per-thread stacks/variables and clickable
   thread/source switching remain end-to-end work.
2. **`std::thread`/synchronization surface is only partial.**
   `include/kr32/thread.hpp` uses `namespace kr32`, not `namespace std`; the
   preprocessor injection omits `thread_self`, the `thread` class, and
   `condition_variable`; `thread` takes an integer entry address rather than a
   function pointer; `condition_variable` has no multi-thread test. Atomics are
   integer-only, memory orders are ignored, and there is no `atomic<T>` template
   (class templates are absent).


### 4.2 ABI/feature work deferred from the M9 plan

- **Class templates** (function templates only; class-template use is diagnosed).
- **COMDAT section groups**; only weak symbols/first-definition merging exist.
- **By-value class parameters/returns and `sret`**, including ABI aggregate
  class handling.
- **Access-control enforcement** (public/private/protected are parsed but not
  enforced).
- **Full static destruction semantics**/true `__cxa_atexit` ordering; current
  fini is custom and fixed-capacity (16 entries).
- **Dynamic TLS / `thread_local` class objects / TLS destructors**; current TLS
  images are constant scalar data only.
- **Full exception ABI:** exact `.cpp_except`/LSDA record encoding is custom;
  exception specifications/`noexcept` are absent; reference `dynamic_cast`
  throws are limited; cross-TU EH is unmerged.
- **Tile register allocator/spill pass and matrix optimization/fusion**;
  lowering uses fixed scratch `T0–T3` and runtime loads.
- **F8–F15 allocation**, `long double` distinct semantics (currently alias
  `double`), richer `va_list` register save area.
- **Debug:** template-instance/namespace/lexical-scope records; full per-thread
  variables/stacks; GUI thread/source selection; UI build not exercised.
- **`kr32 cxx` polish:** no include paths, multi-file build, `--run` for the
  cxx-only command, or public object emission.
- **Additional platform matrix:** MSVC Release/Ninja, WSL Clang/ASan, ARM64.
- **Nonstandard ISA extension:** `SETSP` (opcode `0x3F`) was added for the
  unwinder `SP` restore. It is documented in `docs/isa-v1.md`, but the
  disassembler does not decode it; any ABI/ISA review should explicitly accept
  or replace it.

### 4.3 Test gaps

- No multi-TU tests for **static initialization/destruction**, **TLS offsets**,
  or **throw/catch across TUs** (the gaps above are not currently covered).
- No DAP protocol tests for threads/scopes/stack/variables; DAP is compiled but
  not integration-tested.
- GUI is built with `KR32_BUILD_UI=OFF`; `debugger_ui.cpp` changes are not
  compiled in the Windows/WSL gates.
- No two-thread race test for `__cxa_guard_acquire`; no multi-thread
  `condition_variable` test; no futex timeout/spurious-wake tests.
- No class-template, COMDAT, by-value aggregate, access-control, or
  multi-TU exception tests (because those features are absent).
- No static-local class construction/destruction test.
- Only MSVC Debug and GCC headless configurations were run; Release, Clang,
  ASan, and ARM64 were not.

### 4.4 Documentation caveats

- `cpp-impl.md` contains stale historical checklist lines (for example M9.5
  says member/`this` debug records are deferred, and M9.8 says static
  destruction/guards are deferred, though later phases implemented basic
  versions). Treat the phase summary/status near each M9.9–M9.12 section as the
  current state, not every old unchecked line.
- `docs/debug.md` now documents `C`/`M` class/member records and notes thread
  snapshot/per-thread limitations.
- `docs/abi.md` freezes TCB offsets and describes the intended table-driven
  exceptions, but the implementation's `.cpp_except` encoding is custom and
  not necessarily the final ABI record encoding.
- Tag `m9` was applied after the available Windows/WSL gates, with the gaps in
  this document explicitly deferred.

## 5. Ordered task list for the next agent

### P0 — correctness/blocking

Status on `m9-p0`: items 1–6 are fixed with regression tests; item 7 is fixed at
the snapshot/provider layer, but no GUI/DAP session currently owns a live
`ClusterHost` (see §1.1).

1. **Add a multi-TU test harness** (three objects: main, a global/static-init
   TU, and a throw/TLS TU). Use it to expose the next three items before
   changing code.
2. **Implement per-TU static init/destruction.** Emit `.init_array`/equivalent
   entries for each TU and have startup/fini walk them; keep construction order
   and reverse destruction. Add cross-TU order/destruction tests.
3. **Fix multi-TU TLS.** Emit one merged TLS image (or COMDAT/merged TLS
   layout) and assign offset ranges across TUs; load into every new TCB; test
   two TUs with `thread_local` objects.
4. **Merge exception metadata across TUs.** Replace the weak single
   `__kr32_eh_frame` with a linker-merged table (or per-function descriptors
   registered/collected by the linker) and add cross-TU throw/catch tests.
5. **Make `__cxa_guard_acquire` block/wait correctly** and add a two-thread
   local-static race test.
6. **Support class static locals**: construct at the static object's address,
   register/destroy it, and respect construction-completion/guards.
7. **Wire live threads into debugging.** Add an adapter from `ClusterHost`
   (PC/SP/FP/registers per thread) to `debug::Session`, refresh snapshots while
   stepping/continuing, and implement per-thread stack/variable/source
   selection in DAP and GUI.
8. **Finish the public `Emit::Object` path** or remove it from `cxx.h` and CLI
   documentation.
9. **Decode `SETSP` in the disassembler** or replace the unwinder's SP restore
   with an existing ISA mechanism; record the decision in `docs/isa-v1.md` and
   `docs/abi.md`. Decoding is implemented on `main`; the ISA extension remains
   documented in `docs/isa-v1.md`.

### P1 — deferred ABI/features

10. **Class templates** (instantiate/deduplicate, type substitution, class
    debug records, weak symbols).
11. **COMDAT section groups** and linker merging for template/vtable/typeinfo
    sections (beyond first-weak-symbol behavior).
12. **By-value class parameters/returns and `sret`**; update `docs/abi.md` and
    tests.
13. **True static destruction/`__cxa_atexit`** semantics, including per-TU
    registration and ordering.
14. **Dynamic TLS / class `thread_local` / TLS destructors**; TLS guards.
15. **Complete `std::thread` surface** (`namespace std` aliases or explicit
    `kr32` docs), function-pointer entry, `joinable`, `this_thread`, condition
    variable multi-thread tests, futex semantics (`count == 0` wakes all),
    memory-order handling, integer/pointer atomics at minimum.
16. **Access-control enforcement** and remaining class semantics.
17. **Tile register allocator/spills, matrix fusion/transpose elimination.**
18. **Debug records** for namespaces/templates/lexical scopes; value-object
    member expansion; GUI thread/source selection.
19. **CLI**: C++ include paths, multi-file builds, `kr32 cxx --run`, public
    object output.

### P2 — verification/cleanup

20. Run MSVC Release + Ninja, WSL Clang + ASan, and ARM64; add them to CI.
21. Build UI with SDL and run headless GUI screenshots/step tests; add DAP
    protocol tests for threads/scopes/stack/variables.
22. Reconcile stale checklist lines in `cpp-impl.md`/`implementation.md`.
23. Add negative tests for guard races, TLS multi-TU, static init/destruction
    ordering, cross-TU exceptions, class static locals, and DAP thread
    selection.
24. Re-tag a corrected milestone after P0 items are green (do not move `m9`
    silently; use a new tag such as `m9.1` unless the team agrees to retag).

## 6. Recommended workflow for the next agent

1. `git switch m9-cxx`; verify `git status --short` is empty and
   `git describe --tags --exact-match HEAD` is `m9`.
2. Read `docs/abi.md` (ABI/TCB/exceptions), `docs/debug.md`, `docs/host.md`,
   `cpp-impl.md` (current M9.9–M9.12 sections), and this file.
3. Reproduce the baseline gates (commands in §3). Do not start with a broad
   refactor; fix one P0 item and add its regression test.
4. Keep changes on `m9-cxx` (or a new branch from `m9`); **do not merge into
   `main`**. Do not silently change frozen contracts: TCB offsets, `R11` TP,
  `SETSP`, `.cpp_except`/LSDA encoding, or the ABI aggregate rules. If a change
   is needed, update `docs/abi.md`/`docs/isa-v1.md` and call it out explicitly.
5. Pick the next P0 task from §5; add a failing multi-TU/thread test first;
   implement; run both Windows and WSL gates; update `cpp-impl.md` and
   `implementation.md`; commit with a phase-specific message.
6. Keep warnings-as-errors clean (`-Wall -Wextra -Wpedantic -Werror` on GCC,
   `/W4 /WX` on MSVC). Add CTest cases for CLI behavior, not just doctest unit
   cases.
7. After P0 is green, re-run the full matrix and decide whether a new tag is
   warranted. Do not move the existing `m9` tag without a team decision.

## 7. Key files

| Area | Path |
| --- | --- |
| C++ API | `include/kr32/cxx/cxx.h` |
| ABI constants | `include/kr32/cxx/abi.h`, `docs/abi.md` |
| AST/parser | `src/cxx/ast.h`, `src/cxx/parser.cpp` |
| Codegen | `src/cxx/codegen.cpp` |
| Preprocessor | `src/cxx/preprocessor.cpp` |
| Matrix header | `include/kr32/matrix.hpp` |
| Runtime header | `include/kr32/runtime.hpp` |
| Thread header | `include/kr32/thread.hpp` |
| Thread host/service | `include/kr32/vm/threads.h`, `src/vm/threads.cpp` |
| Debug model/parser | `include/kr32/debug/debug.h`, `src/debug/debug.cpp` |
| DAP | `src/dap/dap.cpp` |
| GUI | `src/ui/debugger_ui.cpp` |
| CLI | `src/cli/cli.cpp` |
| C++ tests | `tests/cxx/`, `tests/unit/test_threads.cpp`, `tests/unit/test_debug.cpp` |
| Trackers | `cpp-impl.md`, `implementation.md` |

## 8. Bottom line

The branch has a working C++ subset, a real (if custom) table-driven exception
unwinder, class/RTTI/matrix support, a cluster thread runtime, TLS/atomics, and
C++ debug/CLI integration. The largest remaining risks are **multi-TU static
init/TLS/exception merging**, **guard blocking**, **class static locals**, and
**end-to-end thread debugging**. Fix those before treating `m9` as a stable
release, and do not assume the historical checklists in `cpp-impl.md` are
uniformly current.
