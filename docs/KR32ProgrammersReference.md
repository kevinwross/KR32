# KR32 Programmer's Reference

This document is the single entry point for writing KR32 programs: the tools
that turn source into an image, the `kr32` executable that runs it, the machine
a program sees, and a reference for every implemented instruction.

**ISA revision:** 0.2-draft (`kr32 version` reports it). Encodings in this
document are the ones the emulator decodes and the assembler emits; where the
draft in [isa-v1.md](isa-v1.md) and the implementation disagree, this document
describes the implementation and the disagreement is called out.

**Tool version:** the `kr32` executable reports
`kr32 0.1.0 (ISA 0.2-draft, container 1, M7 host services)`.

Related documents, each the owner of its subject:

| Subject | Owner |
| --- | --- |
| Instruction encodings and the machine memory map, as designed | [isa-v1.md](isa-v1.md) |
| Guest calling convention, structures, threads | [abi.md](abi.md) |
| `.k32` object and executable format | [container.md](container.md) |
| Devices, timer, block, console, framebuffer, keyboard, terminal, graphics, IRQs | [devices.md](devices.md) |
| Host syscalls, file sandbox, shell, DAP | [host.md](host.md) |
| Debug records and the debugger | [debug.md](debug.md), [ui.md](ui.md) |
| C and C++ compilers, libc, templates | [libc.md](libc.md), [templates.md](templates.md), [roadmap.md](roadmap.md) |
| Running and debugging externally built images | [HowToDebugUsingKR32.md](../HowToDebugUsingKR32.md) |

## Contents

1. [The toolchain](#1-the-toolchain)
2. [Running a program](#2-running-a-program)
3. [The assembler](#3-the-assembler)
4. [Linking, C, and C++](#4-linking-c-and-c)
5. [Architecture](#5-architecture)
6. [Flags and condition codes](#6-flags-and-condition-codes)
7. [Exceptions, interrupts, and control registers](#7-exceptions-interrupts-and-control-registers)
8. [Memory map](#8-memory-map)
9. [Calling convention](#9-calling-convention)
10. [Host services](#10-host-services)
11. [Programming patterns](#11-programming-patterns)
12. [Instruction reference](#12-instruction-reference)

---

## 1. The toolchain

KR32 ships as one executable, `kr32.exe` on Windows and `kr32` on Linux, that
contains the assembler, linker, C and C++ compilers, disassembler, debugger,
DAP server, shell, emulator, and the embedded KR32OS images. Nothing else is
required to build and run a KR32 program; SDL2 is needed only by the windowed
debugger.

### 1.1 Building the executable

The presets are the only supported build commands. Windows drops the result at
`build\windows-msvc\<config>\kr32.exe`; Linux at `build/<preset>/kr32`.

```powershell
# Windows (CMake and ctest are not on PATH in a plain PowerShell)
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake --preset windows-msvc
& $cmake --build --preset windows-msvc-debug
```

```sh
# WSL / Linux
cmake --preset linux-gcc && cmake --build --preset linux-gcc
```

`KR32_BUILD_UI=OFF` builds the CLI without SDL2; every command in this document
except `gui` and `display` still works.

### 1.2 Command summary

| Command | Purpose |
| --- | --- |
| `version` | print the version tuple |
| `help` | print the command list |
| `as <file.asm>` | assemble a flat image, or a `.k32` object with `--object` |
| `ld <objects...>` | link `.k32` objects into an executable, or a flat image with `--flat` |
| `build <file>` | compile/assemble, link, and optionally run in one command |
| `cc <file.c>` | compile C to assembly (`-S`) or an object |
| `cxx <file.cpp>` | compile C++ to assembly (`-S`) or an object |
| `vm <file>` / `run <file>` | assemble-or-load and execute an image |
| `os [kernel]` | boot KR32OS with the default BIOS |
| `dis <image>` | disassemble a flat image |
| `size <image.k32>` | print section sizes, entry, and base |
| `debug <image.k32>` | dump debug records |
| `gui [file]` | windowed source/disassembly debugger (UI build only) |
| `display` | framebuffer window (UI build only) |
| `dap [file]` | Debug Adapter Protocol server |
| `shell` | interactive shell over the same commands |

`as`, `dis`, `ld`, `build`, `cc`, `cxx`, `vm`, and `dap` accept `--help` and
print their own usage line; `size`, `debug`, and `shell` print usage when their
arguments are wrong. Exit codes are `0` for success, `1` for a tool or guest
failure, and `2` for a usage error; `vm`, `run`, and `build --run` instead
return the guest exit code, so a guest that exits with `1` is
indistinguishable from a failed run by exit code alone.

### 1.3 Command pipeline

```text
source              tool          output
.s / .asm           as            flat .bin
.s / .asm           as --object   .k32 object
.c                  cc -S         .s
.c                  cc            .k32 object
.cpp                cxx -S        .s
.cpp                cxx           .k32 object
.k32 objects        ld            .k32 executable, or a flat .bin with --flat
.s / .c / .cpp      build         .k32 executable, and runs it with --run
```

`as` without `--object` writes a headerless image: byte zero is the first byte
loaded, and every address in the source is absolute. `as --object`, `cc`, and
`cxx` write relocatable `.k32` objects; `ld` places sections at `--base` and
resolves symbols. `vm` accepts either a raw image or a `.k32` executable.

---

## 2. Running a program

### 2.1 Invoking `kr32.exe`

```powershell
$KR32 = "C:\Users\kevin\repos\kr32\build\windows-msvc\Debug\kr32.exe"
& $KR32 version
& $KR32 run examples\demo.asm
& $KR32 build examples\hello.s -o hello.k32 --run
```

```sh
./build/linux-gcc/kr32 run examples/demo.asm
```

Programs are run by extension:

- `.asm` and `.s` are assembled in memory (raw path) and then executed. This is
  the raw assembler, not the object assembler; see §3.6.
- `.k32` is read as a container. An executable is loaded into RAM at its linked
  addresses, `PC` is set to its entry point, and `.bss` is zeroed. A relocatable
  object is rejected: `input is a relocatable object, not an executable`.
- Anything else is loaded as a flat image at `--base` (default `0x0001_0000`)
  and `PC` is set to `--base`. The file must not be named `.k32`.

### 2.2 Startup state

Before the first instruction the runner establishes the following, for flat
images and `.k32` executables alike:

| Item | Value |
| --- | --- |
| `PC` | `--base` for a flat image, the container entry for a `.k32` |
| `SP` | top of RAM, 16-byte aligned, minus the `argc`/`argv` block |
| `R0` | `argc`, including the implicit `"program"` name |
| `R1` | `argv`, an array of `argc` pointers followed by a null pointer |
| `FLAGS` | `K` set, `I` clear — the program starts in kernel mode with interrupts off |
| `F0..F15`, `T0..T15`, `R2..R15` | zero |
| paging | off |
| `TSP` | zero (the first `MPUSH` faults until `WRSP` installs a stack) |

The pointer array begins at `SP` and the strings follow it at higher addresses,
all below the original stack top. A program that does not want the arguments can
ignore `R0`/`R1` and use `SP` normally. `SYSCALL #42` and `#43` return only the
explicit `--arg` values and exclude `"program"`.

Because paging is off and `FLAGS.K` is set, a flat image runs as a flat machine
test, not as a KR32OS user process. It can use `HALT`, `RDCR`, and `WRCR`
without a kernel, but it cannot reach the device window until it programs the
page tables itself (§8.4).

### 2.3 Output

A run prints guest output as it is produced, then a fixed trailer:

```text
Hello, KR32!
R0=0 R1=65597 R2=0 R3=0 R4=0 R5=0 R6=0 R7=0
R8=0 R9=0 R10=0 R11=0 R12=0 R13=0 R14=0 R15=0
PC=0x00010030 SP=0x0800FFF0 FLAGS=0x00000025 ticks=84
HALTED exit=0
```

| Line | Meaning |
| --- | --- |
| `HALTED exit=N` | the core halted; `N` is the exit code |
| `ticks=N` | the tick count also appears on the `PC=` line; `--stats` repeats it as a line after `HALTED` |
| `LIMIT after N steps` | `--max-steps` was reached without a halt; the run returns `1` |

The exit code is the value passed to `SYSCALL #32` if one was made, otherwise
`R0 & 0xFF` at `HALT`.

Faults are delivered through the vector table, so a fault does not terminate a
run that has a handler. A program with no vector table (`VTBR` is zero, so the
table is unmapped) cannot deliver the fault, fails to deliver the double fault,
and halts the core with `HALTED exit=1`; the trailer's `PC` is the faulting
instruction. Run such a program with `--trace` to see the last instruction that
executed. The `FAULT ...` line exists for a machine configured with exception
delivery disabled, which the CLI does not do.

### 2.4 `vm` options

| Option | Meaning |
| --- | --- |
| `--base <addr>` | load address and initial `PC` for a flat image |
| `--bios <rom>` | load a flat BIOS into ROM and reset to it instead of running the image |
| `--os [kernel]` | boot the OS path (§2.5) |
| `--embedded` | with `--os`, use the images built into `kr32.exe` |
| `--ram-size <bytes>` | RAM size, default `0x0800_0000` (128 MiB) |
| `--max-steps <n>` | instruction budget, default `1_000_000` |
| `--trace` | print `0xPC: 0xWORD` before each instruction |
| `--stats` | print the tick count after `HALTED` |
| `--fs-root <dir>` | sandbox root for guest file paths |
| `--read-only` | reject guest writes and write-opens |
| `--quota <bytes>` | cap total bytes written through the host |
| `--arg <value>` | add one guest argument (repeatable) |
| `--input <text>` | text `SYSCALL #34` consumes, then `-1` |
| `--serial-input <text>` | bytes preloaded into the OS serial device |

`--base` does not move the stack; the stack is always at the top of RAM. Keep
the image well below `RAM base + RAM size`.

### 2.5 Booting KR32OS

```powershell
& $KR32 os                        # embedded BIOS and kernel
& $KR32 os --embedded             # same, stated explicitly
& $KR32 os kr32os\build\windows\kernel.k32 --bios kr32os\build\windows\bios.k32
& $KR32 gui --os
```

`os` loads the kernel at `0x0002_0000` and the BIOS into ROM at `0xFFFF_0000`,
resets the core, writes the boot-info block at `0x0003_F000`, and starts the
BIOS with `R2` holding the block address. Kernel and BIOS must both be raw
`.bin` or both be `.k32`. Default images are resolved from `KR32OS_BIN` /
`KR32OS_BIOS`, then `kr32os/build/<platform>/`, then the embedded pair.

### 2.6 Debugging front ends

`kr32 gui <file.k32>` opens the windowed debugger; `kr32 gui --headless
--screenshot out.ppm <file.k32>` renders one frame without a window.
`kr32 dap --stdio <file.k32>` or `kr32 dap --port 4711 <file.k32>` serves the
Debug Adapter Protocol. Both require a `.k32` executable: a raw image has no
section or debug records to load. See [HowToDebugUsingKR32.md](../HowToDebugUsingKR32.md)
and [ui.md](ui.md).

---

## 3. The assembler

### 3.1 Source format

- One statement per line: `[label:] [mnemonic [operands]]`.
- Labels end with `:` and may share a line with an instruction. A label
  character set is letters, digits, `_`, and `.`; it must not start with a digit.
- Comments start at `;` and run to end of line. `#` is **not** a comment: it
  prefixes an immediate.
- Mnemonics and register names are case-insensitive; symbols are
  case-sensitive.
- Operands are separated by commas. `[` `]` group a memory operand.
- Blank lines and label-only lines are allowed.

### 3.2 Numbers and expressions

- Decimal (`42`), hexadecimal (`0x2A`), and octal: `strtoll` base 0 is used, so
  a leading zero means octal. `#010` is 8, not 10. Write `#10` or `#0x0A`.
- An immediate may be written with or without `#`; `MOV R0, 42` and
  `MOV R0, #42` assemble to the same word. `#` is the convention.
- An expression is a sum of terms joined by `+` and `-`: numbers, symbols, and
  parenthesised-looking forms are not supported. There is no `*`, `/`, `<<`,
  or `(` `)`. `label + 4`, `end - start`, and `-4` work.
- Symbols are labels, `.equ`/`.set` names, and object-file globals. A reference
  to an undefined symbol is an error at assembly time (raw) or link time
  (object), reported as `undefined symbol: <name>`.

### 3.3 Operand forms

| Form | Example | Meaning |
| --- | --- | --- |
| register | `R7` | general register `R0..R15` |
| FP register | `F3` | `F0..F15` |
| tile register | `T2` | `T0..T15` |
| control register | `CR10` | `CR0..CR15`, only for `RDCR`/`WRCR` |
| immediate | `#42`, `#-16`, `#0x10000` | 11-bit inline, or a 32-bit extension word |
| register indirect | `[R1]` | address in `R1` |
| base + displacement | `[R1 + 8]`, `[R1 - 4]` | `R1 + disp` |
| base + index | `[R1 + R2]`, `[R1 + R2 + 8]` | `R1 + R2 + disp` |
| stack-relative | `[SP]`, `[SP + 12]` | `SP + disp` |
| absolute | `[0x00010000]`, `[message]` | displacement as the address |
| PC-relative | `[PC]`, `[PC + 4]`, `[PC + label]` | instruction address + disp |
| branch target | `label`, `0x00400000`, `R5` | literal target or register target |

`SP` and `PC` are only usable inside `[]`. `MOV R0, SP` is an error
(`undefined symbol: SP`); use `LEA R0, [SP]`. Likewise `LEA R0, [PC]` reads the
address of the `LEA` itself, and `LEA R0, [PC + 4]` the following instruction.

`[label]` is **absolute**, not PC-relative: it becomes `[0x00010000]`-style
encoding. Write `[PC + label]` when position independence matters.

### 3.4 Encoding size

| Source form | Size |
| --- | --- |
| immediate in `-1024..1023` with no symbol | 4 bytes |
| immediate outside that range, or any expression containing a symbol | 8 bytes (extension word) |
| literal branch and call target | always 8 bytes |
| memory displacement in `-1024..1023` with no symbol | 4 bytes |
| memory displacement outside that range, or containing a symbol | 8 bytes |
| everything else | 4 bytes |

Size is fixed by the shape of the operand, not measured by distance: every
literal branch target costs 8 bytes even when the label is the next
instruction, and every symbolic immediate costs 8 bytes even when the address
is small. Only a plain number that fits 11 bits gets the short form. There is
no directive to force either form.

### 3.5 Traps

- `[R1 - R2]` assembles as `[R1 + R2]`: a sign on the index register is ignored.
  Subtract the index into a scratch register instead.
- `0x`-prefixed decimal-looking literals are hex; `0x10000` is 65536, while
  `010000` is octal. Use `0x` consistently.
- `.align N` aligns to a byte boundary, not a power of two: `.align 16` pads to
  a 16-byte boundary. `.p2align` is accepted as a synonym and does **not** take
  an exponent.
- `.float` and `.double` take decimal values (`1.5`, `0.5`, `1e3`, optional
  `f`/`l` suffix). A hex literal such as `0x3F800000` is read as the number
  1065353216 and converted, not as those bits; use `.word 0x3F800000` for a
  specific encoding.
- `[R1 + 0]` assembles as base+displacement with a zero displacement (mode 3),
  not as register indirect (mode 2). The two encode differently (`LOAD R1,
  [R2 + 0]` is `0x02246800`, `LOAD R1, [R2]` is `0x02644800`) and the
  disassembler prints both as `[R2]`, so a listing hides the difference. It
  matters for the strided `MLOAD`/`MSTORE` form (§12.11).

### 3.6 Two assembler paths

The repository contains two assemblers over the same encoding. They do not
accept the same source.

| Feature | `as` (raw) | `as --object`, `build`, `ld` inputs |
| --- | --- | --- |
| `ROL`, `ROR` | yes | no — `unsupported instruction: rol` |
| Base + index `[Rs + Ri]` | yes | no — the index is taken as a symbol and the link fails with `undefined symbol: Rk` |
| `RDSP`, `WRSP` | yes | no — `unsupported instruction: rdsp` |
| `.section`, `.rodata`, `.bss`, `.tls`, `.init_array`, `.fini_array`, `.eh_frame_array`, `.cpp_except`, `.debug_info` | no | yes |
| `.globl`, `.string` | no | yes |
| `.p2align`, `.type`, `.size` | yes (`.type`/`.size` are ignored) | no |
| `.org` | absolute address | section-relative offset |
| relocations, weak symbols, COMDAT | no | yes |
| `kr32 run file.asm` | yes | — |

Practically: `kr32 run file.asm` and `kr32 as file.asm` — and any `vm`/`run`
whose input has a `.s` or `.asm` extension — go through the raw assembler,
which accepts the whole instruction set. `kr32 build file.s`, `kr32 as file.s
--object`, and `kr32 ld` go through the object assembler, which is the path the
C and C++ compilers use. A program that needs `ROL`, `ROR`, base+index
addressing, or `RDSP`/`WRSP` has to be built as a raw image with `as` and run
with `vm`.

### 3.7 Directives

Raw path:

| Directive | Effect |
| --- | --- |
| `.text`, `.data` | accepted, no effect on a flat image |
| `.global`, `.weak`, `.extern`, `.file`, `.type`, `.size` | accepted, ignored |
| `.equ NAME, VALUE`, `.set NAME, VALUE` | define an assembly-time constant |
| `.org ADDRESS` | move forward to an absolute address; spaces are zero-filled |
| `.align N`, `.balign N`, `.p2align N` | pad forward to an `N`-byte boundary |
| `.space N`, `.zero N` | emit `N` zero bytes |
| `.byte v, ...` | one or more 8-bit values |
| `.word v, ...` | one or more 32-bit values; symbols resolve to absolute addresses |
| `.long v, ...` | the same as `.word`: C's `long` is 32 bits in this ABI |
| `.float v, ...` | one binary32 per value |
| `.double v, ...` | one binary64 per value (two little-endian words) |
| `.asciz "s", ...` | bytes plus a terminating zero, per string |
| `.ascii "s", ...` | bytes only |

Object path adds:

| Directive | Effect |
| --- | --- |
| `.section NAME` or one of the known section directives | switch the current section |
| `.section .text.<name>`, `.section .data.<name>`, `.section .bss.<name>` | a COMDAT section for that name; the dotted name is the argument to `.section`, not a directive of its own |
| `.global` / `.globl` / `.weak` | export a symbol, with the given binding |
| `.extern NAME` | mark a symbol as imported |
| `.string "s"` | bytes plus a terminating zero |
| `.org OFFSET` | move forward within the current section |

String escapes are `\n`, `\r`, `\t`, `\0`, `\\`, `\"`; any other escaped
character stands for itself.

Data directives are **packed**: `.double` writes eight bytes at the current
offset and pads nothing, so put `.align 8` before a `double` that follows
smaller data. The section carries the guarantee instead: an object declares
`.rodata`, `.data`, and `.bss` as 8-byte aligned and the linker honours it, so a
`double` at the start of one of them is aligned. `.text` and the other data
sections declare 4. In a raw image the offset is absolute from the image base,
which is itself 8-byte aligned.

### 3.8 Example

```asm
; hello.asm - write "Hello, KR32!\n" through the host putchar service
        MOV     R1, #message
loop:
        LOAD8U  R0, [R1]
        CMP     R0, #0
        JZ      done
        SYSCALL #33
        ADD     R1, R1, #1
        JMP     loop
done:
        MOV     R0, #0
        SYSCALL #32
message:
        .asciz  "Hello, KR32!\n"
```

```text
$ kr32 run hello.asm
Hello, KR32!
R0=0 R1=65597 R2=0 R3=0 R4=0 R5=0 R6=0 R7=0
R8=0 R9=0 R10=0 R11=0 R12=0 R13=0 R14=0 R15=0
PC=0x00010030 SP=0x0800FFF0 FLAGS=0x00000025 ticks=84
HALTED exit=0
```

`FLAGS = 0x25` is `Z | K`: `Z` is left over from the `CMP` that ended the loop,
and `K` marks kernel mode.

---

### 3.9 Aliases

An alias is an assembler convenience that maps to exactly one real encoding.
Nothing in this section adds an opcode; the ISA document is authoritative for
every encoding named here.

**Branch condition aliases.** Each is `JCOND` (`0x39`) with the condition in the
16..13 field.

| Alias | Condition | Meaning |
| --- | --- | --- |
| `JZ` | 0 | equal / zero |
| `JNZ`, `JNE` | 1 | not equal |
| `JCS` | 2 | carry set |
| `JNC` | 3 | carry clear |
| `JMI` | 4 | minus |
| `JPL` | 5 | plus |
| `JVS` | 6 | overflow set |
| `JVC` | 7 | overflow clear |
| `JHI` | 8 | unsigned higher |
| `JLS` | 9 | unsigned lower or same |
| `JGE` | 10 | signed greater or equal |
| `JLT` | 11 | signed less than |
| `JGT` | 12 | signed greater than |
| `JLE` | 13 | signed less or equal |

Conditions 14 and 15 are reserved for `JCOND` itself and have no mnemonic:
`JCOND` is not accepted as a mnemonic either, so only the aliases above reach the
instruction.

**Vector aliases.** `VLOAD` and `VSTORE` are `MLOAD` (`0x52`) and `MSTORE`
(`0x53`) with `L = 1`, which selects the 16-byte vector form; with `L = 0` the
same opcodes move a 64-byte matrix. The strided form that takes a third operand
is matrix-only, so `VLOAD`/`VSTORE` reject it.

**Not aliases.** `NOP`, `NOT` (`0x2D`), `NEG` (`0x2E`), `RET`, `HALT`, `BRK`, and
`FENCE` are instructions in their own right, as are `RDCR`/`WRCR` and
`RDSP`/`WRSP`. `MOV`, `LOAD`, and `STORE` are instructions, not extensions of
anything.

**Directive aliases.**

| Alias | Same as | Notes |
| --- | --- | --- |
| `.globl` | `.global` | `.weak` is a different binding, not an alias |
| `.set` | `.equ` | |
| `.zero` | `.space` | |
| `.balign` | `.align` | both take a byte count; neither is a power of two |
| `.string` | `.asciz` | bytes plus a terminator |

Two directives look like aliases but are not. `.ascii` omits the terminator that
`.asciz` and `.string` add. `.p2align N` is accepted by the raw path only and
takes a power of two — it aligns to `2^N` — so a source that uses it is raw-only.
## 4. Linking, C, and C++

### 4.1 Objects and the linker

```powershell
& $KR32 as hello.s --object -o hello.o
& $KR32 ld hello.o -o hello.k32 --map hello.map
& $KR32 size hello.k32
& $KR32 vm hello.k32
```

| `ld` option | Meaning |
| --- | --- |
| `-o <path>` | output path; default `a.k32`, or `a.bin` with `--flat` |
| `--map <path>` | write the section and symbol map |
| `--sym <path>` | write a symbol record alongside a `--flat` image; requires `--flat` |
| `--base <addr>` | link base, default `0x0001_0000` |
| `--stack-top`, `--stack-limit` | values published as `__stack_top` / `__stack_limit` |
| `--flat` | write a headerless image instead of a `.k32` |
| `--strip` | drop local symbols and relocations |

The entry point is the global symbol `_start` if one is defined, otherwise the
lowest `.text` address. The linker also defines `__heap_start`, `__heap_end`,
`__stack_top`, `__stack_limit`, `__init_array_start`/`_end`,
`__fini_array_start`/`_end`, `__eh_frame_array_start`/`_end`, and
`__kr32_tls_start`/`_end`.

`ld --flat` produces an image whose byte zero corresponds to `--base`, with
gaps and `.bss` zero-filled; that image is what `vm --base <base>` expects.

### 4.2 C and C++

```powershell
& $KR32 cc examples\hello.c -S -o hello.s        # assembly
& $KR32 cc examples\hello.c -o hello.o           # object
& $KR32 cxx examples\hello.cpp -S -o hello-cxx.s
& $KR32 build examples\hello.c -o hello.k32 --run --arg alpha
& $KR32 build examples\hello.cpp -o hello-cxx.k32 --run
```

`build` detects the language from the extension, compiles C or C++, assembles
the result with the object assembler, links the shared C/C++ libc runtime
object, writes a `.k32`, and runs it with `--run`. `-O1` enables the peephole
passes in [optimizations.md](optimizations.md); it is the default, and `-O0`
disables them.

Compiled programs link against the freestanding runtime, not a host libc:
`printf`, `malloc`, `free`, string routines, `operator new`/`delete`,
exceptions, and threads are KR32 code running on KR32 instructions
([libc.md](libc.md)).

### 4.3 Program arguments

```powershell
& $KR32 build examples\mainentry.c --run --arg one --arg "two words"
```

Entry sees `R0 = argc`, `R1 = argv` including `"program"`. Inside the program,
`SYSCALL #42` returns the number of `--arg` values and `SYSCALL #43` copies one
into a buffer ([host.md](host.md)).

---

## 5. Architecture

### 5.1 Machine state

| State | Width | Count | Notes |
| --- | --- | --- | --- |
| `R0..R15` | 32 | 16 | general registers; no hard-wired zero register |
| `F0..F15` | 64 | 16 | scalar FP registers; `float` in bits 31..0, `double` in all 64 |
| `T0..T15` | 512 (4x4 `f32`) | 16 | row-major tiles, 16 floats each |
| `PC` | 32 | 1 | address of the next instruction; must be 4-byte aligned |
| `SP` | 32 | 1 | stack pointer, grows down |
| `TSP` | 32 | 1 | transform stack pointer, grows down, 64-byte aligned |
| `FLAGS` | 32 | 1 | status and mode bits (§6) |
| `CR0..CR15` | 32 | 16 | control registers, kernel access only (§7.3) |

`SP` is not a general register. Read it with `LEA Rd, [SP]`; write it with
`SETSP Rs` or `ADJSP #imm`. `PC` is readable only as `LEA Rd, [PC + disp]` and
writable only by a branch. `TSP` is read and written with `RDSP`/`WRSP` and is
user-accessible.

Registers are not implicitly preserved. A callee saves what it uses (§9).

### 5.2 Datapath

The instructions fall into five execution classes:

| Class | Data path | Instructions |
| --- | --- | --- |
| Integer ALU | `Rd ← f(Rs1, x)` where `x` is `Rs2` or an immediate | arithmetic, logic, shifts, `SLT`, `SLTU` |
| Memory | `Rd ↔ mem[ea]` | `LOAD*`, `STORE*`, `LEA`, `LL`, `SC` |
| Control | `PC ← target` | `JMP`, `JCOND`, `CALL`, `RET`, `IRET`, `HALT` |
| FP | `Fd ← f(Fs1, Fs2)` | the `0x60` escape |
| Tile | `Td ← f(Ta, Tb)` | the `0x61` escape |

The tile unit operates on one 4x4 `f32` tile at a time: four rows of four
floats, row-major, 64 bytes. `MMUL`/`MMAC`/`MVEC` accumulate with fused
multiply-add and round to nearest even.

### 5.3 Instruction encoding

Instructions are 4 or 8 bytes, little-endian, and one word is always the base
word:

| Bits | Field | Meaning |
| --- | --- | --- |
| 31..25 | opcode | 7 bits, §12.15 |
| 24..21 | `Rd` | destination, or the data register for stores |
| 20..17 | `Rs1` | first source, base register, or branch register |
| 16..13 | field | second source register, addressing mode, condition, or width-1 |
| 12 | `E` | a 32-bit extension word follows; the instruction is 8 bytes |
| 11 | `L` | the payload is a literal, not a register number |
| 10..0 | inline | signed 11-bit payload |

Rules:

1. `E = 1` makes the instruction 8 bytes and the next word the payload. The
   inline field is ignored, except that `MLOAD`/`MSTORE` require it to be zero.
2. `E = 0` makes the payload the inline field sign-extended from 11 bits.
3. `L = 1` means the payload is an immediate, address, or displacement.
   `L = 0` means bits 16..13 name a register. `FLOAD`/`FSTORE` and
   `MLOAD`/`MSTORE` reuse `L` as a width selector.
4. At most one extension word; chained extensions do not exist.
5. A reserved opcode, or a reserved addressing mode for a memory instruction,
   raises `#UD`. It is never executed as a no-op.
6. A branch target that is not 4-byte aligned raises `#ALIGN`.

The payload as the machine sees it (`payload()` in `machine.cpp`) is the
extension word when `E = 1` and the sign-extended inline field otherwise. A
displacement or immediate therefore has the same meaning in both forms; only
its width changes.

### 5.4 Execution model

One step:

1. If the core is halted, stop.
2. If `FLAGS.I` is set and a pending line matches `IRQ & ~IMR`, deliver the
   interrupt for the lowest such line and continue with the next step.
3. If `PC` is not 4-byte aligned, raise `#ALIGN`.
4. Fetch the base word at `PC`; if `E` is set, fetch the next word too. A fetch
   fault is delivered.
5. Decode. An invalid opcode raises `#UD` with `PC` unchanged.
6. Execute. A retiring instruction sets `PC` to the next instruction, `next_pc`
   or a branch target.
7. Count one tick per retiring or halting instruction; tick attached devices.

`ticks` in the trailer is that count. It is deterministic: the same program
takes the same number of ticks on every host.

There is no instruction cache, no branch predictor, and no speculative state, so
a fault is precise: memory faults leave `PC` at the faulting instruction, and
the saved frame for a fault points at the instruction to retry.

### 5.5 Endianness and data

The machine is little-endian. A 32-bit load is `b0 | b1<<8 | b2<<16 | b3<<24`.
Byte and halfword loads zero- or sign-extend into the full 32-bit register.
`float` occupies the low 32 bits of an FPR with bits 63..32 cleared; `double`
uses all 64 bits. `FLOAD.W`, `FCVT.S.*`, and `FMV.W.X` clear the upper half.

---

## 6. Flags and condition codes

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `Z` | the result was zero |
| 1 | `N` | result bit 31 is set |
| 2 | `C` | carry out, or "no borrow" for a subtraction |
| 3 | `V` | signed overflow |
| 4 | `I` | interrupts enabled |
| 5 | `K` | kernel mode |
| 6..31 | — | reserved; read as zero, ignored on write |

`MODE` is `FLAGS.K` and `IE` is `FLAGS.I`; there is one copy. `C` on subtraction
means *no borrow* (`left >= right`), so `JCS` after `CMP` means "unsigned
greater or equal".

| Code | Mnemonic | Name | Test |
| --- | --- | --- | --- |
| 0 | `JZ` | equal / zero | `Z = 1` |
| 1 | `JNZ`, `JNE` | not equal | `Z = 0` |
| 2 | `JCS` | carry set | `C = 1` |
| 3 | `JNC` | carry clear | `C = 0` |
| 4 | `JMI` | minus | `N = 1` |
| 5 | `JPL` | plus | `N = 0` |
| 6 | `JVS` | overflow set | `V = 1` |
| 7 | `JVC` | overflow clear | `V = 0` |
| 8 | `JHI` | unsigned higher | `C = 1 and Z = 0` |
| 9 | `JLS` | unsigned lower or same | `C = 0 or Z = 1` |
| 10 | `JGE` | signed greater or equal | `N = V` |
| 11 | `JLT` | signed less than | `N != V` |
| 12 | `JGT` | signed greater than | `Z = 0 and N = V` |
| 13 | `JLE` | signed less or equal | `Z = 1 or N != V` |
| 14 | — | always (`JCOND` only) | true |
| 15 | — | never (`JCOND` only) | false |

The assembler has no mnemonic for codes 14 and 15; the disassembler prints them
as `JMP` and `J??`.

Test the flags after `CMP`, `SUB`, `ADD`, `AND`, `SLT`, or a shift. `CMP` sets
the same flags as `SUB` and writes no destination.

---

## 7. Exceptions, interrupts, and control registers

### 7.1 Vectors

| Vector | Source |
| --- | --- |
| 0 | reserved |
| 1 | `#UD` — invalid opcode or reserved addressing mode |
| 2 | `#DIV0` — integer divide or remainder by zero |
| 3 | `#BRK` — the `BRK` instruction |
| 4 | `#PF` — page fault |
| 5 | `#GP` — general protection |
| 6 | `#ALIGN` — alignment fault |
| 7 | `#DF` — double fault |
| 8..15 | reserved |
| 16..31 | hardware interrupt lines 0..15 |
| 32..255 | software `INT` and `SYSCALL` |

The vector table is 256 entries of 32-bit handler addresses at `VTBR`. `VTBR`
must be 4-byte aligned. `ESR` records the vector of the most recent delivery.

### 7.2 Delivery and return

Delivery, in order:

1. If the core is in user mode (`K = 0`), switch `SP` to `KSP`.
2. Set `K`, so the handler runs in kernel mode.
3. Push the error code, the saved `SP`, the saved `FLAGS`, and the victim `PC`.
4. Clear `I`, so the handler is not interrupted.
5. Load `PC` from `[VTBR + vector * 4]`; the handler address must be 4-byte
   aligned.

After the pushes the frame at the new `SP` is:

| Offset | Contents |
| --- | --- |
| `[SP + 0]` | victim `PC` |
| `[SP + 4]` | saved `FLAGS` |
| `[SP + 8]` | saved `SP` |
| `[SP + 12]` | error code |

`IRET` reverses it: it reads `PC`, `FLAGS`, `SP`, and the error code from the
frame, restores all three registers, and discards the error code. `IRET` is
kernel-only; in user mode it raises `#GP`. It checks only that the restored
`PC` is 4-byte aligned.

The victim `PC` depends on the cause:

| Cause | Saved `PC` | `IRET` resumes |
| --- | --- | --- |
| `#UD`, `#DIV0`, `#GP`, `#ALIGN`, `#PF` | the faulting instruction | retries it |
| `#BRK`, `INT`, `SYSCALL` | the following instruction | continues after it |
| hardware interrupt | the instruction not yet executed | executes it |

A fault during delivery makes the core attempt vector 7; if that also fails the
core halts.

### 7.3 Control registers

| CR | Name | Access | Purpose |
| --- | --- | --- | --- |
| 0 | `IE` | read/write | interrupt enable; reads `FLAGS.I` |
| 1 | `IMR` | read/write | hardware interrupt mask, bits 0..15 |
| 2 | `IRQ` | read/write | pending/active interrupt bitmap, bits 0..15 |
| 3 | `VTBR` | read/write | base of the 256-entry vector table; 4-byte aligned |
| 4 | `PRIV` | read-only | reads `FLAGS.K` |
| 5 | `ESR` | read/write | vector of the most recent delivery |
| 6 | `KSP` | read/write | kernel stack pointer |
| 7 | `MISC` | read-only | reserved; reads zero |
| 8 | `PTBR` | read/write | physical base of the page directory; 4 KiB aligned |
| 9 | `FAR` | read/write | fault address |
| 10 | `MCTL` | read/write | bit 0 `PGE` paging enable, bit 1 `WP` kernel write-protect |
| 11 | `COREID` | read-only | core index, 0-based |
| 12..15 | — | — | access raises `#GP` |

`RDCR`/`WRCR` are kernel-only. Writing `PRIV`, `MISC`, or `COREID` raises
`#GP`; so does a value that violates the alignment or reserved-bit rules for
`VTBR`, `PTBR`, or `MCTL`. For `WRCR` the data comes from the `Rd` field and
the register number from the `field` bits; the assembly order is
`WRCR CRn, Rs`.

---

## 8. Memory map

### 8.1 Physical map

| Range | Region | Size | Access |
| --- | --- | --- | --- |
| `0x0000_0000..0x0000_FFFF` | guard region, never backed | 64 KiB | with paging off, any access is `#GP` |
| `0x0001_0000..0x0800_FFFF` | RAM (default) | 128 MiB | read, write, execute |
| `0x0801_0000..0xFBFF_FFFF` | unbacked gap | — | `#GP` |
| `0xFC00_0000..0xFDFF_FFFF` | framebuffer VRAM aperture | 32 MiB | through the device bus |
| `0xFE00_0000..0xFFEF_FFFF` | unbacked gap | — | `#GP` |
| `0xFFF0_0000..0xFFFE_FFFF` | MMIO window | 960 KiB | through the device bus |
| `0xFFFF_0000..0xFFFF_FFFF` | ROM | 64 KiB | read and execute; stores are `#GP` |

RAM starts at `0x0001_0000` and grows upward. `--ram-size` changes its extent,
capped so that it cannot overlap ROM; keep the default unless a test needs a
different size. The default 128 MiB ends at `0x0800_FFFF`, so the default stack
top is `0x0801_0000`.

ROM holds the BIOS and the reset vector: reset loads `PC` from the word at
`0xFFFF_0000` and leaves it there if that word cannot be read.

### 8.2 Layout of a linked program

`ld` places allocatable sections from `--base` in rank order: `.text`,
`.rodata`, `.data`, `.bss`, then every other allocated section in the order it
first appears in the inputs (`.cpp_except`, `.tls`, `.init_array`,
`.fini_array`, `.eh_frame_array` for compiler output). Debug sections are given
no address and are not loaded.

| Section | Flags | Contents |
| --- | --- | --- |
| `.text` | alloc, execute | code |
| `.rodata` | alloc | constants, string literals |
| `.data` | alloc, write | initialized writable data |
| `.bss` | alloc, write | zero-initialized data, no file payload |
| `.cpp_except` | alloc | exception tables (C++), ranked with the other sections |
| `.tls` | alloc (template) | `thread_local` initial images |
| `.init_array`, `.fini_array`, `.eh_frame_array` | alloc, write | constructor, destructor, and unwind tables |
| `.debug_info` | — | debug records, not loaded |

With the default link base and a C or C++ program:

| Region | Default range |
| --- | --- |
| image | `0x0001_0000` up to the end of the last allocated section |
| heap (`__heap_start`..`__heap_end`) | end of that image (16-byte aligned) .. `0x0800_0000` |
| runtime stack | `0x0800_0000` .. `0x0801_0000`, grows down from `0x0801_0000` |
| `__stack_limit` | `0x0001_0000` |

`__heap_end` is `__stack_top - 64 KiB`, reserving that 64 KiB for the runtime
stack. The C runtime allocates between `__heap_start` and `__heap_end`. If the
image reaches `__stack_top - 64 KiB` the linker clamps `__heap_end` to
`__heap_start`, so an oversized image produces a zero-length heap rather than a
diagnostic; keep the image well below the stack.

For `examples/hello.c` the linker reports `__heap_start = 0x0002_41F0`,
`__heap_end = 0x0800_0000`, `__stack_top = 0x0801_0000`,
`__stack_limit = 0x0001_0000`.

### 8.3 KR32OS boot layout

| Address | Contents |
| --- | --- |
| `0x0002_0000` | kernel image, entry at the same address |
| `0x0002_0000 + initrd_offset` | initial ram disk, per kernel metadata |
| `0x0003_F000` | `BootInfo` block (14 words), address also in `R2` at boot |
| `0xFFFF_0000` | BIOS, reset vector |

The `BootInfo` block carries the magic `0x4232334B` (`"K32B"`), version, size,
RAM base and size, kernel base/size/entry, framebuffer base/size/stride/format,
and the initrd base and size. See [include/kr32/os/bootinfo.h](../include/kr32/os/bootinfo.h).

### 8.4 MMIO window

Each device owns one 4 KiB page:

| Base | Device |
| --- | --- |
| `0xFFF0_0000` | timer |
| `0xFFF0_1000` | block |
| `0xFFF0_2000` | console |
| `0xFFF0_3000` | IRQ router |
| `0xFFF0_4000` | framebuffer control |
| `0xFFF0_5000` | serial tty |
| `0xFFF0_6000` | keyboard |
| `0xFFF0_7000` | reserved for a pointer device, no device behind it |
| `0xFFF0_8000` | terminal (host-rendered console) |
| `0xFFF0_9000` | graphics commands (host-executed drawing) |

Register layouts, bits, and semantics are owned by [devices.md](devices.md).
Access device registers with aligned 32-bit loads and stores; a byte access
becomes a read/modify/write of the containing word. On a device page that has
no register at the offset the access raises `#GP`, except the framebuffer,
terminal, and graphics command pages, which read zero for unknown offsets and
ignore such writes.

The device window and the framebuffer aperture are decoded only when paging is
enabled: with paging off the machine resolves the guard, RAM, and ROM, and
everything else is `#GP`. A flat program that needs a device must build a page
table, set `PTBR`, and set `MCTL.PGE` itself; KR32OS does this in its boot path.

The framebuffer aperture is 32 MiB of VRAM, not one surface: the guest chooses
`FRONT`/`BACK` offsets and a stride, and a pixel is one little-endian word
`0x00RRGGBB`. Control registers are at `0xFFF0_4000`.

### 8.5 Paging

Paging is two-level, 4 KiB pages, 10 + 10 bits of virtual page number:

1. `PDE = physical[PTBR + (vpn >> 10) * 4]`
2. `PTE = physical[(PDE & 0xFFFF_F000) + (vpn & 0x3FF) * 4]`
3. `physical = (PTE & 0xFFFF_F000) | (address & 0xFFF)`

| Bit | Flag | Meaning |
| --- | --- | --- |
| 0 | `V` | present |
| 1 | `W` | writable |
| 2 | `U` | user-accessible |
| 3 | `X` | executable |
| 4 | `A` | accessed, set by the machine |
| 5 | `D` | dirty, set by the machine on a store |
| 6 | `G` | global |
| 7 | `PS` | reserved; a set bit faults |

Rules the machine enforces:

- Both levels must have `V`; a clear bit is a not-present page fault.
- User mode requires `U` on both levels.
- Instruction fetch requires `X`; a store requires `W`.
- With `MCTL.WP` set, a kernel store also requires `W`.
- A page-fault error code is `bit0 P | bit1 W | bit2 U | bit3 X`: `P = 1` means
  protection rather than not-present, `W` a store, `U` user mode, `X` a fetch.
  `FAR` receives the faulting virtual address.
- A translated address must land in RAM, ROM, the framebuffer aperture, or the
  device window; otherwise the access faults.
- Writes are not permitted to ROM at any privilege level.
- There is no TLB, so a page-table edit is visible to the next instruction.

Data accesses are byte-granular and need not be aligned. A 16- or 32-bit access
that crosses a page boundary is split into one translation per page, so the two
halves can fault independently.

---

## 9. Calling convention

The full contract is [abi.md](abi.md); this is the part a hand-written
assembly routine needs.

| Register | Role |
| --- | --- |
| `R0..R3` | first four integer-class arguments; `R0` is the integer return value |
| `R4..R6` | caller-saved scratch |
| `R7` | frame pointer, callee-saved |
| `R8..R10` | callee-saved general registers |
| `R11` | C++ thread pointer (`TP`); preserved across calls |
| `R12..R15` | caller-saved scratch |
| `SP` | stack pointer, not in the GPR file |
| `F0..F7` | FP arguments and return value; caller-saved |
| `F8..F15` | callee-saved FP registers |
| `T0..T15` | caller-saved; never live across a call |
| `TSP` | callee-preserved; balanced on every exit path |

Rules:

- The caller aligns `SP` to 16 bytes immediately before `CALL`. A callee
  therefore enters with `SP % 16 == 12`, because `CALL` pushes 4 bytes. A callee
  that makes nested calls realigns `SP` itself.
- The fifth and later integer arguments are pushed in the outgoing stack area at
  increasing addresses; `float`/`double` arguments use `F0..F7` in declaration
  order independently of the integer registers.
- A scalar `float`/`double` return is in `F0`; `F0`/`F1` carry a two-register
  FP aggregate return.
- Variadic arguments are stack slots of 8 bytes, 8-byte aligned, promoted
  (`float` to `double`, small integers to `int`).
- `CALL` pushes the address after the instruction; `RET` pops it.

The frame the C and C++ compilers emit:

```asm
        PUSH    R7
        ADJSP   #-frame
        LEA     R7, [SP]
        ...
        ADJSP   #frame
        POP     R7
        RET
```

Locals and parameters are at positive offsets from `R7`; the return address is
at `R7 + frame + 4` and the saved `R7` at `R7 + frame`. Exception unwinding
walks that chain, so a hand-written function that can be unwound through must
keep the shape.

---

## 10. Host services

`SYSCALL #n` enters a host service dispatcher when one is installed: the CLI
runner installs `vm::Host` for `vm`, `run`, and `build --run`, and the debugger
session installs its own for `gui` and `dap`. Arguments are in `R0..R3` and the
result is in `R0`. Errors are small negative values.

| Vector | Name | Arguments | Result |
| --- | --- | --- | --- |
| 32 | exit | code | halts the machine |
| 33 | putchar | character | 0 |
| 34 | getchar | — | character or `-1` |
| 35 | open | path, flags | fd or error |
| 36 | close | fd | 0 or error |
| 37 | read | fd, buffer, count | bytes or error |
| 38 | write | fd, buffer, count | bytes or error |
| 39 | seek | fd, offset, whence | position or error |
| 40 | time | — | host seconds |
| 41 | random | — | deterministic xorshift value |
| 42 | argc | — | count of `--arg` values |
| 43 | arg | index, buffer, size | copied length |
| 44..50 | thread create/join/yield/exit, futex wait/wake, self | see host.md | handle, code, or 0 |
| 51 | file dialog | mode, flags, path, capacity | path length or error |
| 52..59 | unlink, rename, stat, truncate, getcwd, chdir, dup, dup2 | see host.md | 0 or error |

Vector 32 halts with the exit code. Vector 33 is the host debug console: each
character is echoed to the CLI's stdout immediately. This is not the guest
screen; a program that wants the screen writes to the terminal device (page 8)
or uses `con_*`. There are no pre-opened descriptors
`1` and `2`; `write` only works on descriptors returned by `open`. Guest paths
must be relative and stay under `--fs-root`; absolute paths and `..` are
rejected. Open flags: bits 0..1 read/write (`0` read, `1` write, `2` both),
bit 2 create, bit 3 truncate, bit 4 append. [host.md](host.md) is normative for
argument and error details.

An `INT #n` uses the same dispatch path and the same vector range. A vector with
no handler is delivered through the guest vector table instead, which is how a
guest kernel receives a software interrupt.

---

## 11. Programming patterns

**Load an address.** Symbolic immediates always take an extension word, so an
address costs 8 bytes once and 4 bytes per access afterwards:

```asm
        MOV     R1, #table          ; 8 bytes: full 32-bit address
        LOAD    R0, [R1]            ; 4 bytes
```

**Read a global by absolute address.** One 8-byte instruction instead of two:

```asm
        LOAD    R0, [counter]
```

**Use the stack.** There is no push-immediate; push a register:

```asm
        MOV     R0, #7
        PUSH    R0
        ...
        POP     R0
```

**Save a whole register set.** `PUSHR`/`POPR` cover `R1..R15` and deliberately
leave `R0` alone, so a syscall wrapper keeps the dispatcher's result:

```asm
        PUSH    R0
        PUSHR
        ...                     ; handler body
        POPR
        POP     R0
```

`PUSHFP`/`POPFP` save all 128 bytes of FP context. `TSP` is not part of either
group; save it explicitly with `RDSP`/`WRSP`.

**Index an array.** There is no scaled index. Shift the index first:

```asm
        SHL     R2, R2, #2      ; word index
        LOAD    R0, [R1 + R2]   ; LOADX
```

**Walk a byte string.** `LOAD8U` zero-extends; use `LOAD8S` for signed bytes:

```asm
next:   LOAD8U  R0, [R1]
        CMP     R0, #0
        JZ      done
        SYSCALL #33
        ADD     R1, R1, #1
        JMP     next
```

**Work with 64-bit products.** `MUL` gives the low word, `MULHU`/`MULH` the high
word of an unsigned/signed 32x32 product. There is no add-with-carry
instruction, so a 64-bit addition tests `C` after `ADD` explicitly, and a
64-bit comparison checks the high words first and the low words only when the
high words are equal.

**Atomically update a word.** The reservation is per core and word-granular, is
cleared by any store to the reserved word, and is cleared by `SC` and by
exception delivery:

```asm
retry:  LL      R1, [R2]
        ADD     R1, R1, #1
        SC      R1, [R2]
        JNZ     retry           ; Z = 0 means the reservation was lost
```

**Use the FP unit.** Scalar FP lives in `F0..F15` and is moved with word and
double memory operations:

```asm
        FLOAD.D F0, [R1]
        FADD.D  F0, F0, F1
        FSTORE.D F0, [R2]
        FCVT.W.D R0, F0         ; saturating conversion to int
```

**Use the tile unit.** Tiles are caller-saved scratch; spill with
`MPUSH`/`MPOP` after installing a transform stack with `WRSP`:

```asm
        MOV     R3, #0x00018000
        WRSP    R3
        MLOAD   T1, [R1]
        MMUL    T2, T1, T1
        MPUSH   T2
        MPOP    T3
        MDROP
```

**Exit.** Two forms; both define the process exit code:

```asm
        MOV     R0, #0
        SYSCALL #32             ; exit(0)
```

```asm
        MOV     R0, #0
        HALT                    ; exit code is R0 & 0xFF
```

Falling off the end of the image is not an exit: the core runs into whatever
bytes follow, or into the guard region, and faults.

---

## 12. Instruction reference

### 12.1 Legend

| Symbol | Meaning |
| --- | --- |
| `Rd`, `Rs1`, `Rs2` | general registers `R0..R15` |
| `x` | `Rs2` when `L = 0`, the payload when `L = 1` |
| `Fd`, `Fs1`, `Fs2` | FP registers |
| `Td`, `Ta`, `Tb` | tile registers |
| `disp` | signed displacement, inline or in the extension word |
| `ea` | effective address |
| **Addr** column | `R` register only, `I` immediate only, `M` any memory mode 2..6, `X` base+index form, `B` bare base register (`[Rs]`), `SP` implicit stack pointer, `—` no operand |
| **Flags** column | flags written; `—` means none. Flags not listed keep their value |

Memory modes are `2 [Rs1]`, `3 [Rs1 + disp]`, `4 [SP + disp]`,
`5 [disp]`, `6 [PC + disp]`. Modes 0, 1, and 7..15 are reserved for a memory
instruction and raise `#UD`. `LEA` computes `ea` without touching memory.

### 12.2 Data movement

| Mnemonic | Title | Effect on operands | Addr | Flags | Faults |
| --- | --- | --- | --- | --- | --- |
| `MOV Rd, Rs` | Move register | `Rd ← Rs1` | R | — | — |
| `MOV Rd, #imm` | Load immediate | `Rd ← imm` | I | — | — |
| `LOAD Rd, [mem]` | Load word | `Rd ← mem32[ea]` | M | — | `#UD` bad mode, `#GP`/`#PF` |
| `STORE Rs, [mem]` | Store word | `mem32[ea] ← Rd` | M | — | `#UD` bad mode, `#GP`/`#PF` |
| `LOAD8U Rd, [mem]` | Load unsigned byte | `Rd ← zero_extend(mem8[ea])` | M | — | as `LOAD` |
| `LOAD8S Rd, [mem]` | Load signed byte | `Rd ← sign_extend(mem8[ea])` | M | — | as `LOAD` |
| `LOAD16U Rd, [mem]` | Load unsigned halfword | `Rd ← zero_extend(mem16[ea])` | M | — | as `LOAD` |
| `LOAD16S Rd, [mem]` | Load signed halfword | `Rd ← sign_extend(mem16[ea])` | M | — | as `LOAD` |
| `STORE8 Rs, [mem]` | Store byte | `mem8[ea] ← Rd & 0xFF` | M | — | as `STORE` |
| `STORE16 Rs, [mem]` | Store halfword | `mem16[ea] ← Rd & 0xFFFF` | M | — | as `STORE` |
| `LEA Rd, [mem]` | Load effective address | `Rd ← ea` | M | — | `#UD` bad mode |

Notes:

- For stores the data register is the first operand and occupies the `Rd` field.
  `STORE R6, [R7]` stores `R6`.
- `LOAD`/`STORE` are byte-granular and do not require alignment. A 32-bit access
  crossing a page boundary is split, and either half can fault.
- `MOV` never touches memory; it has no memory operand form.
- `LEA` is how `SP` and `PC` become values: `LEA R7, [SP]`,
  `LEA R0, [PC + 4]`. `LEA` does not check whether `ea` is backed, so an
  address computed by `LEA` is valid to produce and may fault on first use.

### 12.3 Indexed memory

The same syntax `[Rs1 + Ri + disp]` selects the indexed opcode; there is no
separate mnemonic, but the disassembler prints the `*X` names.

| Opcode | Disassembly | Effect |
| --- | --- | --- |
| `0x10` | `LOADX` | `Rd ← mem32[Rs1 + Ri + disp]` |
| `0x11` | `STOREX` | `mem32[Rs1 + Ri + disp] ← Rd` |
| `0x12` | `LOAD8UX` | zero-extended byte |
| `0x13` | `LOAD8SX` | sign-extended byte |
| `0x14` | `LOAD16UX` | zero-extended halfword |
| `0x15` | `LOAD16SX` | sign-extended halfword |
| `0x16` | `STORE8X` | low byte |
| `0x17` | `STORE16X` | low halfword |

The index register is `Ri` in bits 16..13 and the displacement is the payload.
Every indexed instruction always adds the index, even when the displacement is
zero. The index is never scaled. `Ri` and `Rs1` may name the same register.
Flags are unaffected; faults match the non-indexed form. The object assembler
does not accept this form (§3.6).

### 12.4 Integer arithmetic

| Mnemonic | Title | Effect | Addr | Flags | Faults |
| --- | --- | --- | --- | --- | --- |
| `ADD Rd, Rs1, x` | Add | `Rd ← Rs1 + x` | R, I | `Z N C V` | — |
| `SUB Rd, Rs1, x` | Subtract | `Rd ← Rs1 - x` | R, I | `Z N C V` | — |
| `NEG Rd, Rs1` | Negate | `Rd ← 0 - Rs1` | R | `Z N C V` | — |
| `CMP Rs1, x` | Compare | flags of `Rs1 - x`; no write | R, I | `Z N C V` | — |
| `MUL Rd, Rs1, x` | Multiply, low word | `Rd ← (Rs1 * x) & 0xFFFFFFFF` | R, I | `Z N`; `C = 0`; `V = high != 0` | — |
| `MULH Rd, Rs1, x` | Multiply, signed high | `Rd ← (signed product) >> 32` | R, I | `Z N`; `C = V = 0` | — |
| `MULHU Rd, Rs1, x` | Multiply, unsigned high | `Rd ← (unsigned product) >> 32` | R, I | `Z N`; `C = V = 0` | — |
| `DIV Rd, Rs1, x` | Divide unsigned | `Rd ← Rs1 / x` | R, I | `Z N`; `C = V = 0` | `#DIV0` if `x = 0` |
| `DIVS Rd, Rs1, x` | Divide signed | `Rd ← signed quotient` | R, I | `Z N`; `C = V = 0` | `#DIV0` if `x = 0` |
| `MOD Rd, Rs1, x` | Remainder unsigned | `Rd ← Rs1 mod x` | R, I | `Z N`; `C = V = 0` | `#DIV0` if `x = 0` |
| `MODS Rd, Rs1, x` | Remainder signed | `Rd ← signed remainder` | R, I | `Z N`; `C = V = 0` | `#DIV0` if `x = 0` |
| `SLT Rd, Rs1, x` | Set on less than, signed | `Rd ← (signed Rs1 < x) ? 1 : 0` | R, I | `Z N`; `C = V = 0` | — |
| `SLTU Rd, Rs1, x` | Set on less than, unsigned | `Rd ← (unsigned Rs1 < x) ? 1 : 0` | R, I | `Z N`; `C = V = 0` | — |

Notes:

- `C` on `SUB` and `CMP` is *no borrow*: it is set when `Rs1 >= x` as unsigned.
- `MUL`'s `V` is set when the unsigned 64-bit product does not fit in 32 bits.
  `MULH`/`MULHU` treat the multiplicands as signed/unsigned respectively and
  never set `C` or `V`.
- `DIVS 0x80000000 / -1` yields `0x80000000` and does not trap.
  `MODS 0x80000000 % -1` yields `0`.
- `CMP` writes no register, so its `Rd` field is unused.
- Immediate operands are the payload: signed 11-bit inline, or a full 32-bit
  extension word.

### 12.5 Logic

| Mnemonic | Title | Effect | Addr | Flags |
| --- | --- | --- | --- | --- |
| `AND Rd, Rs1, x` | Bitwise AND | `Rd ← Rs1 & x` | R, I | `Z N`; `C = V = 0` |
| `OR Rd, Rs1, x` | Bitwise OR | `Rd ← Rs1 \| x` | R, I | `Z N`; `C = V = 0` |
| `XOR Rd, Rs1, x` | Bitwise exclusive OR | `Rd ← Rs1 ^ x` | R, I | `Z N`; `C = V = 0` |
| `NOT Rd, Rs1` | Bitwise complement | `Rd ← ~Rs1` | R | `Z N`; `C = V = 0` |

`Z` and `N` come from the result; `C` and `V` are always cleared.

### 12.6 Shifts and rotates

| Mnemonic | Title | Effect | Addr | Flags |
| --- | --- | --- | --- | --- |
| `SHL Rd, Rs1, x` | Shift left logical | `Rd ← Rs1 << (x & 31)` | R, I | `Z N`; `C` = last bit out; `V = 0` |
| `SHR Rd, Rs1, x` | Shift right logical | `Rd ← Rs1 >> (x & 31)` | R, I | `Z N`; `C` = last bit out; `V = 0` |
| `SAR Rd, Rs1, x` | Shift right arithmetic | `Rd ← signed Rs1 >> count` | R, I | `Z N`; `C` = last bit out; `V = 0` |
| `ROL Rd, Rs1, x` | Rotate left | bit 31 wraps into bit 0 | R, I | `Z N`; `C` = last bit out; `V = 0` |
| `ROR Rd, Rs1, x` | Rotate right | bit 0 wraps into bit 31 | R, I | `Z N`; `C` = last bit out; `V = 0` |

The count is `x & 31`. A count of zero leaves the destination unchanged, sets
`Z`/`N` from the unchanged value, clears `V`, and **leaves `C` unchanged**.

`C` is the bit shifted out of the register:

| Instruction | `C` on a non-zero count |
| --- | --- |
| `SHL` | bit `32 - count` of the source |
| `SHR`, `SAR`, `ROR` | bit `count - 1` of the source |
| `ROL` | bit `32 - count` of the source |

`ROL`/`ROR` are accepted only by the raw assembler (§3.6).

### 12.7 Branches

| Mnemonic | Title | Effect | Addr | Flags |
| --- | --- | --- | --- | --- |
| `JMP target` | Jump | `PC ← target` | R, I | — |
| `JCOND cond, target` | Conditional jump | `PC ← target` if `cond`, else next | R, I | — |
| `JZ`/`JNZ`/`JCS`/`JNC`/`JMI`/`JPL`/`JVS`/`JVC`/`JHI`/`JLS`/`JGE`/`JLT`/`JGT`/`JLE target` | Conditional jump | aliases for `JCOND` with condition 0..13, §6 | R, I | — |
| `CALL target` | Call | push `next_pc`; `PC ← target` | R, I | — |
| `RET` | Return | `target ← mem32[SP]`; `SP += 4`; `PC ← target` | `SP` | — |

Notes:

- The target is `Rs1` when `L = 0` and the payload when `L = 1`. A literal
  target is always encoded as an 8-byte instruction.
- An unaligned target raises `#ALIGN` — but only when the branch is taken, so
  `JCOND` with a false condition never checks its target.
- `CALL` pushes `next_pc`: the address after the `CALL`, which is 8 bytes past a
  `CALL` that carries an extension word.
- A stack fault during `CALL`, or a misaligned or overflowing `SP`, raises
  `#GP`, not `#ALIGN`. `RET` raises `#ALIGN` for a misaligned `SP` or a
  misaligned popped target, and propagates the load fault otherwise.
- `JCOND` conditions 14 and 15 are always and never taken; the assembler has no
  mnemonic for them, but hand-encoded words can use them.

### 12.8 Stack

`SP` grows down. `PUSH` stores at `SP - 4` and then decrements; `POP` loads from
`SP` and then increments. `SP` must be 4-byte aligned when a stack access is
attempted. A misaligned or overflowing `SP` on `PUSH`, `POP`, `PUSHR`, `POPR`,
`PUSHFP`, `POPFP`, or `CALL` raises `#GP`; `ADJSP`, `SETSP`, and `RET` raise
`#ALIGN` for a misaligned result.

| Mnemonic | Title | Effect | Addr | Flags | Notes |
| --- | --- | --- | --- | --- | --- |
| `PUSH Rs` | Push word | `SP -= 4; mem32[SP] ← operand` (encoded in `Rd`) | R | — | 4 bytes |
| `POP Rd` | Pop word | `Rd ← mem32[SP]; SP += 4` | R | — | 4 bytes |
| `PUSHR` | Push register file | `SP -= 60`; push `R1..R15` | — | — | excludes `R0` |
| `POPR` | Pop register file | pop `R15..R1`; `SP += 60` | — | — | excludes `R0` |
| `PUSHFP` | Push FP context | `SP -= 128`; for `F0..F15` low word then high word | — | — | all 16 FPRs |
| `POPFP` | Pop FP context | for `F15..F0` high word then low word; `SP += 128` | — | — | all 16 FPRs |
| `ADJSP #imm` | Adjust stack pointer | `SP += imm` | I | — | `imm` is signed 11-bit or 32-bit |
| `SETSP Rs` | Set stack pointer | `SP ← Rs1` | R | — | unwinder support |

Bulk operations are not restartable:

- `PUSHR` and `PUSHFP` restore `SP` to its entry value when a store faults;
  words already written remain in memory.
- `POPR` and `POPFP` leave earlier registers restored when a load faults, and
  `SP` reflects only the completed pops.
- Bulk operations leave the flags unchanged.

`PUSHR`/`POPR` skip `R0` so a syscall wrapper can preserve `R1..R15` without
destroying the dispatcher's result in `R0`. `PUSHFP`/`POPFP` do not save `TSP`,
tiles, or control registers.

### 12.9 System

| Mnemonic | Title | Effect | Addr | Flags | Faults |
| --- | --- | --- | --- | --- | --- |
| `INT #vec` | Software interrupt | raise the software vector; `PC` advances first | I | — | `#GP` if `vec < 32` or `> 255` |
| `SYSCALL #vec` | Host service call | dispatch vector; `PC` advances first | I | — | `#GP` if `vec < 32` or `> 255` |
| `IRET` | Interrupt return | pop `PC`, `FLAGS`, `SP` from the frame | `SP` | restored | `#GP` in user mode; `#ALIGN`, `#PF` |
| `HALT` | Halt core | stop the core | — | — | `#GP` in user mode |
| `BRK` | Breakpoint | raise `#BRK`; `PC` advances first | — | — | `#BRK` |
| `NOP` | No operation | nothing | — | — | — |
| `FENCE` | Memory fence | full barrier; a no-op under sequential consistency | — | — | — |
| `RDSP Rd` | Read transform stack pointer | `Rd ← TSP` | R | — | — |
| `WRSP Rs` | Write transform stack pointer | `TSP ← operand` (encoded in `Rd`) | R | — | — |
| `RDCR Rd, CRn` | Read control register | `Rd ← CRn` | R | — | `#GP` unless kernel and `CRn` valid |
| `WRCR CRn, Rs` | Write control register | `CRn ← operand` (encoded in `Rd`) | R | — | `#GP` unless kernel and value valid |

Notes:

- `INT` and `SYSCALL` are the same machine operation. The host installs a
  handler; if the handler claims the vector the instruction retires, otherwise
  the machine delivers the vector through the guest table. Both advance `PC`
  before raising, so a handler returns to the following instruction.
- `BRK` advances `PC` before raising, so a debugger that resumes continues
  after it. `#UD`, `#DIV0`, `#GP`, `#ALIGN`, and `#PF` leave `PC` on the
  faulting instruction.
- `HALT` records an exit code only through `SYSCALL #32`; otherwise the runner
  uses `R0 & 0xFF`.

### 12.10 Atomics

| Mnemonic | Title | Effect | Addr | Flags | Faults |
| --- | --- | --- | --- | --- | --- |
| `LL Rd, [mem]` | Load-linked | `Rd ← mem32[ea]`; set this core's reservation on the physical word | M | — | `#ALIGN`, `#GP` outside RAM, `#PF`/`#GP` |
| `SC Rs, [mem]` | Store-conditional | store `Rd` if the reservation still holds; `Z = 1` on success | M | `Z` only | `#ALIGN`, `#GP` outside RAM, `#PF`/`#GP` |

- The address must be 4-byte aligned; a misaligned address raises `#ALIGN`.
- RAM only: a ROM, MMIO, or framebuffer address raises `#GP`.
- The reservation holds one physical word per core. It is cleared by `SC`, by
  a store to that word, and by exception or interrupt delivery. A multicore
  `Cluster` invalidates the other cores' reservations when one core writes.
- `SC` writes `Z` and leaves `N`, `C`, and `V` unchanged. A failed `SC` stores
  nothing.
- `FENCE` is a no-op because the emulator is sequentially consistent; include
  it in code that must also be correct on a weaker future implementation.

### 12.11 FP and tile memory

| Mnemonic | Title | Effect | Addr | Flags |
| --- | --- | --- | --- | --- |
| `FLOAD.W Fd, [mem]` | Load single | `Fd ← mem32[ea]`, bits 63..32 cleared | M | — |
| `FLOAD.D Fd, [mem]` | Load double | `Fd ← mem64[ea]` | M | — |
| `FSTORE.W Fd, [mem]` | Store single | `mem32[ea] ← Fd[31:0]` | M | — |
| `FSTORE.D Fd, [mem]` | Store double | `mem64[ea] ← Fd` | M | — |
| `MLOAD Td, [mem]` | Load 4x4 tile | `Td ← 4 rows of 4 floats` at `ea`, rows 16 bytes apart | M | — |
| `MLOAD Td, [Rs], #stride` | Load strided tile | rows at `Rs + r * stride` | B | — |
| `MSTORE Td, [mem]` | Store 4x4 tile | four rows at `ea`, rows 16 bytes apart | M | — |
| `MSTORE Td, [Rs], #stride` | Store strided tile | rows at `Rs + r * stride` | B | — |
| `VLOAD Td, [mem]` | Load vector | row 0 from `ea`; rows 1..3 cleared | M | — |
| `VSTORE Td, [mem]` | Store vector | row 0 to `ea` | M | — |

`L` selects the width: `L = 0` is `.W` or a 64-byte matrix, `L = 1` is `.D` or a
16-byte vector. `E = 1` on `MLOAD`/`MSTORE` carries the row stride.

Rules and limits:

- Scalar FP loads and stores may be unaligned; page and protection faults
  follow the normal data rules.
- Tile transfers require a 4-byte-aligned address, and a stride that is a
  multiple of 4, or the instruction raises `#ALIGN`.
- A strided load or store requires the inline displacement to be zero, and
  `VLOAD`/`VSTORE` have no strided form; either violation raises `#UD`.
- **Use the bare register form for a strided transfer.** The encoder stores the
  stride in the extension word, and the machine also reads the payload as the
  displacement, so a form such as `[R2 + 0]` loads from `R2 + stride`, not from
  `R2`. Write `MLOAD Td, [R2], #stride`.
- Faults are not atomic across rows: a fault on row 2 leaves rows 0 and 1
  transferred and `Td` partially written.

### 12.12 FP escape (`0x60`)

The function is the low 11 bits of the payload, `Fd` is the `Rd` field, `Fs1`
is `Rs1`, and `Fs2` is bits 16..13. Bit 8 selects double precision
(`0x100..0x10B`). `FADD` without a suffix assembles as `FADD.S`; the
disassembler always prints the suffix.

| Mnemonic | Title | Operands | Effect | Flags |
| --- | --- | --- | --- | --- |
| `FADD.S` / `.D` | Add | `Fd, Fs1, Fs2` | `Fd ← Fs1 + Fs2` | — |
| `FSUB.S` / `.D` | Subtract | `Fd, Fs1, Fs2` | `Fd ← Fs1 - Fs2` | — |
| `FMUL.S` / `.D` | Multiply | `Fd, Fs1, Fs2` | `Fd ← Fs1 * Fs2` | — |
| `FDIV.S` / `.D` | Divide | `Fd, Fs1, Fs2` | `Fd ← Fs1 / Fs2` | — |
| `FSQRT.S` / `.D` | Square root | `Fd, Fs1` | `Fd ← sqrt(Fs1)` | — |
| `FMIN.S` / `.D` | Minimum | `Fd, Fs1, Fs2` | `Fd ← min(Fs1, Fs2)` | — |
| `FMAX.S` / `.D` | Maximum | `Fd, Fs1, Fs2` | `Fd ← max(Fs1, Fs2)` | — |
| `FABS.S` / `.D` | Absolute value | `Fd, Fs1` | clear the sign bit | — |
| `FNEG.S` / `.D` | Negate | `Fd, Fs1` | flip the sign bit | — |
| `FEQ.S` / `.D` | Compare equal | `Rd, Fs1, Fs2` | `Rd ← (Fs1 == Fs2) ? 1 : 0` | `Z` from result; `N C V = 0` |
| `FLT.S` / `.D` | Compare less than | `Rd, Fs1, Fs2` | `Rd ← (Fs1 < Fs2) ? 1 : 0` | `Z` from result; `N C V = 0` |
| `FLE.S` / `.D` | Compare less or equal | `Rd, Fs1, Fs2` | `Rd ← (Fs1 <= Fs2) ? 1 : 0` | `Z` from result; `N C V = 0` |

Conversions and moves:

| Mnemonic | Title | Operands | Effect |
| --- | --- | --- | --- |
| `FCVT.S.D` | Double to single | `Fd, Fs` | `Fd ← (float)Fs`, upper half cleared |
| `FCVT.D.S` | Single to double | `Fd, Fs` | `Fd ← (double)Fs` |
| `FCVT.W.S` | Single to signed int | `Rd, Fs` | saturating |
| `FCVT.WU.S` | Single to unsigned int | `Rd, Fs` | saturating |
| `FCVT.S.W` | Signed int to single | `Fd, Rs` | `Fd ← (float)(int)Rs` |
| `FCVT.S.WU` | Unsigned int to single | `Fd, Rs` | `Fd ← (float)Rs` |
| `FCVT.W.D` | Double to signed int | `Rd, Fs` | saturating |
| `FCVT.WU.D` | Double to unsigned int | `Rd, Fs` | saturating |
| `FCVT.D.W` | Signed int to double | `Fd, Rs` | `Fd ← (double)(int)Rs` |
| `FCVT.D.WU` | Unsigned int to double | `Fd, Rs` | `Fd ← (double)Rs` |
| `FMV.W.X` | Bit move to FP | `Fd, Rs` | low 32 bits copied, bits 63..32 cleared |
| `FMV.X.W` | Bit move from FP | `Rd, Fs` | low 32 bits copied |

Semantics:

- Round to nearest, ties to even. There are no FP traps and no FP status flags.
- All NaN results are canonical quiet NaNs: `0x7FC0_0000` for single,
  `0x7FF8_0000_0000_0000` for double.
- `FMIN`/`FMAX` return the non-NaN operand when exactly one is NaN, and the
  canonical NaN when both are.
- Ordered comparisons are false for NaN, so `FEQ` on a NaN writes 0. The result
  is a GPR holding 0 or 1 and the machine sets `Z` from it, so `Z = 1` means the
  predicate was **false**: branch on `JNZ` for true and `JZ` for false.
- Treat the FP unit as flag-free: it never sets `C` or `V`, and every FP
  arithmetic or conversion leaves the integer flags untouched.
- Integer conversions saturate: NaN and `+inf` give the maximum value, `-inf`
  the minimum, and a negative value gives 0 for the unsigned forms.
- Denormals are supported. `float` occupies bits 31..0; `double` uses 64 bits.
- An unknown function raises `#UD`.

### 12.13 Matrix escape (`0x61`)

`Td` is the `Rd` field, `Ta` is `Rs1`, `Tb` is bits 16..13; the function is the
low 11 bits of the payload. Tiles are 4x4 row-major `f32`.

| Mnemonic | Title | Operands | Effect |
| --- | --- | --- | --- |
| `MMUL` | Matrix multiply | `Td, Ta, Tb` | `Td ← Ta * Tb` |
| `MMAC` | Multiply-accumulate | `Td, Ta, Tb` | `Td ← Td + Ta * Tb` |
| `MVEC` | Matrix times vector | `Td, Ta, Tb` | row `r`, column 0 ← dot(`Ta` row `r`, `Tb` column 0); all other elements cleared |
| `MTRANS` | Transpose | `Td, Ta` | `Td ← transpose(Ta)` |
| `VDOT` | Dot product | `Fd, Ta, Tb` | `Fd ← sum(Ta[0][k] * Tb[0][k])` |
| `MPUSH` | Push tile | `Td` | `TSP -= 64; mem[TSP] ← Td` |
| `MPOP` | Pop tile | `Td` | `Td ← mem[TSP]; TSP += 64` |
| `MDROP` | Drop tile | — | `TSP += 64` |

- `MMUL`, `MMAC`, and `MVEC` accumulate with fused multiply-add and round to
  nearest even.
- `Td` may alias `Ta` or `Tb`; sources are read before the destination is
  written.
- Tile and matrix operations leave the integer flags unchanged.
- The transform stack grows down in 64-byte entries. `TSP` must be 64-byte
  aligned; `MPUSH` raises `#ALIGN` on a misaligned `TSP` and `#GP` when
  `TSP < 64`. `TSP` is left unchanged when the access faults.
- `MPOP`/`MDROP` with a bad `TSP` raise `#ALIGN`; a `TSP` outside mapped memory
  faults on the first access.
- `TSP` starts at zero, so the first `MPUSH` requires `WRSP` to install a
  valid, 64-byte-aligned address.
- An unknown function raises `#UD`.

### 12.14 Byte and bit escape (`0x62`)

`Rd` is bits 24..21, `Rs1` is bits 20..17, the payload is
`function << 5 | lsb`, and `width - 1` is in bits 16..13. The assembler takes
the fields as operands.

| Mnemonic | Title | Operands | Effect | Flags |
| --- | --- | --- | --- | --- |
| `BSWAP` | Byte swap | `Rd, Rs1` | reverse the four bytes of `Rs1` | `Z N`; `C = V = 0` |
| `BFX` | Bit-field extract | `Rd, Rs1, #lsb, #width` | `Rd ← (Rs1 >> lsb) & mask` | `Z N`; `C = V = 0` |
| `BFI` | Bit-field insert | `Rd, Rs1, #lsb, #width` | `Rd ← (Rd & ~(mask << lsb)) \| ((Rs1 & mask) << lsb)` | `Z N`; `C = V = 0` |
| `CLZ` | Count leading zeros | `Rd, Rs1` | 0..32; 32 for zero | `Z N`; `C = V = 0` |
| `CTZ` | Count trailing zeros | `Rd, Rs1` | 0..32; 32 for zero | `Z N`; `C = V = 0` |
| `POPCNT` | Population count | `Rd, Rs1` | number of set bits | `Z N`; `C = V = 0` |

- `mask` is `(1 << width) - 1`, with `width = field + 1` in `1..32`.
  `BFX`/`BFI` validate `lsb` in `0..31` and `width` in `1..32` at assembly time.
- `BFI` reads `Rd` as an input as well as a destination.
- `Z` and `N` come from the result, `C` and `V` are cleared. `CLZ`/`CTZ` of
  zero produce 32, so they leave `Z = 0` and `N = 0`; only a result with bit 31
  set raises `N`.
- Functions 6 and 7 are reserved and raise `#UD`.

### 12.15 Encoding summary

| Opcode | Mnemonic | `Rd` | `Rs1` | field 16..13 | `L` |
| --- | --- | --- | --- | --- | --- |
| `0x00` | `MOV` | destination | source | — | 0 = register, 1 = immediate |
| `0x01` | `LOAD` | destination | base | mode | 1 |
| `0x02` | `STORE` | data | base | mode | 1 |
| `0x03` | `LOAD8U` | destination | base | mode | 1 |
| `0x04` | `LOAD8S` | destination | base | mode | 1 |
| `0x05` | `LOAD16U` | destination | base | mode | 1 |
| `0x06` | `LOAD16S` | destination | base | mode | 1 |
| `0x07` | `STORE8` | data | base | mode | 1 |
| `0x08` | `STORE16` | data | base | mode | 1 |
| `0x09` | `LEA` | destination | base | mode | 1 |
| `0x0A` | `POPFP` | — | — | — | — |
| `0x10..0x17` | `LOADX`..`STORE16X` | destination/data | base | index | 1 |
| `0x18` | `SLT` | destination | first source | second source | 0/1 |
| `0x19` | `SLTU` | destination | first source | second source | 0/1 |
| `0x20..0x28` | `ADD`..`MODS` | destination | first source | second source | 0/1 |
| `0x29` | `CMP` | — | first source | second source | 0/1 |
| `0x2A..0x2C` | `AND`, `OR`, `XOR` | destination | first source | second source | 0/1 |
| `0x2D` | `NOT` | destination | source | — | 0 |
| `0x2E` | `NEG` | destination | source | — | 0 |
| `0x30..0x34` | `SHL`..`ROR` | destination | first source | second source | 0/1 |
| `0x38` | `JMP` | — | register target | — | 0 = register, 1 = literal |
| `0x39` | `JCOND` | — | register target | condition | 0/1 |
| `0x3A` | `CALL` | — | register target | — | 0/1 |
| `0x3B` | `RET` | — | — | — | — |
| `0x3C` | `PUSH` | source | — | — | — |
| `0x3D` | `POP` | destination | — | — | — |
| `0x3E` | `ADJSP` | — | — | — | 1 |
| `0x3F` | `SETSP` | — | source | — | — |
| `0x40` | `INT` | — | — | — | 1 |
| `0x41` | `SYSCALL` | — | — | — | 1 |
| `0x42` | `IRET` | — | — | — | — |
| `0x43` | `HALT` | — | — | — | — |
| `0x44` | `BRK` | — | — | — | — |
| `0x45` | `NOP` | — | — | — | — |
| `0x46` | `LL` | destination | base | mode | 1 |
| `0x47` | `SC` | data | base | mode | 1 |
| `0x48` | `FENCE` | — | — | — | — |
| `0x49` | `RDCR` | destination | — | control register | — |
| `0x4A` | `WRCR` | data | — | control register | — |
| `0x4B` | `RDSP` | destination | — | — | — |
| `0x4C` | `WRSP` | data | — | — | — |
| `0x4D` | `PUSHR` | — | — | — | — |
| `0x4E` | `POPR` | — | — | — | — |
| `0x4F` | `PUSHFP` | — | — | — | — |
| `0x50` | `FLOAD` | FP destination | base | mode | 0 = `.W`, 1 = `.D` |
| `0x51` | `FSTORE` | FP source | base | mode | 0 = `.W`, 1 = `.D` |
| `0x52` | `MLOAD` | tile | base | mode | 0 = matrix, 1 = vector |
| `0x53` | `MSTORE` | tile | base | mode | 0 = matrix, 1 = vector |
| `0x60` | FP escape | `Fd` | `Fs1` | `Fs2` | — |
| `0x61` | matrix escape | `Td` | `Ta` | `Tb` | — |
| `0x62` | byte/bit escape | `Rd` | `Rs1` | width-1 | — |

Reserved ranges, all raising `#UD`: `0x0B..0x0F`, `0x1A..0x1F`, `0x2F`,
`0x35..0x37`, `0x54..0x5F`, and `0x63..0x7F`.

`E` is set only by the strided `MLOAD`/`MSTORE` form and by instructions whose
payload needs more than 11 bits; every other instruction leaves it clear. The
extension word is little-endian and always follows the base word.

The table gives the fields as this assembler emits them. `L` is *decoded* only
where it distinguishes something a programmer can write: `MOV` and the ALU
group (register versus immediate), `JMP`/`JCOND`/`CALL` (register versus literal
target), and the `FLOAD`/`FSTORE`/`MLOAD`/`MSTORE` width selectors. On `LOAD`,
`STORE`, `LEA`, `LL`, and `SC` the machine reads the payload as a displacement
regardless of `L`; the assembler sets it to 1 anyway, and a hand-encoded word
with `L = 0` behaves the same.

### 12.16 Verified encodings

Every word below is produced by this assembler and accepted by this emulator; a
listing from `kr32 as ... --listing` shows the same values.

| Instruction | Encoding |
| --- | --- |
| `MOV R1, #42` | `0x0020_082A` |
| `ADD R3, R1, R2` | `0x4062_4000` |
| `SLT R1, R2, R3` | `0x3024_6000` |
| `SLTU R1, R2, R3` | `0x3224_6000` |
| `LOAD R4, [R5 + 8]` | `0x028A_6808` |
| `LOAD8U R4, [R5 + 8]` | `0x068A_6808` |
| `STORE R6, [R7]` | `0x04CE_4800` |
| `LL R1, [R2]` | `0x8C24_4800` |
| `SC R1, [R2]` | `0x8E24_4800` |
| `JMP 0x0040_0000` | `0x7000_1800`, `0x0040_0000` |
| `CALL 0x0040_0000` | `0x7400_1800`, `0x0040_0000` |
| `JZ R7` | `0x720E_0000` |
| `RET` | `0x7600_0000` |
| `HALT` | `0x8600_0000` |
| `ADJSP #-16` | `0x7C00_0FF0` |
| `FADD.S F1, F2, F3` | `0xC024_6000` |
| `MMUL T1, T2, T3` | `0xC224_6000` |
| `BSWAP R1, R2` | `0xC424_0000` |
| `PUSHR` | `0x9A00_0000` |
| `POPR` | `0x9C00_0000` |
| `PUSHFP` | `0x9E00_0000` |
| `POPFP` | `0x1400_0000` |

`MOV R1, #42` decomposes as opcode `0x00`, `Rd = 1`, `L = 1`, inline `42`;
`ADD R3, R1, R2` as opcode `0x20`, `Rd = 3`, `Rs1 = 1`, `field = 2`, `L = 0`.

---

## 13. Implementation notes

Differences between this reference and [isa-v1.md](isa-v1.md), all verified
against the built executable:

- The assembler has no `LOADX`/`STOREX`/`LOAD8UX`/... mnemonics. Base+index is
  written `[Rs1 + Ri + disp]` with the normal load/store mnemonic; the
  disassembler renders the `*X` names.
- A misaligned or overflowing `SP` on `PUSH`, `POP`, `PUSHR`, `POPR`,
  `PUSHFP`, `POPFP`, or `CALL` raises `#GP`, not `#ALIGN`. `RET`, `ADJSP`, and
  `SETSP` raise `#ALIGN`; `IRET` raises it only for an unaligned restored `PC`.
- Strided `MLOAD`/`MSTORE` is meaningful only with the bare register form
  (`[Rs], #stride`); with an explicit displacement the stride is also added to
  the effective address (§12.11).
- The object assembler (used by `kr32 as --object`, `kr32 ld`, and
  `kr32 build`) rejects `ROL`, `ROR`, `RDSP`, `WRSP`, and base+index operands.
  Use the raw `as` path for those (§3.6).
- With paging off, the device window and framebuffer aperture are not decoded;
  an access there raises `#GP` (§8.4).
- A fault that cannot be delivered halts the core and the CLI reports
  `HALTED exit=1` rather than printing a `FAULT` line; use `--trace` or a
  vector table to see it.
