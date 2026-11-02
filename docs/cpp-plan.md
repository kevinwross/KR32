# KR32 C++ toolchain (M9) plan

**Status:** draft for review, 2026-11-02. The major architecture decisions were
confirmed in review: custom C++11 subset front end, one thread per core with
cooperative oversubscription, `R11` as the thread pointer, library matrix types
plus compiler intrinsics, 8-byte `double` alignment with 16-byte public call
boundaries, and explicit per-thread transform-stack discipline. Remaining review
items are listed in section 15. `implementation.md` remains the milestone
checklist.

## 1. Goals

Build a self-contained C++ compiler module for KR32 that can be developed and
tested independently, then linked into `kr32` and the debugger.

The first release must compile and run:

- C++ hello world through the existing assembler, linker, container, and VM.
- Classes, constructors/destructors, inheritance, and virtual dispatch.
- Function and class templates with monomorphic instantiation.
- `new`/`delete`.
- Exceptions that unwind destructors correctly.
- Threads that map onto KR32 cores, with mutexes, condition variables,
  atomics, and thread-local storage.
- C++ source-level debugging through `DebugSession`, the GUI, and DAP where the
  existing debug format can represent it.

The compiler is a KR32-targeted compiler. It is not a host compiler and does not
need to be ABI-compatible with MSVC, GCC, Clang, or the host standard library.

## 2. Non-goals for the first release

- Full ISO C++ conformance or a complete standard library.
- Modules, coroutines, concepts, ranges, full `constexpr`, full template
  metaprogramming, or full two-phase lookup.
- Multiple/virtual inheritance beyond the documented subset.
- Dynamic loading, JIT, or host-thread passthrough.
- Binary compatibility with any existing C++ ABI.
- Self-hosting the compiler.

## 3. Confirmed decisions

These decisions were confirmed during the 2026-11-02 review.

| Area | Decision |
| --- | --- |
| Front end | Custom recursive-descent C++ subset front end; no EDG/Clang dependency |
| Language level | C++11-flavoured subset, not full standard conformance |
| ABI | Documented KR32 C++ ABI, Itanium-like name mangling where practical (mangling still to confirm) |
| Object model | Extend the existing object/debug model; C and C++ share `.k32` |
| Exceptions | Table-driven cleanup/landing pads, introduced after basic classes |
| Threads | Guest threads mapped to KR32 cores; one thread per core with cooperative oversubscription; no host OS threads |
| TLS | `R11` is the C++ thread pointer |
| FP layout | `float` align 4; `double` align 8; 16-byte public call boundaries |
| Matrices | `kr32::mat4`/`vec4` library types plus compiler intrinsics; `T0..T15` caller-saved |
| Transform stack | Explicit library push/pop/drop; TSP owned by the current thread |
| Standard library | Freestanding runtime plus a small selected `std::` subset |
| Driver | `kr32-cxx` standalone executable plus a library API |
| Integration | Library target first, CLI wiring last |
| Directory | `src/cxx`, `include/kr32/cxx`, `tests/cxx` |

## 4. Language subset

### 4.1 Required in the first gate

- Fundamental types already supported by the C compiler: `bool`, `char`, `int`,
  pointers, arrays, references.
- `float` (32-bit) and `double` (64-bit), including literals, arithmetic,
  comparisons, conversions, NaN/infinity, and `long double` as a documented
  alias for `double` initially.
- Namespaces, nested names, `using` declarations.
- Function and operator overloading.
- Default arguments and `const` member functions.
- Classes/structs/unions/enums, access control, constructors, destructors,
  copy constructors, assignment operators.
- Single inheritance and virtual functions.
- Abstract classes, pure virtual functions, virtual destructors.
- `new`, `delete`, `new[]`, `delete[]`.
- Static members, static initialization, and destruction.
- Function templates and class templates with simple explicit/implicit
  instantiation and specialization.
- RTTI sufficient for `typeid` and `dynamic_cast` in the supported hierarchy.
- Exceptions: `throw`, `try`, `catch`, destructor cleanup, rethrow.
- `static_assert`, `nullptr`, `auto`, range-for over arrays, lambdas without
  captures first, then simple captures.
- Matrix/transform support through the `kr32::mat4`, `kr32::vec4`, and
  `kr32::transform` library plus compiler intrinsics (section 4.5).
- A selected `std::` subset: `std::atomic`, `std::mutex`, `std::condition_variable`,
  `std::thread`, `std::this_thread`, `std::size_t`, `std::move`, `std::forward`,
  `std::numeric_limits<float/double>` basics, `std::unique_ptr`/`shared_ptr` if
  runtime time permits.

### 4.2 Deliberately deferred

- Full multiple inheritance and virtual bases.
- Full template partial ordering, SFINAE, variable templates, concepts.
- Full `constexpr` evaluation, consteval, constinit.
- Coroutines, modules, exceptions specifications, reflection.
- Full STL containers/algorithms/iostreams.
- Host-compatible C++ ABI or dynamic shared objects.
- Thread cancellation and full POSIX surface.

### 4.3 C and C++ interoperability

- `extern "C"` functions use the existing C ABI.
- C headers remain usable through the preprocessor and the C ABI.
- C++ name mangling is confined to C++ symbols.
- `char` is 1 byte, `int`/pointers 4 bytes, little-endian; existing
  `docs/abi.md` data model remains the base.

### 4.4 Floating-point model

- `float` is IEEE-754 binary32 in FPR bits 31..0; upper 32 bits are zero.
- `double` is IEEE-754 binary64 occupying all 64 FPR bits.
- `long double` is initially an alias for `double`; no x87-style 80-bit format.
- Arithmetic lowers to `FADD.S/D`, `FSUB.S/D`, `FMUL.S/D`, `FDIV.S/D`,
  `FSQRT.S/D`, `FMIN/FMAX`, and the corresponding compares.
- Conversion lowers to `FCVT.*.S/D` and `FCVT.S/D.*`.
- Match the ISA semantics: round to nearest, ties to even; no FP traps; NaN
  results are canonical; denormals supported; saturating integer conversions.
- Float and double literals must be parsed and rounded identically by the
  compiler and runtime.
- FPR allocation uses `F0..F15`; spills use `FLOAD`/`FSTORE`.
- `alignof(float) == 4`; `alignof(double) == 8`, with public call boundaries
  aligned to 16 bytes. Aggregate layout follows these alignments.

### 4.5 Matrix and transform-stack support

The ISA already has what a useful matrix layer needs: sixteen 4x4 `f32` tiles
(`T0..T15`), `MMUL`, `MMAC`, `MVEC`, `MTRANS`, `VDOT`, and a 64-byte-entry
transform stack (`TSP` + `MPUSH`/`MPOP`/`MDROP`). The C++ layer should expose
that without making every C++ program depend on matrix-specific syntax.

The confirmed first layer is library types plus compiler intrinsics: ordinary
C++ aggregates stored in memory, with inline functions that lower to tile
instructions. Matrices do not become a core language type in M9.

```cpp
namespace kr32 {
struct alignas(16) vec4 { float x, y, z, w; };
struct alignas(16) mat4 { /* 64 bytes, row-major 4x4 f32 */ };

mat4 matrix_mul(const mat4& a, const mat4& b);   // MMUL
mat4 matrix_mul_add(const mat4& a, const mat4& b, const mat4& c); // MMAC
vec4 transform(const mat4& m, const vec4& v);    // MVEC / VDOT
mat4 transpose(const mat4& m);                   // MTRANS
float dot(const vec4& a, const vec4& b);         // VDOT
void push(const mat4& m);                        // MPUSH
void pop(mat4& m);                               // MPOP
void drop();                                     // MDROP
}
```

Implementation strategy:

1. **Library + intrinsics first.** `kr32::mat4` and `kr32::vec4` are ordinary
   aggregates stored in memory; inline functions call compiler intrinsics.
   This keeps matrix support optional and avoids changing C++ grammar.
2. **Intrinsic codegen.** Builtins such as
   `__builtin_kr32_mat4_mul`, `__builtin_kr32_mat4_transform`,
   `__builtin_kr32_mat4_transpose`, `__builtin_kr32_vec4_dot`,
   `__builtin_kr32_mat4_push/pop/drop` lower directly to tile instructions.
3. **Tile register allocation.** Matrices are loaded into `T0..T15` only while
   an expression is being evaluated. Tile registers are caller-saved; matrix
   values crossing a call are passed by pointer/reference unless a future ABI
   adds a tile-argument convention.
4. **Transform stack discipline.** The transform stack is per thread/core.
   `push`/`pop` are explicit in the library; the compiler does not silently
   maintain a transform stack across a call. Temporary tile spills may use
   `MPUSH`/`MPOP`, but the runtime must restore `TSP` on thread switch.
5. **Pattern optimization.** Once correctness works, recognize:
   - `mat4 C = A * B` -> `MMUL`
   - `C += A * B` -> `MMAC`
   - `transform(A, transform(B, v))` -> `MVEC` + `MMAC`/`MMUL` where legal
   - `transpose(transpose(A))` -> identity
   - transform-stack push/pop pairs that can be fused or dropped.
6. **Constants and I/O.** Matrix constants use `MLOAD`/`VLOAD`; printing or
   exposing matrix contents uses ordinary memory loads. No special debug type
   is required for the first gate, but `mat4` should appear as a 4x4 value in
   debug info later.

This gives code generation a real target without making matrices part of the
core C++ type system. A future explicit `tile` language type can be added if
the library approach proves too limiting.

## 5. Threads and multicore model

KR32 has a cooperative multicore cluster with shared RAM, IPIs, LL/SC, and a
deterministic round-robin scheduler. Guest threads must use those cores, not
host OS threads.

### 5.1 Thread model

- A **guest thread** has a stack, a thread control block (TCB), an entry
  function, a TLS block, an FP context, a transform-stack context, and a
  lifecycle state.
- The first implementation maps **one runnable thread per core**.
- When there are more threads than cores, the runtime uses cooperative
  scheduling and `yield`; preemption is deferred until a timer-driven scheduler
  exists.
- The host runner advances the cluster and handles thread-create/join services;
  there are no host `std::thread`s inside the guest.
- A thread context switch saves/restores GPRs, FPRs, SP, and TSP. The transform
  stack must be owned by one thread at a time, or copied/rebased on migration.
- `Cluster` remains the scheduler authority; the compiler/runtime must not
  assume a particular core count.
- Debugging treats threads as independent execution contexts; the UI/DAP
  `threads` request must expose them, and the cores panel should identify the
  thread currently assigned to each core.

### 5.2 Thread runtime ABI

Proposed host services, subject to final vector allocation:

| Service | Purpose |
| --- | --- |
| `thread_create(entry, arg, stack_size)` | Create a thread and return a handle |
| `thread_join(handle)` | Wait for termination |
| `thread_yield()` | Voluntarily deschedule |
| `thread_exit(code)` | Terminate the current thread |
| `futex_wait(address, expected)` | Block/wake primitive for mutex/condvar |
| `futex_wake(address, count)` | Wake waiters |

The guest `std::thread`/`std::mutex` classes are a thin layer over these
services. The exact syscall vectors and error values belong in `docs/host.md`
before implementation.

### 5.3 TLS and thread pointer

`thread_local` needs a fast per-thread base. The confirmed ABI extension is:

- **`R11` is the thread pointer (TP)** in C++ code. The C compiler currently
  uses only `R0-R7`, so reserving `R11` does not break existing C objects.
- C code does not set or depend on `R11`; C++ entry/thunk setup does.
- The TCB layout starts at TP and contains the TLS block, stack limits, thread
  id, and self pointer.
- If a future ABI revision rejects a dedicated register, the fallback is a
  runtime `COREID`-indexed TLS table with slower access.

This must be documented in `docs/abi.md` before codegen depends on it.

### 5.4 Atomics and memory model

- Implement `std::atomic` on LL/SC where possible.
- Start with **sequentially consistent** operations using full `FENCE`.
- Do not claim relaxed/acquire-release support until tested on the cluster.
- `FENCE` is currently a full barrier, which is a valid initial implementation.
- Static initialization uses atomic guard variables (`__cxa_guard_*`).

### 5.5 Thread-related compiler work

- `thread_local` storage and TLS access.
- Thread-safe function-local statics.
- Correct handling of `volatile` and atomics in optimization.
- No assumption that `main` is the only thread.
- Thread-aware debug records and call stacks.

## 6. ABI

Extend `docs/abi.md` into one C/C++ ABI document covering:

- Data layout, alignment, empty classes, padding, bit-fields if supported.
- `float` (4-byte) and `double` (8-byte proposed) alignment and aggregate layout.
- Name mangling grammar and examples.
- `this` convention; member function calling convention.
- Scalar FP arguments in `F0..F7`, FP returns in `F0`/`F1`, and `F8..F15`
  callee-saved; mixed integer/FP argument assignment.
- Tile registers `T0..T15` as caller-saved; matrix arguments by pointer/reference
  in the first ABI; transform-stack (`TSP`) ownership.
- Vtable layout, RTTI pointer, virtual destructor slots.
- Constructor/destructor variants and return conventions.
- Aggregate parameter/return convention.
- Exception object layout, cleanup landing pads, and unwinding tables.
- Thread control block, TP (`R11` proposed), TLS layout, and per-thread FP/TSP
  context save/restore.
- Static initialization/destruction order and guard variables.
- Weak/COMDAT symbol rules for inline functions, templates, vtables, and RTTI.

No codegen should depend on an undocumented ABI decision.

## 7. Front end and pipeline

Proposed pipeline:

```text
source
  -> preprocessing (reuse/extend the C preprocessor)
  -> lexer
  -> C++ parser / declaration builder
  -> semantic analysis and type system
  -> template instantiation
  -> language-neutral IR (new, shared with future C work where practical)
  -> KR32 lowering and register allocation
  -> assembly text
  -> existing object assembler, linker, container, runtime
```

The existing C compiler is a useful reference for diagnostics, expression
lowering, frames, and object emission, but its parser is not a C++ parser. The
new module should expose only a clean compile API; internal headers stay private.

## 8. Runtime and standard library

Freestanding runtime additions:

- `operator new`/`delete` and array forms.
- Constructor/destructor helpers for arrays and aggregates.
- `__cxa_atexit`-style registration and static destruction.
- `__cxa_guard_acquire/release`.
- `type_info`, `dynamic_cast`/`typeid` helpers.
- Exception allocation and cleanup runtime.
- Thread runtime, `futex`, TLS setup, and thread entry thunk.
- `kr32/matrix.hpp`: `mat4`, `vec4`, transform-stack helpers, and FP math
  wrappers (`sqrtf`, `fabsf`, `fminf`, `fmaxf`, and double variants).
- Minimal `std::` types listed in section 4.1.

The initial library is intentionally small. Missing headers must produce a clear
diagnostic rather than accidentally falling back to host headers.

## 9. Object, linker, and container prerequisites

Audit and complete before broad C++ codegen:

- COMDAT/weak section groups and duplicate inline/template merging.
- Vtable/RTTI relocations.
- `.init_array`/`.fini_array` or an equivalent static-init mechanism.
- Section alignment and per-function sections where required.
- Extended debug sections for names, scopes, types, and template instances.
- Linker resolution order for weak definitions and local symbols.
- Deterministic output for golden tests.

If a prerequisite is missing, implement it behind the C++ module first rather
than changing the C ABI silently.

## 10. Debug information

The current format is scalar/C-oriented. Extend it with:

- Namespaces and nested/member names.
- Class/struct/union layout and member offsets.
- Function kinds (free, member, constructor, destructor, template instance).
- `this` pointer location and lexical blocks.
- Template arguments and instantiation names.
- Thread/core identity and per-thread frames.
- Inline call sites, at least as source-location metadata.

Required M9 debug gate: step through a `.cpp` file with correct lines,
variables, class members, and call stack.

## 11. Driver and API

The standalone driver is a test harness and a usable compiler, not a second
product architecture.

Proposed public API:

```cpp
namespace kr32::cxx {

enum class Emit { Assembly, Object };

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

Result compile(std::string_view source, const Options& options = {});

} // namespace kr32::cxx
```

Standalone executable:

```text
kr32-cxx input.cpp [--emit asm|obj] [-o output]
```

- Default: write assembly to stdout or `-o`.
- `--emit obj`: produce a `.k32` relocatable object.
- `--help`, clear diagnostics, non-zero exit on failure.
- Source files are read by the driver; the library stays filesystem-free.
- Later, `kr32 cxx` and `kr32 build file.cpp` call the same API.

## 12. Directory, CMake, and ownership

```text
docs/cpp-plan.md                 this document
src/cxx/                         compiler implementation
src/cxx/tool/main.cpp            standalone driver
include/kr32/cxx/cxx.h           public API
tests/cxx/                       C++ compiler tests
```

- Keep `kr32_cxx` as a normal library target linked to `kr32_asm` and
  `kr32_container` (and later the runtime/linker pieces it needs).
- Add a separate `kr32-cxx` executable target.
- Do not create a second build system; use the root presets and warnings.
- Use a separate Git worktree/branch for M9 so UI work and compiler work do not
  contend for the same working tree.
- The compiler module must never include SDL/UI/platform headers.

## 13. Testing and gates

### Unit

- Lexer, parser, type checker, template instantiation, diagnostics.
- ABI layout, mangling, vtable layout, aggregate calling convention.
- Preprocessor and include handling.
- TLS/TP addressing and atomic lowering.

### Integration

- Compile, assemble, link, run C++ programs through the existing tools.
- Float/double arithmetic, literals, conversions, comparisons, NaN/infinity, and
  FP spills.
- Matrix multiply, transform, transpose, dot product, and transform-stack
  push/pop/drop against scalar reference results.
- Multiple translation units, headers, inline functions, weak/COMDAT.
- Classes, inheritance, virtual calls, constructors/destructors.
- Templates and explicit specializations.
- Exceptions with destructor cleanup.
- `new`/`delete`, static initialization/destruction.
- Threads: two-core counter, mutex, condition variable, atomic, TLS, static-init
  race.
- Debug: breakpoints, step, locals, class members, stack.
- Negative tests for every unsupported feature with a clear diagnostic.

### Cross-platform

Windows MSVC Debug/Release/Ninja, WSL GCC/Clang, ASan/UBSan, and Linux ARM64
when available.

## 14. Implementation phases

1. **Plan, ABI, API, driver skeleton.** Review and freeze decisions.
2. **Lexer/preprocessor/parser skeleton.** Declarations, expressions, types,
   diagnostics; no codegen yet.
3. **Functions, namespaces, references, overloads.** Run C-style C++ through
   link/run.
4. **Scalar float/double.** Literals, FPR allocation, arithmetic, conversions,
   compares, calls, and spills.
5. **Classes and objects.** Layout, constructors/destructors, `this`, single
   inheritance.
6. **Virtual dispatch and RTTI.** Vtables, typeinfo, dynamic_cast.
7. **Matrix/transform intrinsics and library.** `T0..T15`, `MMUL`/`MMAC`/
   `MVEC`/`MTRANS`/`VDOT`, and explicit transform-stack push/pop.
8. **Templates and static initialization.** COMDAT and init arrays.
9. **`new`/`delete`, exceptions, cleanup.**
10. **Thread runtime, TLS, atomics, mutex/condvar, `std::thread` subset.**
11. **Debug info and GUI/DAP integration.**
12. **CLI integration, `build file.cpp`, cross-platform gate, tag `m9`.**

Threads are late in the sequence but not an afterthought: TP/TLS, FP context
save/restore, and transform-stack ownership are ABI decisions made before codegen
begins.

## 15. Open questions for review

1. **Language scope:** is the section 4.1 subset the right M9 gate, including
   `float`/`double` and the listed matrix library surface?
2. **Exceptions:** table-driven cleanup (confirmed direction) — do we require
   destructor unwinding in the first exception gate, or can the first gate be
   throw/catch without full cleanup?
3. **Standard library:** is the minimal `std::` subset enough, or is a named
   container/string subset required for the gate?
4. **FP library:** is scalar `<cmath>`-style math plus `numeric_limits` enough,
   or is formatted FP I/O required as well?
5. **Name mangling:** Itanium-like (recommended) or a smaller custom scheme?
6. **Debug:** must class members/`this`/templates be in the first GUI gate, or
   can they land after the core compiler?
7. **Driver:** is `kr32-cxx` acceptable as the standalone name and root CMake
   target?
8. **Repository workflow:** separate worktree/branch for M9, or direct work in
   the current tree?

## 16. Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Full C++ scope | Never ships | Freeze subset; reject unsupported features clearly |
| ABI churn | Rework codegen/runtime | Freeze layout/mangling/TLS before phase 3 |
| Float/double layout mismatch | Broken structs/ABI | Freeze alignment and FP calling convention early |
| Matrix abstraction leaks | Poor codegen | Library+intrinsics first; measure tile spills |
| Thread model underspecified | Broken TLS/scheduler | Decide TP and thread services before phase 3 |
| COMDAT/linker gaps | Duplicate template/vtable symbols | Audit before phase 6 |
| Debug format too C-specific | GUI cannot inspect C++ | Extend records in phase 5/9 |
| Parallel UI work conflicts | Merge pain | Separate worktree; module-only edits |
| Host/guest thread confusion | Nondeterminism | Guest threads only; cluster scheduler owns them |

## 17. Review checklist

Before handing this to a subagent:

- [x] Confirm custom front end vs external front end.
- [ ] Confirm language subset, including float/double and the matrix surface,
      and thread/exception scope.
- [x] Confirm `R11` TP.
- [ ] Confirm standalone driver name and directory layout.
- [x] Confirm `docs/cpp-plan.md` is the design source of truth.
- [ ] Confirm worktree/branch strategy.

Once approved, create `cpp-impl.md` as the live task/status tracker and keep this
document as the design contract.
