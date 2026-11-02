# KR32 C runtime and libc

KR32 C is freestanding. There is no host libc, no separate libc archive, and no
dynamic linking. Every C executable built with `kr32 build` receives:

1. a small assembly runtime for startup and host syscalls, and
2. a libc layer written in KR32 C and compiled by the KR32 C compiler.

The C libc source lives in `src/cc/libc.c`. At configure time CMake embeds that
file into `libc_runtime.h`, and the KR32 compiler compiles the embedded text
with the same C ABI as user code. Debug builds retain the `src/cc/libc.c`
source path and line records so the debugger can switch into libc source.

## Headers

Headers are compiler-provided shims; there are no header files in the include
search path. Each synthetic include expands to a single source line so original
source line numbers and debugger line mappings are preserved.

| Include | Contents |
| --- | --- |
| `<kr32.h>`, `<stdio.h>`, `<stdlib.h>`, `<string.h>` | Console, raw file descriptor, argument, memory, and string prototypes |
| `<stdarg.h>` | `va_list` and the `va_start`/`va_arg`/`va_end`/`va_copy` macros |
| `<stddef.h>` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` |
| `<math.h>` | `sqrt`, `sqrtf`, `fabs`, `fabsf`, `fmin`, `fminf`, `fmax`, `fmaxf`, `M_PI` |

`<stdio.h>` and `<stdlib.h>` currently expose the same prototype block. There is
no `FILE`, `EOF`, `O_RDONLY`, or stream buffering.

## Compiler extensions used by libc

- `sizeof(type)`, `sizeof expression`
- `_Alignof(type)` / `alignof(type)`
- `__builtin_offsetof(type, member)`, normally used through `offsetof`
- `_Static_assert(expression, "message")`
- `__builtin_heap_start()` and `__builtin_heap_end()` for the linker-provided
  heap bounds

## Heap and allocator

The linker defines:

- `__heap_start`: first 16-byte-aligned address after the loaded image.
- `__heap_end`: `__stack_top - 64 KiB`, clamped to at least `__heap_start`.

The allocator is a boundary-tag allocator with address-ordered free lists:

- Every block has a 16-byte header (`size`, free flag, next, previous) and a
  4-byte footer containing the block size.
- Payloads are 8-byte aligned.
- `malloc` uses a best-fit search, splits large blocks, and returns `0` when the
  request cannot be satisfied.
- `free` inserts blocks in address order and coalesces with both the next block
  and the previous block (using its footer), so contiguous free space becomes a
  single block.
- `calloc` zeroes its allocation; `realloc` copies into a larger block and frees
  the old one.
- `free(0)` is a no-op. Double-free detection is limited to ignoring an
  already-free block; there is no validation of arbitrary pointers.
- The allocator is not thread-safe; the initial libc layer targets one thread.

## String and memory functions

The runtime currently provides only three string/memory routines:

| Function | Signature | Notes |
| --- | --- | --- |
| `strlen` | `int strlen(const char *text)` | Scans to the first NUL |
| `memcpy` | `void *memcpy(void *dst, const void *src, int count)` | Non-overlapping regions; returns `dst` |
| `memset` | `void *memset(void *dst, int value, int count)` | Uses the low byte of `value`; returns `dst` |

There is no `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strcat`, `strncat`,
`strchr`, `strrchr`, `strstr`, `strtok`, `strdup`, `memmove`, `memcmp`, or
`memchr` yet. `<string.h>` is a synthetic header that exposes the three existing
prototypes. All sizes and lengths are 32-bit `int` values in the current libc.

## printf

`printf` supports:

- `%d`, `%i`, `%u`, `%o`, `%x`, `%X`, `%c`, `%s`, `%p`, `%%`
- Flags `-`, `+`, space, `#`, and `0`
- Numeric or `*` width, numeric or `*` precision
- Length modifiers are parsed and ignored (`h`, `l`, `j`, `z`, `t`, `L`)
- `%f`, `%F`, `%e`, `%E`, `%g`, `%G` for `double` varargs, including precision,
  width, signs, zero padding, exponent selection, and trailing-zero trimming for
  `%g`

`float` variadic arguments promote to `double`, as required by the C ABI.

Not yet implemented: `%a`/`%A`, `%n`, locale-specific formatting, and a full
`NaN`/infinity policy. FP output is decimal-only.

## Math and intrinsics

The ISA-backed math functions map to FP instructions:

- `sqrt`/`sqrtf` -> `FSQRT`
- `fabs`/`fabsf` -> `FABS`
- `fmin`/`fminf` -> `FMIN`
- `fmax`/`fmaxf` -> `FMAX`

Atomic and bit operations use the ISA escapes:

- `__builtin_ll(address)` -> `LL`
- `__builtin_sc(address, value)` -> `SC`; returns 1 on success
- `__builtin_fence()` -> `FENCE`
- `__builtin_bswap32(value)` -> `BSWAP`
- `__builtin_clz`, `__builtin_ctz`, `__builtin_popcount` -> `CLZ`, `CTZ`, `POPCNT`
- `__builtin_add_overflow`, `__builtin_sub_overflow`, `__builtin_mul_overflow`
  store the wrapped result through their third argument and return 1 on overflow

There is no C11 `_Atomic` type or full `__atomic_*` API yet. `LL`/`SC` remain
low-level building blocks for synchronization primitives.

## Runtime syscall wrappers

The assembly startup/runtime provides `_start`, `putchar`, `getchar`, `exit`,
`open`, `close`, `read`, `write`, `seek`, `sys_time`, `sys_random`, `sys_argc`,
`sys_arg`, `memcpy`, `memset`, and `strlen`.

`malloc`, `free`, `calloc`, `realloc`, and `printf` are implemented in C.
