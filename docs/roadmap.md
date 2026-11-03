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

- One-to-four-byte trivial aggregates passed and returned in registers
- Eight-byte trivial aggregates passed and returned in register pairs
- Floating and mixed integer/floating register aggregates
- Larger aggregates use hidden-pointer parameters and `sret` returns
- Aggregate copy construction and type-aware constructor selection

### Templates

- Function and class templates
- Full and partial specializations, including pointer, multi-level pointer,
  reference, and fixed-size array patterns
- Out-of-class member definitions and static data members
- Integer non-type parameters and constant-expression array bounds with
  multipliers/offsets
- Explicit function specializations
- Weak template static data and function-local statics merged across
  translation units
- COMDAT `.text.<symbol>` sections for template functions with linker
  deduplication

### Lambda, thread, and debug

- Captureless and capturing lambdas
- Capturing lambdas as `std::thread` entries
- Cluster runtime coverage for function pointers and both lambda forms
- Mutex synchronization, shared globals, detached workers, and explicit
  handle transfer via `release`/`adopt`
- Debug records for C++ templates and lambdas

## Known limitations

- Template array bounds support constant multipliers/offsets, not arbitrary
  symbolic expressions.
- Nested template-id partial-specialization patterns are limited.
- Aggregates larger than eight bytes use the interpreter's hidden-pointer ABI
  rather than full register classification.
- A `std::thread` move constructor is not provided; use `release`/`adopt`.
- COMDAT grouping is implemented at template-function section granularity;
  template data relies on weak-symbol merging.
