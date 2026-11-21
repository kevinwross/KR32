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

KR32 compiles guest C and C++ with the installed **krc** toolchain; there is no
in-tree compiler, assembler or linker. Configure with the `krc-toolchain` package
prefix (KRCompiler 4.12 or later, built with `KRC_BUILD_TOOLS=ON`). The package
exports `KRC_SYSROOT` and `KRC_KR32_RUNTIME_DIR`; configure prints both and fails
naming any missing sysroot or runtime file.

### Windows

CMake and Ninja are bundled with Visual Studio 2026 but are not on `PATH` in a
plain shell. Use a Developer PowerShell or the full paths:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

& $cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=<krc install>
& $cmake --build --preset windows-msvc-debug
& $ctest --preset windows-msvc-debug
```

SDL2 is installed locally by `scripts/install-sdl2.ps1`; see
[third_party/README.md](third_party/README.md).

### WSL / Linux

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libsdl2-dev clang git
cmake --preset linux-gcc -DCMAKE_PREFIX_PATH=<krc install>
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

For Clang, use `linux-clang`. For a native ARM64 Linux machine, use
`linux-arm64-gcc`.

### Headless

```bash
cmake --preset linux-asan -DCMAKE_PREFIX_PATH=<krc install>   # UI off, ASan + UBSan
cmake --build --preset linux-asan
ctest --preset linux-asan
```

`KR32_BUILD_UI=OFF` removes SDL2 from the build; the CLI and tests still run.
`KR32_EMBED_OS=OFF` builds without the embedded KR32OS images and requires
explicit `--os`/`--bios` paths. `KR32_BUILD_TESTS=ON` (the default) builds the
test images through the same krc package.

The guest console header is `<kr32/cons.h>`; the former `<kr32/con.h>` name is
retired because Windows reserves it.

`main` is the integration branch. The ARM64 tester polls `main` and records its
runs in `docs/arm64-test-log.md`; `adopt-krc` is kept as the completed krc
adoption branch and is no longer polled.

## Commands

The `kr32` executable is a runner and debugger. It loads `.k32` images; it does
not assemble, link or compile them. The window's **Build** compiles a `.c` or
`.cpp` source with krc, and the same toolchain builds guest programs outside the
window.

```text
kr32 dis <image> [--base <addr>] [--start <addr>] [--max-words <n>]
kr32 vm <file.k32> [--max-steps <n>] [--trace] [--stats] [--arg <value>] [--input <text>]
kr32 run <file.k32>
kr32 size <file.k32>
kr32 debug <file.k32>
kr32 gui <source-or-image> [--headless] [--screenshot <path>]
kr32 gui --os --embedded --headless --screenshot os-ui.ppm
kr32 os [kernel.k32] --bios <bios.k32> [--serial-input <text>]
kr32 os --embedded
kr32 shell
kr32 dap --stdio <image.k32>
kr32 version
kr32 help
kr32 --smoke-ui
```

`vm` runs a `.k32`; `run` is its alias. `size` reports sections and `debug`
dumps the `.debug_info` records. `gui` opens the source/disassembly debugger or
renders a headless PPM screenshot. `os` boots a KR32OS kernel with a BIOS and
resolves the default artifacts from `KR32OS_BIN`, `KR32OS_BIOS`, or the embedded
BIOS/kernel pair. The shipped images under `src/resources/os/<platform>` are the
fixtures the tests use.

## Layout

```text
include/kr32/      public headers
src/core/          ISA encoding and CPU/machine
src/container/     .k32 object/image format
src/dis/           disassembler
src/debug/         debug information and DebugSession
src/vm/            runner and host services
src/dap/           Debug Adapter Protocol
src/ui/            portable windowed debugger (no OS/SDL headers)
src/platform/      SDL2 window and OS boundary
src/cli/           command dispatcher and runner
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
