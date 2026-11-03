# KR32 ILP32 ABI (C and C++)

This document records the ABI produced by the M4 C bootstrap and freezes the
C++ ABI contract for M9. It is a design contract, not a description of any host
ABI: KR32 code is not binary-compatible with MSVC, GCC, Clang, or the host
standard library.

The machine-checkable form of the layout and register constants is
[`include/kr32/cxx/abi.h`](../include/kr32/cxx/abi.h). The design source of
truth is [`docs/cpp-plan.md`](cpp-plan.md); changed decisions must update both
documents and the ABI revision.

**C++ ABI revision:** 1 (M9). Reviewed and approved 2026-11-02; all review
decisions were accepted, with the exact TCB offsets explicitly deferred to
M9.10.

## Data model

- `char`, `_Bool`: 1 byte.
- `int`, pointers: 4 bytes.
- `float`: 4 bytes, 4-byte alignment.
- `double`: 8 bytes, 8-byte alignment. `long double` is not yet accepted by the M4
  C front end; the C++ plan aliases it to `double`.
- Little-endian.
- Aggregates are laid out with members aligned to their natural alignment
  (`min(size, 4)` for integer members, 4 for `float`, 8 for `double`); structs
  are padded to their maximum member alignment; unions place every member at
  offset zero and take the largest member size.
- Arrays are contiguous; a pointer is 4 bytes.

### Scalar sizes and alignment

| Type | Size | Alignment | Notes |
| --- | --- | --- | --- |
| `bool` | 1 | 1 | C `_Bool` and C++ `bool` |
| `char` | 1 | 1 | Plain, signed, and unsigned char |
| `int` | 4 | 4 | |
| pointer / reference | 4 | 4 | 32-bit address |
| `float` | 4 | 4 | IEEE-754 binary32 |
| `double` | 8 | 8 | IEEE-754 binary64 |
| `long double` | 8 | 8 | Alias for `double` in ABI revision 1 |

`sizeof(T)` is always a multiple of `alignof(T)`. `float` values in an FPR use
bits 31..0 with the upper 32 bits zero; `double` uses all 64 bits.

### Aggregate layout

The C bootstrap rules above remain the base. C++ extends them as follows:

- Members are assigned in declaration order to the next offset that satisfies
  the member's natural alignment.
- Natural alignment is the scalar alignment from the table above, the
  aggregate's own alignment, the element alignment for arrays, or a stronger
  `alignas(N)` alignment. `alignas` must be a power of two; revision 1 supports
  up to 16.
- The aggregate alignment is the maximum of its members' alignments (1 for an
  empty class), and its size is rounded up to that alignment. Tail padding is
  part of the object.
- Unions place every member at offset zero, with size and alignment equal to the
  largest member, rounded up to the alignment.
- An empty class or struct has size 1 and alignment 1.
- Single inheritance places the base subobject at offset 0. The derived members
  follow the base object's full size (including its tail padding). Multiple and
  virtual inheritance are outside revision 1.
- Bit-fields are rejected with a diagnostic in the first gate.
- A `double` member raises the containing aggregate's alignment to at least 8;
  `alignas(16)` raises it to 16. `kr32::vec4` is 16 bytes with alignment 16 and
  `kr32::mat4` is 64 bytes with alignment 16, both stored row-major.

The C and C++ layouts agree for every type the C compiler can currently express
(it has no `float`/`double`). Any future C extension that adds `double` must use
the same alignment rule rather than changing `.k32` silently.

### Public call alignment

- The C++ caller aligns `SP` to 16 bytes immediately before every `CALL`.
  `CALL` pushes a 4-byte return address, so a callee normally enters with
  `SP % 16 == 12`.
- A C++ callee realigns `SP` to 16 bytes in its prologue before making any
  nested call. This also supports a C caller that only guarantees 4-byte
  alignment; a C++ function called from C must not assume 16-byte alignment at
  entry.
- Stack slots are 4 bytes. A `double` or a stack-passed `float` occupies an
  8-byte, 8-byte-aligned slot.
- The outgoing argument area is padded so that the caller restores its own
  16-byte alignment after the call.
- The bootstrap 4-byte `PUSH`/`POP`/`ADJSP` requirement is unchanged; 16-byte
  alignment is a public-boundary rule, not a requirement on every temporary.

## Registers

| Register(s) | Role |
| --- | --- |
| `R0..R3` | First four integer-class arguments; `R0` is the integer return value |
| `R4..R6` | Caller-saved scratch; runtime helpers may clobber them |
| `R7` | Frame pointer (callee-saved) |
| `R8..R10` | Callee-saved general registers |
| `R11` | C++ thread pointer (`TP`); reserved and preserved across calls |
| `R12..R15` | Caller-saved scratch |
| `SP` | Stack pointer, separate from the GPR file |
| `F0..F7` | Scalar FP arguments and caller-saved scratch; `F0`/`F1` return values |
| `F8..F15` | Callee-saved FP registers |
| `T0..T15` | 4x4 `f32` tiles; caller-saved |

The C compiler uses only `R0-R7`; `R8-R11` remain reserved for the wider ABI. In
C++ translation units `R11` is the thread pointer and is not available to the
general register allocator. C code never sets or depends on `R11`.

### Integer argument assignment

Integer-class arguments are assigned to `R0`, `R1`, `R2`, `R3` in declaration
order. The following consume an integer argument register:

- scalar `bool`, `char`, `int`, and pointers/references;
- the implicit `this` pointer of a non-static member function;
- a hidden return pointer for a memory-class aggregate return.

The fifth and later integer-class arguments are passed in the outgoing stack
argument area at increasing addresses. An argument that does not fit in the
remaining registers of its class is stack-passed; later arguments may still use
remaining registers of the other class (integer and FP assignment are
independent).

`R0` holds the integer-class return value or the returned `sret` pointer.

### Floating-point argument, return, and callee-saved rules

- Scalar `float` and `double` arguments use `F0..F7` in declaration order,
  independently of the integer register assignment.
- `float` occupies the low 32 bits of its FPR; the upper 32 bits are zero.
- `double` (and `long double`, which is an alias) uses all 64 bits of one FPR.
- A scalar `float`/`double` return value is in `F0`.
- An FP-only aggregate of up to 16 bytes may be returned in `F0`/`F1` (two
  eightbytes); a scalar return uses only `F0`.
- `F0..F7` are caller-saved. `F8..F15` are callee-saved: a function that uses
  one must save and restore it.
- There is no implicit promotion for named arguments: `float` stays `float` and
  is passed in an FPR. Promotion applies only to variadic arguments.
- FP arguments that find `F0..F7` exhausted are passed on the stack in 8-byte,
  8-byte-aligned slots.

## Stack frames

The bootstrap compiler emits:

```text
PUSH R7
ADJSP #-frame
LEA R7, [SP]
```

Locals and parameters live at positive offsets from `R7`. Temporary expression
values use `PUSH`/`POP` below `R7`. The epilogue is:

```text
ADJSP #frame
POP R7
RET
```

The C bootstrap uses 4-byte stack alignment for integer objects; `double` locals
and variables are aligned to 8 bytes within the frame. C++ keeps the same frame
shape and adds:

- `frame` is chosen so that `SP` is 16-byte aligned before every nested public
  call; the saved `R7` chain and return addresses stay where `docs/debug.md`
  expects them.
- Outgoing stack arguments occupy the low end of the frame, at increasing
  addresses, and are released after the call.
- `SP` and `TSP` are independent. A function entry does not change `TSP`.
- Exception cleanup may need extra frame state (landing-pad selector, current
  exception); that state lives in the frame or TCB, not in registers reserved for
  values.

## Calls and aggregates

- Up to four scalar arguments are supported. Integer/pointer arguments travel in
  `R0..R3`; `float`/`double` arguments travel in `F0..F7` in declaration order.
- Integer return values are in `R0`; `float`/`double` returns are in `F0`.
  (`F1` is reserved for C++ two-register FP returns.)
- Struct/union values are not passed or returned by value by the bootstrap C
  compiler; C passes pointers instead. C++ uses the aggregate convention below.
- The caller stages evaluated arguments in frame slots and loads the integer and
  FP register banks separately before `CALL`. This preserves left-to-right
  evaluation and mixed integer/FP argument lists.

### C varargs

- The fixed parameters use the normal register convention. Variadic arguments
  are passed on the stack in **uniform 8-byte, 8-byte-aligned slots**, pushed in
  reverse order so the first variable argument is at `[SP]` at the call.
- `float` promotes to `double`; `char`, `_Bool`, and integer types promote to
  `int`. Pointers occupy one 8-byte slot with the low 4 bytes significant.
- `va_list` is a 4-byte `char *` pointing at the next stack slot. `va_start`
  computes `R7 + frame_size + 8` (the first argument above the return address
  and saved frame pointer); `va_arg` loads the slot and advances by 8 bytes.
- The caller removes all variadic stack slots after the call. There is no
  register save area and no hidden argument count.

### C++ aggregate parameter and return convention

Revision 1 uses a deliberately small set of aggregate classes:

- A trivial aggregate of 4 bytes or less with no FP members is passed in one
  integer register.
- A trivial aggregate of 8 bytes or less with no FP members is passed in one or
  two integer registers as needed.
- A trivial aggregate of 8 bytes or less whose members are all `float`/`double`
  is passed in one FPR.
- An FP-only trivial aggregate of more than 8 and up to 16 bytes is passed in
  two FPRs and returned in `F0`/`F1`.
- Every other aggregate, including `mat4` by value, mixed FP/integer aggregates
  over 8 bytes, and aggregates larger than 16 bytes, is memory class:
  - passed as an invisible pointer to a caller-owned copy (the callee may modify
    its copy);
  - returned through a hidden `sret` pointer that is the first integer-class
    argument (`R0`); the callee returns the same pointer in `R0`.
- A type with a non-trivial copy constructor, move constructor, or destructor is
  passed and returned by invisible reference to a caller-owned temporary, even
  if it is small.
- A stack-passed aggregate with alignment 16 starts at a 16-byte-aligned
  outgoing offset.

An empty class has size 1 and is passed as a one-byte integer-class value.

The aggregate classification is a first-gate rule and is listed as a review
item at the end of this document.

## C++ ABI revision 1 (M9)

### Name mangling

C++ symbols use an Itanium-like grammar. `extern "C"` functions and objects are
not mangled and use the C ABI. Only C++ symbols carry the `_Z` prefix.

```text
<mangled-name>       ::= "_Z" <encoding>
<encoding>           ::= <function-name> <bare-function-type>
                       | <data-name>
                       | <special-name>
<name>               ::= <source-name> | <operator-name>
<function-name>      ::= <name> | <nested-name>
<data-name>          ::= <name> | <nested-name>
<special-name>       ::= "TV" <type> | "TI" <type> | "TS" <type>
<nested-name>        ::= "N" [ "K" ] [ "V" ] <prefix> <unqualified-name> "E"
<prefix>             ::= <prefix> <unqualified-name> | <unqualified-name>
<unqualified-name>   ::= <source-name> | <operator-name> | <ctor-dtor-name>
                       | <unqualified-name> <template-args>
<source-name>        ::= <positive-length> <identifier>
<bare-function-type> ::= <type>+ [ "z" ]
<class-type>         ::= <source-name> | <nested-name>
<type>               ::= <builtin-type> | <class-type>
                       | "P" <type>                  # pointer
                       | "R" <type>                  # lvalue reference
                       | "O" <type>                  # rvalue reference
                       | "K" <type>                  # const
                       | "V" <type>                  # volatile
                       | "A" <number> "_" <type>     # array
<builtin-type>       ::= "v" | "b" | "c" | "i" | "j" | "f" | "d"
```

The builtin codes in revision 1 are `v` void, `b` bool, `c` char, `i` int,
`j` unsigned int, `f` float, and `d` double. `long double` is the same type as
`double`, so it mangles as `d`. Additional Itanium builtin codes are reserved
for later language extensions.

Rules:

- `<source-name>` length counts bytes and has no leading zeroes.
- A namespace or class qualifier is a nested name. A free function at global
  scope uses a bare `<source-name>`.
- Member functions include their class in the nested name. A `const` member
  function adds `K` to the nested-name cv-qualifiers; a `volatile` member adds
  `V`.
- The implicit `this` parameter is not mangled. The return type of a non-template
  function is not mangled, so functions cannot be overloaded on return type
  alone.
- A variadic function terminates its bare function type with `z`.
- Constructors use `C1` (complete), `C2` (base), and reserved `C3` (allocating)
  codes. Destructors use `D0` (deleting), `D1` (complete), and `D2` (base).
- Operator functions use the Itanium operator-name codes (`pl`, `mi`, `ml`,
  `dv`, `eq`, `ne`, `lt`, `gt`, `le`, `ge`, `cl`, `ix`, `as`, and their compound
  forms).
- Template instantiations use `I <template-args> E` after the unqualified name;
  function-template encodings include the return type. Templates are finalised
  in phase M9.8, but the encoding direction is fixed here.
- Repeated types and nested-name prefixes use the Itanium substitution
  dictionary (`S_` first, `S0_` second, ...). Every translation unit must produce
  the same mangled name for the same declaration.
- Vtable, typeinfo, and typeinfo-name objects use the Itanium special names:
  `_ZTV<type>`, `_ZTI<type>`, and `_ZTS<type>`. The `<type>` is the mangled
  class name without the `_Z` function prefix (for example `_ZTV6Widget`).

Concrete examples:

| Declaration | Mangled symbol |
| --- | --- |
| `int add(int, int)` | `_Z3addii` |
| `void ns::f()` | `_ZN2ns1fEv` |
| `void Widget::set(int)` | `_ZN6Widget3setEi` |
| `void ns::Widget::draw() const` | `_ZNK2ns6Widget4drawEv` |
| `Widget::Widget()` | `_ZN6WidgetC1Ev` |
| `Widget::~Widget()` | `_ZN6WidgetD1Ev` |
| `int Widget::count` (static data member) | `_ZN6Widget5countE` |
| `void logf(const char*, ...)` | `_Z4logfPKcz` |
| `double scale(double, float)` | `_Z5scaledf` |

### `this` and member functions

- A non-static member function receives `this` as an implicit first
  integer-class argument in `R0`; the first explicit parameter therefore starts
  in `R1` (or the first FP register if it is FP class). This keeps the call
  convention compatible with the existing integer argument assignment and makes
  virtual dispatch a normal indirect call.
- `this` is a 4-byte pointer. A `const` member function receives a pointer to a
  const object, but the bit pattern in `R0` is an ordinary pointer.
- A static member function has no `this` and is an ordinary function whose
  mangled name is class-qualified.
- A virtual call loads the vptr from offset 0 of `*this`, indexes the slot, and
  calls it with `this` still in `R0`.
- Constructors and destructors also receive `this` in `R0`.
- `this` is assumed non-null for a well-formed call; revision 1 does not add a
  null check.

### Constructors and destructors

| Variant | Meaning | Status in revision 1 |
| --- | --- | --- |
| `C1` | complete-object constructor | emitted |
| `C2` | base-subobject constructor | emitted when needed |
| `C3` | allocating constructor | reserved |
| `D0` | deleting destructor | emitted for `delete` |
| `D1` | complete-object destructor | emitted |
| `D2` | base-subobject destructor | emitted when needed |

- Constructors and destructors return `void`; `R0` is unspecified on return.
  `this` is in `R0` on entry.
- Construction runs base constructor, then member constructors in declaration
  order, then the constructor body. The vptr is installed by each constructor so
  virtual calls during construction dispatch to the class under construction.
- Destruction runs the destructor body, then member destructors in reverse
  declaration order, then the base destructor. `D1`/`D2` restore the vptr to
  their own class's vtable before the body so virtual calls in a destructor
  dispatch to that class.
- A virtual destructor occupies two vtable slots: `D1` (complete) followed by
  `D0` (deleting). A derived override replaces both.
- `new T` calls `operator new`, then `C1`; `delete p` calls `D0`. Array forms use
  the runtime constructor/destructor helpers described in `docs/cpp-plan.md`
  section 8.

The constructor return convention is approved in the review decisions below.

### Virtual dispatch, vtable layout, and RTTI

A polymorphic object stores its vptr at offset 0. With single inheritance the
base subobject is at offset 0 and the whole object shares one vptr.

```text
        vptr -> +----------------------+
                | address point        |  slot 0
                | slot 1               |
                | ...                  |
                | slot n-1             |
                +----------------------+
                | offset-to-top (= 0)  |  vptr - 4
                | type_info pointer    |  vptr - 8
                +----------------------+
```

- The vptr points at the address point, not at the start of the vtable object.
  `[vptr - 8]` is the `type_info` pointer and `[vptr - 4]` is the offset-to-top.
- Slots follow source declaration order: base-class virtual functions first in
  their declaration order, then new virtuals in derived declaration order.
  An override replaces the base slot rather than appending a new one.
- A pure virtual slot points at the runtime's `__cxa_pure_virtual` stub.
  Abstract classes cannot be instantiated but still have a vtable and typeinfo.
- Revision 1 has no secondary vtables, no thunks, and no virtual bases;
  offset-to-top is always zero.
- The vtable is emitted as a weak COMDAT symbol named `_ZTV<type>`; the typeinfo
  object is `_ZTI<type>` and its name string is `_ZTS<type>`.

`type_info` layout:

| Offset | Field |
| --- | --- |
| 0 | vtable pointer (runtime-provided `__class_type_info` vtable) |
| 4 | pointer to the mangled type name (`_ZTS...`) |
| 8 | base typeinfo pointer (single-inheritance form, `__si_class_type_info`) |

Revision 1 uses only `__class_type_info` and `__si_class_type_info`.

- `typeid(T)` for a non-polymorphic type yields the static `_ZTI<type>` address.
  For a polymorphic glvalue it loads `[vptr - 8]`.
- `dynamic_cast` uses the `__si_class_type_info` base chain. A failed pointer
  cast yields null; a failed reference cast throws `std::bad_cast` from the
  runtime. Cross-casts and virtual-base casts are outside revision 1.

The exact vtable and typeinfo offsets are mirrored in `abi.h` and are approved
in the review decisions below.

### Exceptions, cleanup, and landing pads

Exceptions are table-driven and unwind through every live destructor.

- `throw` copies the thrown object into a runtime-allocated exception object via
  `__cxa_allocate_exception` and calls `__cxa_throw(object, typeinfo,
  destructor)`. The exception object starts with a `__cxa_exception` header
  (typeinfo, destructor, reference/handler counts, current-exception link, and
  the generic unwind header). The thrown object follows at a fixed aligned
  offset. `__cxa_begin_catch` returns a pointer to the thrown object;
  `__cxa_end_catch` releases it. Exact header offsets are frozen with the
  runtime in phase M9.9.
- Every function that needs cleanup or handlers contributes a language-specific
  data area (LSDA) to an allocated read-only `.cpp_except` section. The section
  is ordinary rodata in the `.k32` container: it is not a debug section and is
  present in the run image.
- An LSDA contains a call-site table, an action table, and a type table:
  - call-site entries map `[start_pc, end_pc)` ranges to a landing pad and an
    action index;
  - action entries describe either a cleanup or a catch filter;
  - type entries hold `type_info` pointers for the catch clauses.
- The unwinder walks the saved `R7` frame chain (return address at
  `R7 + frame + 4`, saved `R7` at `R7 + frame`, matching `docs/debug.md`),
  finds the innermost call-site entry that covers the return address, and calls
  the personality routine `__kr32_personality_v0`.
- A landing pad receives the exception pointer in `R0` and the handler selector
  in `R1`. A cleanup landing pad runs the required destructors and then calls
  `_Unwind_Resume`, so unwinding continues outward. A catch landing pad compares
  the selector/typeinfo, calls `__cxa_begin_catch`, executes the handler, calls
  `__cxa_end_catch`, and either continues after the try block or rethrows.
- Full destructor unwinding: the compiler emits a cleanup action for every
  automatic object that is live at a potentially throwing call. Destructors run
  in reverse construction order. Arrays, conditionals, and subobjects use
  construction-completion flags so that a destructor never runs for an object
  whose construction did not finish. Temporaries destroyed at the end of a full
  expression are represented the same way.
- Cleanup actions are idempotent and must not throw. An exception escaping
  `main` or a thread entry calls `std::terminate`.
- `throw;` rethrows the exception currently being handled, using the
  per-thread exception state described below.
- Revision 1 has no exception specifications (`noexcept` is not a supported
  language feature yet) and no dynamic exception specifications.

The exact `.cpp_except` record encoding and the image-level handler index are
frozen in phase M9.9.

### Variadic functions

- Named arguments keep the normal register ABI (`R0..R3` and `F0..F7`).
- Default argument promotions apply: `float` promotes to `double`, and integer
  types smaller than `int` promote to `int`.
- Variadic arguments are passed on the stack, in declaration order at
  increasing addresses, starting immediately after any stack-passed named
  arguments. They never consume the named-argument registers.
- Each variadic argument occupies one 8-byte, 8-byte-aligned slot. A promoted
  `double` uses the whole slot; smaller promoted values are stored in the
  low-addressed bytes of the slot and the remaining bytes are unspecified. The
  variadic area starts at an 8-byte-aligned outgoing offset.
- `va_list` is a 4-byte pointer to the first variadic slot. `va_start`
  computes that address from the frame pointer using the fixed size of the named
  argument area (named FP arguments in registers do not need a register save
  area because variadic arguments are stack-only). `va_arg` reads the slot and
  advances by 8; `va_copy` copies the pointer; `va_end` is a no-op.
- `va_arg` on a named parameter is outside the ABI (standard C++ leaves it
  undefined). Aggregate/class variadic arguments are rejected with a diagnostic
  in the first gate; only scalar and pointer variadic arguments are supported.

Uniform 8-byte variadic slots are approved in the review decisions below; the
alternative is naturally-sized 4-byte slots with explicit padding before
`double`.

### Threads: `R11` thread pointer, TCB, and TLS

`R11` is the C++ thread pointer (TP). The thread runtime sets it before entering
C++ code, and every C++ function may assume it is valid and stable on entry.
Ordinary C++ code does not modify it. C code does not set or depend on it. The
runtime installs TP for the initial thread before `main` and for every guest
thread before its entry function, so C++ code reached from a C caller on a
properly initialized thread still sees a valid TP.

The TCB begins at TP and uses non-negative offsets. The layout direction is:

| TP offset | Field |
| --- | --- |
| 0 | self pointer (equal to TP) |
| 4 | thread id |
| 8 | stack base (highest usable address) |
| 12 | stack limit (lowest usable address) |
| 16 | current `TSP` (transform stack pointer) |
| 20 | FPR context save-area pointer |
| 24 | current-exception pointer (per-thread handler state) |
| 28 | reserved, zero |
| 32 | start of the static TLS block |

Exact offsets are fixed with the thread runtime in phase M9.10, but the
direction is frozen: the static TLS block immediately follows the TCB header,
every `thread_local` object has a link-time-constant positive offset from TP,
and code generation uses `TP + 32 + offset` (initial-exec model). Offsets are
assigned after `.tls` sections are merged; each access loads its link-time
offset from a data cell rather than assuming a per-TU base. There are no
negative TP offsets. Dynamic TLS (`__tls_get_addr`) is outside revision 1.

Per-thread context:

- A thread context switch saves and restores the GPRs, `SP`, `PC`/`FLAGS`, all
  16 FPRs (128 bytes), and `TSP`. The FPR save area is addressed from the TCB;
  `TSP` is stored in the TCB. The transform-stack memory region belongs to the
  thread.
- Tile registers `T0..T15` are caller-saved evaluation temporaries and are not
  part of the saved thread context. A context switch is a call-like boundary for
  tiles; a thread that needs a tile value across a switch must spill it.
- A migrated thread restores `TP` and `TSP` before resuming; the transform-stack
  memory is owned by the thread, not by a core.
- The thread entry thunk installs the TCB address in `R11` before calling the
  C++ entry function. Zero-initialized and constant-initialized TLS is ready
  before entry; dynamic `thread_local` initialization uses the same guard
  mechanism as function-local statics and is finalised in M9.10.
- Guest thread create/join/yield/exit and futex services are host services
  documented in [`docs/host.md`](host.md); the compiler only assumes the TP/TLS
  and context rules above.

### Tiles, matrices, and the transform stack

- `T0..T15` are 512-bit (64-byte) row-major 4x4 `f32` tiles. They are
  caller-saved: no function may assume tile contents survive a call or a thread
  switch. The compiler allocates tiles only while evaluating an expression and
  spills with `MPUSH`/`MPOP` or `MLOAD`/`MSTORE`.
- Matrices are ordinary aggregates in memory; they are not a tile-class
  argument. In revision 1 every matrix/vector argument is passed by pointer or
  reference (4-byte integer class). A by-value `mat4`/`vec4` uses the
  aggregate memory/register-class rules above; `mat4` is memory class and uses
  `sret`.
- `kr32::vec4` is 16 bytes, alignment 16, `{float x, y, z, w;}`.
  `kr32::mat4` is 64 bytes, alignment 16, row-major 4x4 `f32`. These alignments
  permit `VLOAD`/`VSTORE` and tile transfers without unaligned faults.
- `TSP` is per-thread, 64-byte aligned, grows down in 64-byte entries, and is
  read/written with `RDSP`/`WRSP`. `MPUSH`, `MPOP`, and `MDROP` operate on one
  entry.
- The transform stack is explicit: the library's `kr32::push`/`pop`/`drop` are
  the language-level operations, and the compiler never silently maintains a
  transform stack across a call.
- `TSP` is callee-preserved: a function must return with the same `TSP` value it
  entered with, and must balance every `MPUSH` with an `MPOP`/`MDROP` on every
  exit path, including exception cleanup. Entries above the entry `TSP` belong
  to the caller and are not modified.
- A function that uses the transform stack owns only the region below its entry
  `TSP`; the thread runtime initializes `TSP` to the top of the thread's
  transform-stack region and restores it on a context switch.
- `MLOAD`/`MSTORE`/`VLOAD`/`VSTORE` follow the ISA's 4-byte alignment rule;
  the 16-byte object alignment above is stronger and always satisfies it.

### Weak and COMDAT symbols

The `.k32` object model already has weak symbol binding and a reserved COMDAT
section flag. Revision 1 uses them as follows:

| Definition | Linkage |
| --- | --- |
| Inline function | weak, COMDAT |
| Function template instantiation | weak, COMDAT |
| Class template instantiation members | weak, COMDAT |
| Vtable (`_ZTV...`) | weak, COMDAT |
| Typeinfo and typeinfo name (`_ZTI...`/`_ZTS...`) | weak, COMDAT |
| Guard variable for a static local of an inline/template function | weak, COMDAT |
| Non-inline function, namespace-scope object, `extern "C"` definition | global strong |
| `static` function, anonymous-namespace entity, local symbol | local |
| Function-local static guard in a non-inline function | local |

- A COMDAT group is keyed by the mangled symbol name (the vtable/typeinfo
  special name for those objects). Every copy must be byte-identical, must use
  the same group key, and must carry the same relocations.
- The linker keeps the first definition, discards duplicate weak definitions,
  lets a strong definition replace a weak one, and reports duplicate strong
  definitions as an error. Existing behavior in `src/link/linker.cpp` already
  implements the weak-symbol part; COMDAT group merging is phase M9.8 work.
- Guard variables are zero-initialized before any constructor runs. The
  runtime provides `__cxa_guard_acquire` and `__cxa_guard_release`; the guard is
  a 4-byte object aligned to 4.
- Output remains deterministic: the same inputs produce the same symbol names
  and group keys in every translation unit.

### Static initialization and destruction

- Per-translation-unit static constructors are emitted as `.init_array` entries
  (allocated arrays of function pointers). The linker concatenates them and
  defines `__init_array_start`/`__init_array_end`; `main` walks the range forward
  before its body. Static destructors use `.fini_array` with
  `__fini_array_start`/`__fini_array_end` and are walked in reverse at exit.
- Dynamic initializers within a translation unit run in declaration order.
  Ordering across translation units follows link order; revision 1 does not
  promise more.
- Static destruction is registered with a `__cxa_atexit`-style runtime call and
  runs in reverse registration order during normal exit.
- Function-local statics use a guard variable plus
  `__cxa_guard_acquire`/`__cxa_guard_release`. The guard protocol blocks on a
  futex when another thread is initializing; the compiler must not elide the
  guard for a type with a non-trivial constructor.
- Exception frame tables are emitted per translation unit and registered in
  `.eh_frame_array`; the linker defines `__eh_frame_array_start/end` and the
  unwinder walks every table. The TLS section is merged by the linker and each
  `thread_local` access loads its link-time offset from a data cell, then adds
  `TP + 32 + offset`.

### Approved review decisions

The following details were frozen for implementation and approved during the
2026-11-02 review. They are called out because `docs/cpp-plan.md` does not spell
them out.

1. `this` is the implicit first integer-class argument in `R0` (rather than a
   dedicated register), shifting explicit integer parameters by one.
2. Constructors and destructors return `void`; `C3` is reserved, and `D0`/`D1`/
   `D2` are emitted as needed.
3. Variadic arguments use uniform 8-byte, 8-byte-aligned stack slots.
4. Aggregate classification uses the 8-byte register threshold and the 16-byte
   FP-only return rule described above; non-trivial types are passed by
   invisible reference.
5. The vtable address point is at the vptr, with typeinfo at `vptr - 8` and
   offset-to-top at `vptr - 4`; typeinfo uses the `__class_type_info` /
   `__si_class_type_info` forms.
6. `.cpp_except` is the LSDA section name; landing pads receive the exception in
   `R0` and the selector in `R1`; the personality routine is
   `__kr32_personality_v0`.
7. TCB offsets are frozen at the table values (0/4/8/12/16/20/24/28, TLS at 32,
   160-byte header + 128-byte FPR save area) as recorded in `abi.h`; the M9.10
   thread runtime installs these fields.
8. Empty base optimization is disabled in revision 1 (an empty base occupies one
   byte at offset 0).
9. `long double` is identical to `double`, including mangling as `d`.
10. Variadic `va_list` is a simple 4-byte slot pointer with no register save
    area.

## Runtime

`_start` calls `main`, then `exit` with `main`'s return value in `R0`.
`SYSCALL #33` writes `R0` as a character; `SYSCALL #32` exits with `R0`.
`memcpy`, `memset`, `strlen`, `malloc`, `free`, `calloc`, `realloc`, and
`printf` are supplied by the runtime. The allocator is a boundary-tag,
address-ordered free-list allocator over `__heap_start..__heap_end`, with
best-fit allocation and bidirectional coalescing; see [libc.md](libc.md).

The C++ runtime adds `operator new`/`delete`, constructor/destructor array
helpers, `__cxa_atexit`, `__cxa_guard_acquire`/`__cxa_guard_release`,
`__cxa_allocate_exception`/`__cxa_throw`/`__cxa_begin_catch`/`__cxa_end_catch`,
the personality routine and unwinder, typeinfo support, thread/TLS setup, and
the matrix/FP library. These are documented with their phases in
`docs/cpp-plan.md` section 8 and `cpp-impl.md`.

## C++ readiness

The `.k32` container and linker are language-neutral: sections, symbols,
relocations, weak bindings, and the reserved COMDAT section flag are already
available. The M4 compiler emits the same assembly/object path that a C++ front
end reuses. C++ adds the layout, mangling, call, exception, TLS, and COMDAT rules
in this document; it does not change the container header or existing C object
behavior.

The matching constants live in
[`include/kr32/cxx/abi.h`](../include/kr32/cxx/abi.h) and are covered by
host-side tests in [`tests/cxx`](../tests/cxx). No code generation may depend on
an ABI rule that is not written down here.
