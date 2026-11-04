# KR32

KR32 is a 32-bit teaching computer: an ISA, emulator, assembler, linker, C/C++
toolchain, debugger, and one `kr32` executable that contains the whole system.

The design and decision record is in [KR32-plan.md](KR32-plan.md). The working
checklists, dependency notes, test gates, and running log are in
[implementation.md](implementation.md). The instruction encoding draft is in
[docs/isa-v1.md](docs/isa-v1.md).

**Status:** M9 complete (2026-11-02). The C and C++ front ends coexist in the
same `kr32` executable and share one freestanding libc/runtime. The C compiler
supports integer, `short`/`unsigned short`, `unsigned int`/`unsigned char`,
`long` (32-bit), `long long` (64-bit storage/arithmetic/functions, divide/modulo, and `printf` formats), `float`, and
`double` scalars with FP calls, expressions, multidimensional arrays, global and
local aggregate/array initializers, designated initializers, compound
assignment, `++`/`--`, conditional
expressions, `do`-`while`, and switch statements. The C++ subset covers classes
with type-aware constructor selection,
inheritance, virtual dispatch/RTTI, function and class templates with explicit
specializations (full and pointer/array/nested-pattern partial), COMDAT groups for template functions and data, integer non-type parameters and dependent array-bound expressions, template static data and function-local statics merged across translation units, 64-bit `long long` locals/functions/globals/fields/static/arrays/references/thread-locals/arithmetic/compound-assignment/casts/float-conversion/wide-shifts/`printf`, matrices,
exceptions/unwinding, `new`/`delete`, callable `std::thread` (function pointers, captureless and capturing lambdas, with cluster runtime coverage for all three, mutex synchronization, shared globals, detached workers, `std::move` construction, and handle transfer via release/adopt) /`std::thread::id`/`kr32::thread::id`/TLS/atomics, coarse timed `try_lock_for`/`sleep_for`/`sleep_until`/condition `wait_for`,
per-thread
`thread_local` construction/destruction with dynamic startup initialization, conditional
expressions, switch statements, global and local aggregate initializers,
designated initializers, access control (including friends), unsigned integer
semantics, `std::initializer_list` parameters, constructors, braced and empty returns, `auto` variables with full return-type deduction, 64-bit elements including range-for, and begin/end iteration,
captureless and
value/reference/default-capturing lambdas,
by-value class parameters/returns with memory-class `R4` destinations (integer aggregates up to eight bytes in integer registers, FP-only aggregates up to sixteen bytes in FP registers; larger or mixed aggregates use hidden pointers), exit-time static destruction,
range-based `for` over arrays, braced initializer lists, and
`initializer_list` variables, `goto`/labels, and
C++ debug records (including template and lambda functions). Template support and limitations are summarized in
[docs/templates.md](docs/templates.md), and overall compiler roadmap status is
in [docs/roadmap.md](docs/roadmap.md). A
freestanding C libc supplies `printf` (including FP formats),
coalescing `malloc`/`free`, `calloc`, `realloc`, `stddef.h`
(`sizeof`, `offsetof`, `_Alignof`, `_Static_assert`), math builtins, LL/SC
atomics, and string/memory routines; see [docs/libc.md](docs/libc.md). The
debugger opens at 1440x900 and uses embedded Inter and JetBrains Mono fonts,
separate source-line and instruction stepping controls, responsive resizing,
KR16-style gutters with breakpoint toggles and a Run to line/address context
menu, a HEX word column, disassembly hover help for instructions and addressing
modes, an expandable CORES panel, a four-line memory dump docked under the
source pane with an editable start address, and always-visible variables,
call-stack, and console panels. See [docs/devices.md](docs/devices.md) and
[docs/ui.md](docs/ui.md). Remaining C++ work and known integration gaps are
tracked in [cpp-impl.md](cpp-impl.md) and
[cpp-handoff.md](cpp-handoff.md).

## Build

### Windows

CMake and Ninja are bundled with Visual Studio 2026 but are not on `PATH` in a
plain shell. Use a Developer PowerShell or the full paths:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

& $cmake --preset windows-msvc
& $cmake --build --preset windows-msvc-debug
& $ctest --preset windows-msvc-debug
```

SDL2 is installed locally by `scripts/install-sdl2.ps1`; see
[third_party/README.md](third_party/README.md).

### WSL / Linux

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libsdl2-dev clang git
cmake --preset linux-gcc
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

For Clang, use `linux-clang`. For a native ARM64 Linux machine, use
`linux-arm64-gcc`.

### Headless

```bash
cmake --preset linux-asan      # UI off, ASan + UBSan
cmake --build --preset linux-asan
ctest --preset linux-asan
```

`KR32_BUILD_UI=OFF` removes SDL2 from the build; the CLI and tests still run.

## Commands

```text
kr32 as examples/demo.asm -o demo.bin --listing demo.lst
kr32 as examples/hello.s --object -o hello.o
kr32 ld hello.o -o hello.k32 --map hello.map
kr32 ld kernel.o --flat --base 0x20000 -o kernel.bin
kr32 size hello.k32
kr32 build examples/hello.s -o hello.k32 --run
kr32 cc examples/hello.c -S -o hello.s
kr32 build examples/hello.c -o hello-c.k32 --run
kr32 build examples/mainentry.c --run --arg one --arg "two words"
kr32 build examples/types.c -o types-c.k32 --run
kr32 build examples/floats.c -o floats-c.k32 --run
kr32 build examples/varargs.c -o varargs-c.k32 --run
kr32 build examples/libc-test.c -o libc-c.k32 --run
kr32 build examples/strings.c -o strings-c.k32 --run
kr32 build examples/binarytree.c -o binarytree-c.k32 --run
kr32 cxx examples/hello.cpp -S -o hello-cxx.s
kr32 build examples/hello.cpp -o hello-cxx.k32 --run
kr32 build examples/cxx_libc.cpp -o cxx-libc.k32 --run
kr32 gui examples/libc-test.c
kr32 dis demo.bin
kr32 run examples/demo.asm
kr32 run examples/fp-demo.asm
kr32 run examples/matrix-demo.asm
kr32 run examples/demo.asm --trace --stats
kr32 as examples/boot-halt.asm -o boot-halt.bin --base 0xFFFF0000
kr32 vm examples/demo.asm --bios boot-halt.bin
kr32 version
kr32 help
kr32 --smoke-ui     # initialize and tear down the SDL2 platform
kr32 gui examples/hello.c
kr32 gui build/hello.k32 --headless --screenshot hello-ui.ppm
kr32 shell
kr32 dap --stdio build/hello-c.k32
kr32 vm build/fileio.k32 --fs-root sandbox --read-only
```

`as` writes a raw image or, with `--object`, a relocatable `.k32` object. `ld`
merges objects, resolves symbols and relocations, and writes an executable
`.k32`; `--flat` writes a headerless image beginning at `--base`, with gaps and
BSS zero-filled; `--map` writes the section/symbol map. `cc` compiles C to
assembly or an object. `build` runs compile/assemble, link, and optional
`--run` in one command. `size` reports `.k32` sections and `debug` dumps the
`.debug_info` records. `gui` opens the source/disassembly debugger or renders
headless PPM
screenshots. `shell` runs tool commands interactively or from piped input, and
`dap` exposes the debugger to DAP clients. `cxx` compiles a C++ source to
assembly or an object, and `build file.cpp` compiles, links the shared
C/C++ libc, and optionally runs the result.

## Layout

```text
include/kr32/      public headers
src/core/          ISA encoding and CPU/machine
src/asm/           assembler
src/container/     .k32 object/image format
src/link/          linker
src/cc/ src/cxx/   C and C++ compilers
src/dis/           disassembler
src/debug/         debug information and DebugSession
src/vm/            runner and host services
src/dap/           Debug Adapter Protocol
src/ui/            portable windowed debugger (no OS/SDL headers)
src/platform/      SDL2 window and OS boundary
src/cli/           dispatcher and build pipeline
tests/             unit, integration, boundary
cmake/             build helpers and the platform-boundary check
third_party/       vendored headers and local SDK notes
```

## Dependencies

- C++20 compiler: MSVC 2022+, GCC 12+, or Clang 15+.
- CMake 3.25+ and Ninja.
- SDL2 2.0.20+ for the windowed debugger; see
  [third_party/README.md](third_party/README.md) or
  `sudo apt install libsdl2-dev pkg-config`.
- doctest 2.4.11, vendored as `third_party/doctest.h` (MIT).
- Git.

No network access is required at configure time. Install the system packages
first; the only local download script is the Windows SDL2 bootstrap.
