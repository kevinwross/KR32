# KR32 C runtime and libc

KR32 C is freestanding. There is no host libc, no separate libc archive, and no
dynamic linking. Both the C and C++ front ends compile to the same `.k32`
object/executable model and link the **same** C runtime/libc: C++ `new`/`delete`
lower to `malloc`/`free`, and C++ code can include the same C headers
(`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, `<stddef.h>`,
`<stdint.h>`, `<stdbool.h>`, `<limits.h>`, `<ctype.h>`, `<assert.h>`,
`<time.h>`) through
the freestanding C++ preprocessor and call the C functions directly. Every C
executable built with `kr32 build` receives:

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
| `<kr32.h>`, `<stdio.h>`, `<stdlib.h>`, `<string.h>` | Console, file stream, raw descriptor, argument, memory, string, and numeric-conversion prototypes (`atoi`/`atol`/`atof`/`strtol`/`strtoul`), including `strspn`/`strcspn`/`strpbrk`/`strcoll`/`strxfrm`, `strnlen`/`strndup`, and `memccpy`; `<stdlib.h>` also provides `abs`/`labs`/`srand`/`rand`/`qsort`/`bsearch` |
| `<stdarg.h>` | `va_list` and the `va_start`/`va_arg`/`va_end`/`va_copy` macros |
| `<stddef.h>` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` |
| `<stdint.h>` | `int8_t` through `uint64_t`, `intptr_t`, `uintptr_t`, and limit macros |
| `<ctype.h>` | Character classification and `tolower`/`toupper` |
| `<assert.h>` | `assert` macro and assertion-failure reporting |
| `<time.h>` | `time_t`, `time`, `difftime` |
| `<stdbool.h>` | `bool`, `true`, `false` |
| `<limits.h>` | `CHAR_BIT`, `SCHAR`/`SHRT`/`INT`/`LONG` limits |
| `<string>` | compact `std::string`: construction, `c_str`, `size`, `operator[]`, `set`, `append` (C string or `std::string`), `swap`, `clear` |
| `<errno.h>` | `errno`, `strerror`, `perror`, and error constants |
| `<sys/stat.h>` | `struct stat`, `stat`, `fstat`, `remove`, `rename` |
| `<unistd.h>` | `access`, `isatty`, `truncate`, `fsync`, `fdatasync`, `getcwd`, `chdir`, `dup`, `dup2` |
| `<math.h>` | `sqrt`, `sqrtf`, `fabs`, `fabsf`, `fmin`, `fminf`, `fmax`, `fmaxf`, `M_PI` |

`<stdio.h>` provides a `FILE` layer over the descriptor syscalls:
`fopen`/`fdopen`/`freopen`/`fclose`, `fread`/`fwrite`, `fgetc`/`fputc`,
`ungetc`, `fgets`/`fputs`/`puts`, `fprintf`/`fscanf`/`scanf`/`sscanf`,
`fseek`/`ftell`/`rewind`/`fgetpos`/`fsetpos`, `feof`/`ferror`/`clearerr`,
`fileno`, `getline`/`getdelim`, `tmpfile`, `snprintf`/`sprintf`, and the
`vprintf`/`vsnprintf`/`vsprintf`/`vfprintf` va_list variants. `stdin`, `stdout`,
and `stderr` map to the console syscalls; file streams use descriptors 3 and
above. Streams are unbuffered by default; `setvbuf`/`setbuf` can install full
or line output buffering and block input buffering. `fflush`/`fclose`/`fseek`/
`ftell` flush output, and seeks reset the input buffer.

`<errno.h>` provides `errno`, `strerror`, and `perror`. `<sys/stat.h>` provides
`struct stat`, `stat`, `fstat`, `remove`, and `rename`; the host operations are
sandboxed just like `open`.

## Compiler extensions used by libc

- `sizeof(type)`, `sizeof expression`
- `_Alignof(type)` / `alignof(type)`
- `__builtin_offsetof(type, member)`, normally used through `offsetof`
- `_Static_assert(expression, "message")`
- Function-scope `static` locals with persistent data/BSS storage and constant
  initializers, including arrays and structs
- Function-pointer typedefs (`typedef int (*fn)(int);`) with indirect calls,
  pointer parameters, returns, assignment, global/`&`-address initializers, and
  direct `int (*fn)(int)` declarators, array-element calls, and calls with more than four fixed arguments
- Aggregate local initializers and assignment from struct-returning calls and
  string literals
- Compound literals such as `(struct Point){1, 2}`
- Anonymous struct and union members promoted into the enclosing type
- Variadic function-like macros using `__VA_ARGS__`
- Parenthesized comma expressions
- Bit-fields with packed layout, masking, and sign extension
- `__builtin_expect`, `__builtin_constant_p`, `__builtin_unreachable`, and `__extension__`
- Struct/union parameters copied into callee locals, preserving by-value semantics
- `__func__`, `__LINE__`, `__FILE__`, `__DATE__`, `__TIME__`, `__STDC__`,
  `__STDC_HOSTED__`, and `__STDC_VERSION__`
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

The allocator exports two globals for debugger inspection:

- `heap_allocated_blocks` — number of live (allocated) blocks.
- `heap_free_blocks` — number of blocks currently on the free list.

They are updated on allocation, split, free, and coalesce operations. Both are
visible in the GUI **VARIABLES** panel and in `kr32 debug` output.

`int _heapwalk(void)` validates the physical block chain and address-ordered
free list, checks every header/footer and free-list link, then recomputes the
two counters from the validated blocks. It is declared by `<stdlib.h>` and
returns:

- `0` — heap is consistent; counters were resynchronized.
- `1` — heap is unavailable or too small to initialize.
- `2` — physical block chain/header failure.
- `3` — block footer mismatch.
- `4` — free-list link or membership failure.
- `5` — free-list count does not match the physical free blocks.

It is a detector, not a repair tool: corrupt links are reported, not rewritten.
Like the allocator, it is not thread-safe.

## String and memory functions

The runtime provides:

| Function | Signature |
| --- | --- |
| `strlen` | `int strlen(const char *text)` |
| `memcpy` | `void *memcpy(void *dst, const void *src, int count)` |
| `memset` | `void *memset(void *dst, int value, int count)` |
| `strcpy` | `char *strcpy(char *dst, const char *src)` |
| `strncpy` | `char *strncpy(char *dst, const char *src, int count)` |
| `strcat` | `char *strcat(char *dst, const char *src)` |
| `strncat` | `char *strncat(char *dst, const char *src, int count)` |
| `strcmp` | `int strcmp(const char *left, const char *right)` |
| `strncmp` | `int strncmp(const char *left, const char *right, int count)` |
| `strchr` | `char *strchr(const char *text, int character)` |
| `strrchr` | `char *strrchr(const char *text, int character)` |
| `strstr` | `char *strstr(const char *text, const char *needle)` |
| `strtok` | `char *strtok(char *text, const char *delimiters)` |
| `strdup` | `char *strdup(const char *text)` |
| `memmove` | `void *memmove(void *dst, const void *src, int count)` |
| `memcmp` | `int memcmp(const void *left, const void *right, int count)` |
| `memchr` | `void *memchr(const void *memory, int character, int count)` |

`memcpy` requires non-overlapping regions; use `memmove` when they may overlap.
`strtok` uses a single static cursor and has a reentrant `strtok_r` variant and is not reentrant or thread-safe.
`strdup` allocates with `malloc`; free its result with `free`. All lengths and
counts are 32-bit `int` values. `<string.h>` exposes the complete set.

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
`sys_arg`, `sys_file_dialog`, `sys_open_dialog`, `sys_save_dialog`, `memcpy`,
`memset`, and `strlen`.

The program loader builds a conventional `argv` array on the guest stack before
entry and places `argc` in `R0` and `argv` in `R1`; `_start` forwards those
registers to `main`. `argv[0]` is the loader-provided program label (`program`
for the CLI default, or the loaded source filename in the GUI); host `--arg`
values follow as `argv[1..]`. Programs may also use `sys_argc`/`sys_arg`
directly.

`malloc`, `free`, `calloc`, `realloc`, and `printf` are implemented in C.

C++ programs can include `<stdio.h>` and use the same `FILE` API. They can also
include `<iostream>` or `<fstream>` for a compact stream subset:
`std::ifstream`, `std::ofstream`, `std::fstream`, `std::cin`, `std::cout`,
`std::cerr`, `operator<<`, `operator>>`, `std::endl`, `read`, `write`, `get`,
`getline`, `seekg`/`tellg`, `seekp`/`tellp`, `hex`/`dec`/`oct`, `width`, and
`precision`. Standard `std::hex`, `std::dec`, and `std::oct` manipulators are
available. These streams are unbuffered
wrappers over `FILE`. The compiler currently supports member binary operator
overloads (including class-by-value parameters), member `operator[]`, compound assignment operators, and simple
unary operators, postfix increment/decrement, `operator->`, and conversion
operators such as `operator bool` and `operator int`, and converting constructors (with `explicit` respected), including conditions,
casts (including class temporary construction), copy initialization, scalar arguments, returns, scalar
equality/relational comparisons, and integer arithmetic, including reversed
operands. Free binary operators are
supported; member `operator()` supports multiple arguments, and functions and
members and constructors may declare default arguments; `alignas` and
`alignof` are supported. Full standard
iostreams, locales, and facets are not fully implemented; `std::string` is a
compact subset with `operator=` (C string, character, or `std::string`), `operator+=` (C
string, character, or `std::string`), `operator+` (string/string, string/C
string, C string/string, string/character, character/string), `operator==`, `operator!=`, relational operators,
`find`/`rfind`/`find_first_of`/`find_last_of`/`find_first_not_of`/
`find_last_not_of`/`compare` (C string,
character, or `std::string`), with `find`/`rfind`/`find_first_of`/
`find_last_of` accepting an optional start position and `compare` accepting
a substring position/count; `std::string::npos` is available. `starts_with`/`ends_with`,
`at`, `substr` (with or without
count), `front`, `back`, `replace`/`insert` (C string, `std::string`,
character, or count+character), `erase` (position or range), `resize` (count or count+character), `push_back`,
`pop_back`, `assign` (C string or prefix count), `append` (string, C string,
prefix count, or count+character), `swap`, `begin`/`end`, `data`, and `std::to_string`.
