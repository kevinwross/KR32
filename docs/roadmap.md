# Compiler roadmap status

This document records the completed compiler roadmap items and the remaining
known limitations.

## Completed

### C `long long`

- 64-bit storage, arithmetic, shifts, comparisons, and casts
- Signed and unsigned division/modulo helpers
- `printf` formats `%lld`, `%llu`, `%llx`, and `%llo`
- Function parameters/returns, globals, arrays, and struct fields

### C++ `long long`

- Locals, globals, fields, static members, arrays, references, and
  `thread_local` storage
- Function parameters and returns, including register and aggregate
  interactions
- Arithmetic, compound assignment, comparisons, and conversions
- Signed/unsigned division/modulo helpers
- 64-bit casts, wide shifts, and integer/floating-point conversion
- `printf` integration through the shared C libc

### Initializer lists

- Braced local/global arrays and aggregates
- `std::initializer_list` parameters
- Constructors taking `std::initializer_list`, including empty lists and
  64-bit elements
- `begin`/`end`, `data_`/`size_`, and range-for iteration
- Braced returns backed by compiler-managed static storage
- `auto` deduction for initializer-list locals and call returns

### Aggregate ABI

- One-to-eight-byte integer aggregates passed in one or two integer registers
- FP-only aggregates up to 16 bytes passed in one or two FP registers and
  returned in `F0`/`F1`
- Mixed, non-trivial, and larger aggregates use hidden-pointer parameters and
  an `R4` destination for memory-class returns
- Aggregate copy construction and type-aware constructor selection

### Templates

- Function and class templates
- Full and partial specializations, including pointer, multi-level pointer,
  reference, array, and nested template-id patterns
- Out-of-class member definitions and static data members
- Integer non-type parameters and dependent array bounds with `+`, `-`, `*`,
  `/`, unary minus, and parentheses
- Explicit function specializations
- COMDAT `.text.<symbol>`, `.data.<symbol>`, and `.bss.<symbol>` groups with
  linker deduplication

### Lambda, thread, and debug

- Captureless and capturing lambdas
- Capturing lambdas as `std::thread` entries
- Cluster runtime coverage for function pointers and both lambda forms
- Mutex synchronization, shared globals, detached workers, move construction
  through `std::move`, and explicit handle transfer via `release`/`adopt`
- Dynamic `thread_local` initialization through the C++ init array
- Debug records for C++ templates and lambdas

## Known limitations

- Dependent template expressions outside array-bound constant arithmetic remain
  unsupported.
- Full template partial ordering, SFINAE, variable templates, and concepts are
  deferred.
- C structs are still passed by pointer rather than by value.
- Threading is cooperative with one thread per core; preemption is deferred.
- Per-thread variable and stack expansion in the debugger is partial.
