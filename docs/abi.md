# KR32 ILP32 ABI (bootstrap)

This document records the ABI produced by the M4 C bootstrap and the direction
for the C++ compiler in M9.

## Data model

- `char`, `_Bool`: 1 byte.
- `int`, pointers: 4 bytes.
- Little-endian.
- Aggregates are laid out with members aligned to `min(size, 4)`; structs are
  padded to their maximum member alignment; unions place every member at offset
  zero and take the largest member size.
- Arrays are contiguous; a pointer is 4 bytes.

## Registers

| Register(s) | Role |
| --- | --- |
| `R0..R3` | First four integer arguments; `R0` is the return value |
| `R4..R6` | Caller-saved scratch; runtime helpers may clobber them |
| `R7` | Frame pointer (callee-saved) |
| `R8..R11` | Callee-saved; reserved for the wider ABI |
| `R12..R15` | Caller-saved scratch |
| `SP` | Stack pointer, separate from the GPR file |
| `F0..F15` | Scalar FP; `F0`/`F1` return values, `F0..F7` arguments, `F8..F15` callee-saved |

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

The bootstrap uses 4-byte stack alignment. Future C++ support will preserve the
same frame shape and enforce 16-byte alignment at public call boundaries.

## Calls and aggregates

- Up to four scalar arguments travel in `R0..R3`; the return value is in `R0`.
- Struct/union values are not passed or returned by value in the bootstrap; pass
  pointers instead. The object/debug model is language-neutral, so M9 can add a
  by-value aggregate convention without changing `.k32`.
- The caller evaluates arguments left-to-right and pushes each result; the
  sequence is popped into `R0..Rn-1` before `CALL`.

## Runtime

`_start` calls `main`, then `exit` with `main`'s return value in `R0`.
`SYSCALL #33` writes `R0` as a character; `SYSCALL #32` exits with `R0`.
`memcpy`, `memset`, `strlen`, `malloc`, and `free` are supplied by the runtime;
`malloc` is a simple bump allocator starting at `__heap_start`.

## C++ readiness

The `.k32` container and linker are language-neutral: sections, symbols,
relocations, weak bindings, and COMDAT flags are already available. The M4
compiler emits the same assembly/object path that a C++ front end can reuse.
A dedicated IR and the aggregate/debug-record details are intentionally reserved
for M9.
