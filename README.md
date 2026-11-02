# KR32

KR32 is a 32-bit teaching computer: an ISA, emulator, assembler, linker, C/C++
toolchain, debugger, and one `kr32` executable that contains the whole system.

The design and decision record is in [KR32-plan.md](KR32-plan.md). The working
checklists, dependency notes, test gates, and running log are in
[implementation.md](implementation.md). The instruction encoding draft is in
[docs/isa-v1.md](docs/isa-v1.md).

**Status:** M4 in progress (2026-11-02). The bootstrap C compiler handles
integers, chars, pointers, arrays, functions, control flow, strings, and
recursion; `kr32 cc` emits assembly or an object, and `kr32 build file.c --run`
links the `_start`/`putchar`/`exit` runtime and runs the result. Structs,
`switch`/`goto`, the full preprocessor, and the remaining M4 gate are still
being completed.

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
kr32 size hello.k32
kr32 build examples/hello.s -o hello.k32 --run
kr32 cc examples/hello.c -S -o hello.s
kr32 build examples/hello.c -o hello-c.k32 --run
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
kr32 gui            # M6 placeholder for the windowed debugger
```

`as` writes a raw image or, with `--object`, a relocatable `.k32` object. `ld`
merges objects, resolves symbols and relocations, and writes an executable
`.k32`; `--map` writes the section/symbol map. `cc` compiles C to assembly or an
object. `build` runs compile/assemble, link, and optional `--run` in one
command. `size` reports `.k32` sections. The remaining tool commands (`cxx`,
`debug`, `dap`, `shell`) are stubs for later milestones.

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
