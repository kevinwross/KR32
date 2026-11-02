# KR32 C++ toolchain (M9) plan

**Status:** draft for review, 2026-11-02. This document is the design contract for
`kr32_cxx`; the milestone checklist remains in `implementation.md`.

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

## 3. Proposed decisions for review

These are defaults the implementation should follow unless review changes them.

| Area | Proposed decision |
| --- | --- |
| Front end | Custom recursive-descent C++ subset front end; no EDG/Clang dependency |
| Language level | C++11-flavoured subset, not full standard conformance |
| ABI | Documented KR32 C++ ABI, Itanium-like name mangling where practical |
| Object model | Extend the existing object/debug model; C and C++ share `.k32` |
| Exceptions | Table-driven cleanup/landing pads, introduced after basic classes |
| Threads | Guest threads mapped to KR32 cores; no host OS threads |
| Standard library | Freestanding runtime plus a small selected `std::` subset |
| Driver | `kr32-cxx` standalone executable plus a library API |
| Integration | Library target first, CLI wiring last |
| Directory | `src/cxx`, `include/kr32/cxx`, `tests/cxx` |

## 4. Language subset

### 4.1 Required in the first gate

- Fundamental types already supported by the C compiler: `bool`, `char`, `int`,
  pointers, arrays, references.
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
- A selected `std::` subset: `std::atomic`, `std::mutex`, `std::condition_variable`,
  `std::thread`, `std::this_thread`, `std::size_t`, `std::move`, `std::forward`,
  `std::unique_ptr`/`shared_ptr` if runtime time permits.

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

## 5. Threads and multicore model

KR32 has a cooperative multicore cluster with shared RAM, IPIs, LL/SC, and a
deterministic round-robin scheduler. Guest threads must use those cores, not
host OS threads.

### 5.1 Thread model

- A **guest thread** has a stack, a thread control block (TCB), an entry
  function, a TLS block, and a lifecycle state.
- The first implementation maps **one runnable thread per core**.
- When there are more threads than cores, the runtime uses cooperative
  scheduling and `yield`; preemption is deferred until a timer-driven scheduler
  exists.
- The host runner advances the cluster and handles thread-create/join services;
  there are no host `std::thread`s inside the guest.
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

`thread_local` needs a fast per-thread base. The proposed ABI extension is:

- **`R11` is the thread pointer (TP)** in C++ code. The C compiler currently
  uses only `R0-R7`, so reserving `R11` does not break existing C objects.
- C code does not set or depend on `R11`; C++ entry/thunk setup does.
- The TCB layout starts at TP and contains the TLS block, stack limits, thread
  id, and self pointer.
- If review rejects a dedicated register, the fallback is a runtime
  `COREID`-indexed TLS table with slower access.

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
- Name mangling grammar and examples.
- `this` convention; member function calling convention.
- Vtable layout, RTTI pointer, virtual destructor slots.
- Constructor/destructor variants and return conventions.
- Aggregate parameter/return convention.
- Exception object layout, cleanup landing pads, and unwinding tables.
- Thread control block, TP (`R11` proposed), and TLS layout.
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
4. **Classes and objects.** Layout, constructors/destructors, `this`, single
   inheritance.
5. **Virtual dispatch and RTTI.** Vtables, typeinfo, dynamic_cast.
6. **Templates and static initialization.** COMDAT and init arrays.
7. **`new`/`delete`, exceptions, cleanup.**
8. **Thread runtime, TLS, atomics, mutex/condvar, `std::thread` subset.**
9. **Debug info and GUI/DAP integration.**
10. **CLI integration, `build file.cpp`, cross-platform gate, tag `m9`.**

Threads are phase 8 rather than an afterthought because TP/TLS and atomic
lowering affect ABI and codegen decisions made earlier.

## 15. Open questions for review

1. **Front end:** custom subset (recommended) vs EDG/Clang?
2. **Language scope:** is the section 4.1 subset the right M9 gate?
3. **Threads:** one thread per core with cooperative oversubscription
   (recommended), or a full preemptive scheduler first?
4. **Thread pointer:** reserve `R11` as TP (recommended), or use a slower
   `COREID` TLS table?
5. **Exceptions:** table-driven cleanup (recommended), or a simpler
   `setjmp`-style runtime initially?
6. **Standard library:** is the minimal `std::` subset enough, or is a named
   container/string subset required for the gate?
7. **Name mangling:** Itanium-like (recommended) or a smaller custom scheme?
8. **Debug:** must class members/`this`/templates be in the first GUI gate, or
   can they land after the core compiler?
9. **Driver:** is `kr32-cxx` acceptable as the standalone name and root CMake
   target?
10. **Repository workflow:** separate worktree/branch for M9, or direct work in
    the current tree?

## 16. Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Full C++ scope | Never ships | Freeze subset; reject unsupported features clearly |
| ABI churn | Rework codegen/runtime | Freeze layout/mangling/TLS before phase 3 |
| Thread model underspecified | Broken TLS/scheduler | Decide TP and thread services before phase 3 |
| COMDAT/linker gaps | Duplicate template/vtable symbols | Audit before phase 6 |
| Debug format too C-specific | GUI cannot inspect C++ | Extend records in phase 5/9 |
| Parallel UI work conflicts | Merge pain | Separate worktree; module-only edits |
| Host/guest thread confusion | Nondeterminism | Guest threads only; cluster scheduler owns them |

## 17. Review checklist

Before handing this to a subagent:

- [ ] Confirm custom front end vs external front end.
- [ ] Confirm language subset and thread/exception scope.
- [ ] Confirm `R11` TP or alternative.
- [ ] Confirm standalone driver name and directory layout.
- [ ] Confirm `docs/cpp-plan.md` is the design source of truth.
- [ ] Confirm worktree/branch strategy.

Once approved, create `cpp-impl.md` as the live task/status tracker and keep this
document as the design contract.
