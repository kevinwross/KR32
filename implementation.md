# KR32 Implementation Plan

> **Status:** active. This file is the working checklist, milestone gate, dependency
> record, and running log for KR32.
>
> **Last updated:** 2026-11-15 16:52 -08:00
>
> **Related:** [KR32-plan.md](KR32-plan.md) (architecture and decisions),
> [docs/isa-v1.md](docs/isa-v1.md) (encoding), [kr16-final-plan.md](kr16-final-plan.md)
> (product-reference model).

## 0. How to use this file

- `[ ]` is a task that is not done. `[x]` is done. Do not delete completed items;
  the history is useful.
- A milestone is complete only when **every** task is checked, the test gate
  passes on Windows and WSL (and ARM64 when available), the affected docs are
  updated, the work is committed, and the running log has an entry.
- Update the checklists after every work session, not only at milestone ends.
- If a task turns out to be deferred, move it to **Deferred** with a one-line
  reason rather than silently dropping it.
- Add new tasks as they are discovered. Keep them under the milestone that owns
  them.
- Timestamp log entries with the local date and time, using
  `local-datetime` when working with the agent. The format is
  `YYYY-MM-DD HH:MM ±HH:MM — entry`.

## 1. Environment and facts

Recorded 2026-11-02 on the primary development host.

| Item | Value |
| --- | --- |
| Host OS | Windows |
| Visual Studio | 2026 Community, MSVC 14.51.36231 |
| CMake (Windows) | 4.3.1-msvc1, bundled with Visual Studio |
| Ninja (Windows) | 1.13.2, bundled with Visual Studio |
| WSL | Ubuntu 26.04 LTS on WSL2 |
| GCC (WSL) | 15.2.0 |
| Clang (WSL) | 21.1.8 |
| CMake (WSL) | 4.2.3 |
| Ninja (WSL) | 1.13.2 |
| Git | 2.55.0.windows.3 on Windows, 2.53.0 in WSL |
| Git identity | `kevinwross <kevinro@live.com>` (global) |
| Repository | `C:\Users\kevin\source\repos\KR32` |
| WSL path | `/mnt/c/Users/kevin/source/repos/KR32` |
| Future target | native Linux ARM64 build; no cross-compilation |

Notes:

- CMake and Ninja are **not on `PATH`** in a plain PowerShell. They live at:
  - `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
  - `ctest.exe` is beside `cmake.exe`.
  Use a Developer PowerShell, a helper script, or the full paths.
- WSL dependency check passed on 2026-11-02: `pkg-config` 2.5.1 and
  `libsdl2-dev` 2.32.10 are installed, `/usr/include/SDL2/SDL.h` exists, and
  GCC/Clang C++20 plus an SDL2 link test succeed.
- Windows dependency check on 2026-11-02: CMake, Ninja, `ctest`, MSVC, Python
  3.14.6, and Git are present, and a C++20 concepts smoke test compiles and runs.
  SDL2 2.32.10 is installed at `third_party/SDL2`, `SDL2_DIR` is set, and a
  minimal CMake + SDL2 link/run test prints `SDL 2.32.10`.
- WSL is available with full local access; Linux builds run directly in Ubuntu.
  This machine is the reference Windows/WSL development host.
- The repository is local and uses the global Git identity. No remote is
  configured yet; checkpoints are local commits and tags.

## 2. Dependencies and installation

### 2.1 Required

| Dependency | Minimum / pin | Needed for | Windows | WSL / Ubuntu / ARM64 |
| --- | --- | --- | --- | --- |
| Git | 2.40+ (2.55 / 2.53 present) | versioning | Visual Studio or git-scm | `apt install git` |
| CMake | 3.25+ (4.3.1 / 4.2.3 present) | build | bundled with VS | `apt install cmake` |
| Ninja | 1.11+ (1.13.2 present) | build | bundled with VS | `apt install ninja-build` |
| C++20 compiler | MSVC 2022+, GCC 12+, Clang 15+ | build | VS 2026 workload | `build-essential`, `clang` |
| SDL2 development | 2.0.20+ (2.32.10, both platforms) | GUI | `scripts/install-sdl2.ps1` or `SDL2-devel` | `apt install libsdl2-dev` |
| pkg-config | any | Linux SDL2 discovery | not needed | `apt install pkg-config` |
| doctest | 2.4.x, vendored | unit tests | `third_party/doctest.h` | same |
| Python 3 | 3.10+ | helper scripts, only if used | python.org or VS | `apt install python3` |

### 2.2 Optional / later

| Dependency | When | Notes |
| --- | --- | --- |
| JSON library | DAP work | Prefer a vendored MIT single-header; record the exact version and license here before adding it |
| KR16 font atlas | GUI work | Reuse the subsetted fonts and license notices from KR16 |
| stb_truetype | GUI work | Public domain/MIT single-header TTF rasterizer |
| Inter / JetBrains Mono | GUI work | SIL OFL 1.1; embedded TTF glyph sources |
| VS Code + DAP extension | M7 | Debugging convenience, not a build dependency |
| EDG or another C++ front end | M9 | Apache-2.0 with LLVM exceptions if EDG; not installed yet |

### 2.3 Windows setup

**Status 2026-11-02:** all required dependencies are installed. SDL2 2.32.10 is
in `third_party/SDL2`, `SDL2_DIR` points at `third_party/SDL2/cmake`, and a
CMake link/run test succeeds.

1. Visual Studio Installer → **Desktop development with C++** and the CMake
   tools component. VS 2026 Community is already installed.
2. Prefer the bundled CMake/Ninja paths above, or add them to the user `PATH`.
3. SDL2:
   - Run `powershell -ExecutionPolicy Bypass -File scripts/install-sdl2.ps1`.
     It downloads `SDL2-devel-2.32.10-VC.zip` and extracts it to
     `third_party/SDL2` (gitignored).
   - Set `SDL2_DIR` to `third_party/SDL2/cmake`, or let the CMake preset do it
     with `${sourceDir}/third_party/SDL2/cmake`.
   - Define `SDL_MAIN_HANDLED` for the normal `main` entry point and link
     `SDL2::SDL2`; the DLL must be next to the executable at run time.
   - Include `<SDL.h>`; this VC package has a flat include directory. The CMake
     target adds it, and Linux works the same through pkg-config.
4. Confirm:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --version
   & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" --version
   ```

### 2.4 WSL / Ubuntu setup

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libsdl2-dev clang git
```

Verify:

```bash
cmake --version
ninja --version
gcc --version
clang --version
pkg-config --modversion sdl2
```

### 2.5 Linux ARM64 setup

Same package list as WSL. Build natively on the ARM64 machine; do not
cross-compile. CMake presets must not assume x86-64.

### 2.6 Dependency policy

- No network access at configure time. Install system packages or vcpkg
  dependencies first.
- Small header-only libraries (doctest, JSON) are vendored under
  `third_party/`, with their license text.
- SDL2 is a system/vcpkg dependency, not vendored.
- Pin versions in CI and record upgrades in the running log.
- License summary: SDL2 zlib, doctest MIT, KR16 fonts OFL, JSON MIT if
  nlohmann/json, EDG Apache-2.0 with LLVM exceptions (later).

## 3. Repository and Git workflow

- Branch: `main`.
- The local repository uses the global identity
  `kevinwross <kevinro@live.com>`.
- `.gitattributes` normalises source to LF; `.sln`/`.bat`/`.cmd` stay CRLF.
- `.gitignore` excludes build trees, CMake output, IDE state, and CMake user
  presets.
- Commit messages:
  - `M0: scaffold CMake and CLI`
  - `M1: integer core and assembler`
  - `docs: update implementation checklist`
- Tag a milestone only after its gate passes: `m0`, `m1`, `m2`, `m2b`, `m3`,
  `m4`, `m5`, `m6`, `m7`, `m8`, `m9`.
- Build in `build/`, `out/`, or `dist/`; never commit build products.
  Small golden fixtures are the exception and must be listed in the test plan.
- WSL builds may use `/mnt/c/...` (shared, slower) or a clone in
  `~/src/KR32` (faster). The Git history is the source of truth; keep one
  working copy for edits to avoid confusion.
- Do not commit credentials, machine-specific paths in CMake files, or
  generated `CMakeUserPresets.json`.

## 4. Milestone map

C is on the main path; C++ is a later add-on. The assembler, linker,
container, debug format, and ABI are designed for C++ from the start.

| ID | Milestone | Gate |
| --- | --- | --- |
| M0 | Foundation and scaffold | one `kr32` binary, CI, docs, git |
| M1 | Machine core, assembler, disassembler | integer program runs headless |
| M2 | Privilege, exceptions, MMU, boot, multicore hooks | user mode, faults, paging, BIOS |
| M2b | FP and M extension | FP and matrix demos pass |
| M3 | Container, linker, `build` | `.k32` objects/linker, `kr32 build` runs |
| M4 | Simple C compiler and minimal runtime | C hello world runs |
| M5 | Debug information, `DebugSession`, CLI debugger | source-level model works |
| M6 | Windowed debugger | KR16 UI adapted to SDL2; debug a C program |
| M7 | Host services, sandbox, shell, DAP | file I/O, shell, VS Code DAP |
| M8 | Devices, time, full multicore | timer/block/console; two-core test |
| M9 | C++ toolchain | C++ smoke tests through `kr32_cxx` |
| M10 | Display device, host surface, console | framebuffer v2, `kr32 display`, bootable console |
| M11 | Graphics and console API | guest `gfx_*`/`con_*` examples run |

## 5. Milestones and tasks

### M0 — Foundation and scaffold

**Goal:** a repository that builds one `kr32` executable on Windows and WSL,
with the module boundaries, CI, and test harness in place.

**Status:** code complete 2026-11-02. The local gate passes on Windows and WSL.
The CI workflow is in place; its first remote run is pending because no remote
is configured yet.

**Tasks**

- [x] Initialise the local Git repository with the global identity, branch
      `main`, and an initial commit.
- [x] Add `.gitignore` and `.gitattributes` (LF source).
- [x] Add `implementation.md` and link it from `KR32-plan.md`.
- [x] Verify the Windows and WSL toolchains (2026-11-02): CMake, Ninja, MSVC,
      GCC, Clang, Python, Git, and WSL SDL2 are present; Windows C++20 and WSL
      C++20/SDL2 smoke tests pass.
- [x] Install SDL2 development files on Windows: 2.32.10 in
      `third_party/SDL2` via `scripts/install-sdl2.ps1`; `SDL2_DIR` set.
- [x] Add `README.md` with the one-command build quick start.
- [x] Add `CMakeLists.txt` and `CMakePresets.json`:
  - `windows-msvc`, `windows-ninja`
  - `linux-gcc`, `linux-clang`
  - `linux-arm64-gcc`
  - `linux-asan` (ASan + UBSan)
- [x] C++20, warnings-as-errors, no compiler-specific extensions.
- [x] `KR32_BUILD_UI` option, default ON; `KR32_BUILD_TESTS` option, default ON.
- [x] Skeleton libraries with the agreed dependency edges:
      `kr32_core`, `kr32_asm`, `kr32_link`, `kr32_container`, `kr32_cc`,
      `kr32_cxx` (stub), `kr32_dis`, `kr32_debug`, `kr32_vm`, `kr32_dap`,
      `kr32_ui`, `kr32_platform`, `kr32_cli`.
- [x] `kr32` executable with a CLI dispatcher: `version`, `help`, and stubs for
      `as`, `ld`, `build`, `cc`, `cxx`, `vm`, `dis`, `size`, `debug`, `dap`,
      `shell`.
- [x] Platform-boundary check: `src/ui` contains no OS or SDL headers;
      `src/platform` is the only OS/SDL boundary.
- [x] Vendor `third_party/doctest.h`; add a `kr32_tests` target and one smoke
      test.
- [x] Golden-vector harness skeleton: encode/decode API plus the first vectors
      from `docs/isa-v1.md`.
- [x] CI workflow:
  - Windows MSVC Debug and Release.
  - Ubuntu GCC and Clang.
  - Linux ARM64 native.
  - Sanitizers.
  - UI ON and UI OFF.
- [x] CMake install/package placeholder and a `dist/` convention.
- [x] Tag `m0` after the local gate passes.

**Test plan and gate**

- [x] Windows: configure and build `windows-msvc` (Debug and Release) and
      `windows-ninja` (Debug and Release); `ctest` passes.
- [x] WSL: configure and build `linux-gcc` and `linux-clang`; `ctest` passes.
- [x] `KR32_BUILD_UI=OFF` configures and builds without SDL2.
- [x] `KR32_BUILD_UI=ON` configures and builds with SDL2.
- [x] `kr32 version` prints the version tuple.
- [x] `kr32 help` lists every command.
- [x] Boundary check fails on an intentional OS include in `src/ui` and passes
      on the clean tree.
- [ ] CI is green on every configured job. The workflow exists; the first remote
      run needs a GitHub push.
- [x] Commit and tag `m0`; log entry added.

### M1 — Machine core, assembler, disassembler

**Goal:** execute a hand-written integer program headless and round-trip every
base encoding.

**Status:** complete 2026-11-02. The integer machine, assembler, disassembler,
raw-image CLI, symbol listings, demo, and tests pass on Windows and WSL.

**Tasks**

- [x] `kr32_core`: GPRs, PC, SP, FLAGS, tick counter, reset state.
- [x] Memory map and identity access; guard region; RAM and ROM.
- [x] Base integer ISA from `docs/isa-v1.md`: `MOV`, loads/stores (all widths
      and indexed forms), `LEA`, ALU (`SLT`/`SLTU`/`MULH`/`MULHU`/`DIVS`/
      `MODS`), shifts, `JCOND`/`JMP`/`CALL`/`RET`, `PUSH`/`POP`/`ADJSP`, `NOP`,
      `BRK`, `HALT`.
- [x] Flag semantics for every ALU and shift instruction; `#DIV0`, `#UD`,
      `#ALIGN` stubs.
- [x] Assembler: case-insensitive mnemonics, labels, `#` immediates, memory
      forms, `.word`, `.byte`, `.org`, `.text`, `.data`, `.align`, `.space`,
      `.asciz`, `.equ`, `.global`, `.extern`.
- [x] Disassembler: base ISA, symbol labels, optional hex bytes.
- [x] CLI: `kr32 as`, `kr32 dis`, `kr32 vm`/`run` with `--max-steps`,
      `--trace`, `--stats`.
- [x] Golden vectors for every base opcode, including 4-byte and 8-byte forms.
- [x] Unit tests: ALU flags, multiply/divide edge cases, shifts, stack,
      addressing modes, branch conditions.
- [x] Property test: random instruction words decode without crashing.
- [x] Integration: an assembly demo that computes a result and exits; the VM
      reports registers and exit code.
- [x] Determinism: the same image and inputs produce identical state twice.

**Test plan and gate**

- [x] Golden encode/decode tests pass.
- [x] ALU/shift/branch/stack unit tests pass.
- [x] Assembler/disassembler round-trip every opcode.
- [x] `kr32 run examples/demo.asm` reaches the expected result.
- [x] Property test runs at least 10^5 random words.
- [x] Windows and WSL test suites pass.
- [x] Commit and tag `m1`; log entry added.

### M2 — Privilege, exceptions, MMU, boot, multicore hooks

**Goal:** a user-mode-capable machine with paging, faults, and the multicore
foundation.

**Status:** complete 2026-11-02. The boot test prebuilds the identity page
tables in the harness; the BIOS installs `PTBR`/`VTBR`/`MCTL`, enables paging,
and `IRET`s into user code, which then traps through `SYSCALL`. Moving page-table
construction into the ROM itself is deferred to the boot/loader work in M3/M7.

**Tasks**

- [x] Exception vectors, delivery frame, `IRET`, `ESR`, `FAR`, `#DF`.
- [x] 4 KiB two-level MMU: PTE flags, permissions, `A`/`D`, `PTBR`, `MCTL`.
- [x] User/kernel mode, `KSP`, `VTBR`, privileged instructions, `#GP`.
- [x] Boot ROM/BIOS: reset vector, identity map, paging enable, jump to kernel.
- [x] `LL`/`SC`/`FENCE`, per-core state, `COREID`, inter-processor block hook.
- [x] CLI flags: `--ram-size`, `--bios`, `--max-steps`, `--trace`.
- [x] Unit/integration tests for every fault and permission case.
- [x] Two-core bring-up smoke test.

**Test plan and gate**

- [x] `#PF` error code and `FAR` are correct for load/store/fetch/user.
- [x] Guard region faults in both paging modes.
- [x] `IRET` restores mode and `SP`.
- [x] `LL`/`SC` works on aligned RAM; unaligned/non-RAM faults as specified.
- [x] BIOS boots a user program end to end.
- [x] Windows and WSL test suites pass.
- [x] Commit and tag `m2`; log entry added.

### M2b — FP and M extension

**Goal:** the scalar FP and 4x4 tile units from the ISA document.

**Status:** complete 2026-11-02. Scalar FP uses host IEEE arithmetic with
canonical NaNs and saturating conversions; the tile file, transform stack,
strided loads, assembler/disassembler support, and demos are in place.

**Tasks**

- [x] FPR file, `FLOAD`/`FSTORE`, FP escape, `.S`/`.D` arithmetic, compares,
      conversions, moves.
- [x] RNE, canonical NaN, denormals, no traps, saturating conversions.
- [x] Tile file, `MMUL`/`MMAC`/`MVEC`/`MTRANS`/`VDOT`.
- [x] `MLOAD`/`MSTORE`, `VLOAD`/`VSTORE`, strided loads, `MPUSH`/`MPOP`/`MDROP`.
- [x] FP and matrix demos: transform a vector, multiply two matrices, dot
      product, push/pop a transform.
- [x] Unit tests for FP flags/NaN/conversions and matrix results.

**Test plan and gate**

- [x] `FADD.S`/`FADD.D`, `FEQ`/`FLT`/`FLE`, conversions match the spec.
- [x] `MMUL` matches a reference 4x4 multiply; `MVEC` matches a reference.
- [x] Strided `MLOAD` reads a 4x4 block from a larger matrix.
- [x] Transform stack push/pop restores the parent matrix.
- [x] Windows and WSL tests pass.
- [x] Commit and tag `m2b`; log entry added.

### M3 — Container, linker, build pipeline

**Goal:** relocatable objects, a linker, a `.k32` image, and `kr32 build`.

**Status:** complete 2026-11-02. The object assembler currently covers the
integer instruction subset used by the M3 pipeline; FP/M and the full C-directed
directive set can extend the same section/symbol/relocation model without a
container change.

**Tasks**

- [x] Write `docs/container.md`: header, sections, symbols, relocations, debug
      sections, CRC.
- [x] Container reader/writer with round-trip tests.
- [x] Assembler emits relocatable objects with `.text`, `.rodata`, `.data`,
      `.bss`, symbols, and relocations.
- [x] Linker: layout, symbol resolution, relocation, map, strip; define
      `__heap_start`, `__stack_top`, `__stack_limit`.
- [x] CLI: `kr32 ld`, `kr32 size`, `kr32 build` for `.s` inputs.
- [x] Golden object and executable fixtures (generated and round-tripped in the
      test suite).
- [x] `build` pipeline: assemble → link → run.
- [x] C++ readiness: weak symbols, reserved COMDAT/section flags, relocation
      types, section merging, and extensible debug sections.

**Test plan and gate**

- [x] Container round-trip for every section and relocation type.
- [x] Two-object link resolves symbols and relocations correctly.
- [x] Linker map lists sections and symbols.
- [x] `kr32 build examples/hello.s --run` succeeds.
- [x] Windows and WSL tests pass.
- [x] Commit and tag `m3`; log entry added.

### M4 — Simple C compiler and minimal runtime

**Goal:** a C subset that compiles, assembles, links, and runs C programs.

**Status:** complete 2026-11-02 for the M4 bootstrap. The compiler covers the
decided scalar/aggregate subset, object/function macros, built-in includes,
conditionals, casts, structs/unions/enums/typedefs, arrays, recursion, `switch`
(no fallthrough yet), and `goto`. Struct/union values are passed by pointer
rather than by value in this bootstrap. The ABI is recorded in
[docs/abi.md](docs/abi.md); the dedicated IR/back-end split remains part of the
M9 C++ work.

**Tasks**

- [x] Freeze the C subset: `int`, `char`, `_Bool`, pointers, arrays, structs,
      unions, enums, typedefs, qualifiers, casts, control flow, functions,
      prototypes, string/character literals.
- [x] Preprocessor: object/function macros, `#include`, conditionals, `#error`,
      `#warning`; built-in headers.
- [x] Lexer, parser, type checker for the M4 subset.
- [x] C11 `_Static_assert` and `_Generic` type selection; `typeof`/`__typeof__`
      for type names and expressions; `_Alignas`; and
      `__attribute__((packed, aligned(N)))` struct layout.
- [x] Code generation to KR32 assembly; ILP32 frames (R7 frame pointer) and
      callee-saved frame handling.
- [x] `crt0`, `putchar`, `exit`, `memcpy`/`memset`/`strlen`; bump allocator.
- [x] Minimal `SYSCALL` console/exit path (full services in M7).
- [x] `kr32 cc`, `kr32 build file.c`; link the runtime automatically.
- [x] Diagnostics `file:line:column: message`.
- [x] C++ readiness: language-neutral object model, weak/COMDAT flags, shared
      ABI document, and debug-record extension points reserved for M9.

**Test plan and gate**

- [x] `kr32 build examples/hello.c --run` prints the expected text.
- [x] Arithmetic, pointers, arrays, structs, recursion, `switch`, `goto`.
- [x] Compiler diagnostics tests for common errors.
- [x] Runtime and ABI tests.
- [x] Windows and WSL tests pass.
- [x] Commit and tag `m4`; log entry added.

### M5 — Debug information, `DebugSession`, CLI debugger

**Goal:** the shared debug model that the GUI, DAP, CLI, and tests will use.

**Status:** complete 2026-11-02. The format and API are documented in
[docs/debug.md](docs/debug.md). Line records are statement-granular; debug
labels are resolved against the linked symbol table at load time.

**Tasks**

- [x] Debug sections: lines, sources, variables, frames, types, globals,
      strings.
- [x] Compiler emits line rows, variable/frame/type/global records; the object
      assembler and linker preserve `.debug_info`.
- [x] `DebugSession` API: load, breakpoints, step in/over/out, registers,
      memory, variables, call stack, console.
- [x] CLI `kr32 debug` dumps debug records; `vm --trace` remains.
- [x] Tests: line mapping, variable lookup, frames, breakpoints, stepping.

**Test plan and gate**

- [x] Source line lookup matches the compiled program.
- [x] Variables and frames are correct at a breakpoint.
- [x] Step over/in/out behave on a small C program.
- [x] Debug dump identifies files, functions, and types.
- [x] Windows and WSL tests pass.
- [x] Commit and tag `m5`; log entry added.

### M6 — Windowed debugger

**Goal:** the KR16-style GUI debugger, adapted to KR32 and SDL2.

**Status:** complete 2026-11-02. The UI is a portable, original canvas/widget
implementation with the KR16-style panel layout (no KR16 assets were copied).
Settings and a guest-argument editor are deferred to M7 host services; break
control is expressed through breakpoints.

**Tasks**

- [x] Portable UI layout, painting, widgets, hit testing, and an embedded
      Inter/JetBrains Mono TTF font atlas.
- [x] SDL2 backend; software RGBA canvas presented as an SDL texture.
- [x] Panels: source, disassembly, registers, variables, watches, memory,
      call stack, console.
- [x] KR32 panels: FPRs, tiles as 4x4 grids, MMU/page tables, transform stack,
      core selector.
- [x] Toolbar: Load/Build, Run, Breakpoint, Restart, counters, settings/guest
      arguments deferred.
- [x] Breakpoints, step over/in/out, error dialog.
- [x] Offscreen UI tests and PPM/golden-hash screenshots; dummy-driver smoke
      test.
- [x] `KR32_BUILD_UI=OFF` continues to build and test.

**Test plan and gate**

- [x] Debug a C program: source and disassembly follow the PC.
- [x] Breakpoints, stepping, registers, memory, variables, call stack,
      console.
- [x] Matrix/tile and MMU panels render correct values.
- [x] UI works on Windows and WSL (WSLg); headless build remains green.
- [x] Commit and tag `m6`; log entry added.

### M7 — Host services, sandbox, shell, DAP

**Goal:** a usable hosted runtime and the secondary debug interface.

**Status:** complete 2026-11-02. The DAP stdio and TCP transports were exercised
with protocol-level smoke tests; a VS Code installation was detected but no
KR32 VS Code extension/adapter package is part of this repository yet.

**Tasks**

- [x] `SYSCALL` ABI block: console, file I/O, time, random, exit.
- [x] Sandbox: `--fs-root`, quota, read-only, path rules.
- [x] Runtime wrappers for file I/O and stdio subset.
- [x] `kr32 shell`: commands, history, piped input.
- [x] DAP server over stdio/TCP sharing `DebugSession`; protocol smoke test.
- [x] Guest arguments, traces, stats.

**Test plan and gate**

- [x] C program reads and writes inside the sandbox; escapes are rejected.
- [x] Shell runs the tool commands and a scripted session.
- [x] DAP launch, breakpoints, stepping, memory, and evaluate work; VS Code
      packaging remains an M9 integration task.
- [x] Windows and WSL tests pass.
- [x] Commit and tag `m7`; log entry added.

### M8 — Devices, time, full multicore

**Goal:** deterministic devices and the complete multicore model.

**Status:** complete 2026-11-02. Device and cluster integration is documented in
[docs/devices.md](docs/devices.md). The GUI shows the selected core id; a full
multi-core selector/per-core panel layout remains a UI follow-up.

**Tasks**

- [x] Timer, block, and console devices; IRQ route table; MMIO.
- [x] Device timing on the tick clock; DMA.
- [x] Multicore scheduler, IPIs, `LL`/`SC` stress test.
- [x] GUI core-id display and per-core cluster tests; full selector is a UI
      follow-up.
- [x] Determinism tests for interrupts and devices.

**Test plan and gate**

- [x] Timer interrupt arrives at the expected tick.
- [x] Block DMA completes and raises the routed interrupt.
- [x] Two-core spinlock increments correctly; runs reproduce.
- [x] Windows and WSL tests pass.
- [x] Commit and tag `m8`; log entry added.

### M9 — C++ toolchain

**Goal:** a C++ compiler module linked into `kr32`, with C++ source debugging.

**Status:** implementation started 2026-11-02. The design contract is
[docs/cpp-plan.md](docs/cpp-plan.md); the implementation/task tracker is
[cpp-impl.md](cpp-impl.md). The `m9-cxx` worktree/branch is active; phases M9.1
(ABI/API/test harness), M9.2 (preprocessor, lexer, AST, parser, diagnostics),
M9.3 (scopes, name lookup, overloads, mangling, integer code generation), M9.4
(scalar `float`/`double`, FP calls, conversions, and stack varargs), M9.5
(classes, constructors/destructors, single inheritance), M9.6 (virtual dispatch
and RTTI), M9.7 (matrix/transform-stack codegen), M9.8 (function templates,
weak symbols, and static initialization), and M9.9 (`new`/`delete`, exceptions,
cleanup) are complete; M9.10 (threads/TLS/atomics) is complete. M9.11 (C++
debug/GUI/DAP) and M9.12 (CLI/cross-platform/tag) are complete for the
Windows/WSL gate; tag `m9` marks the final commit. Post-merge P0 fixes are on
`m9-p0`: multi-TU init/fini, TLS/EH merging, blocking guards, class static
locals, and debug thread-snapshot plumbing. C++ ABI revision 1 is frozen in
[docs/abi.md](docs/abi.md) with machine-checkable constants in
`include/kr32/cxx/abi.h`. The standalone API and `kr32-cxx` driver skeleton are
in place, and `compile()` now produces assembly for the supported integer,
scalar-FP, class, virtual/RTTI, matrix, function-template, allocation, and
exception subset. Confirmed decisions include custom C++11 subset, one
thread per core with cooperative oversubscription, `R11` TP, library matrices
plus intrinsics, `double` align 8/16-byte call boundaries, explicit per-thread
transform stacks, full destructor unwinding, scalar FP library, stack-based FP
varargs, Itanium-like mangling, and class-member/`this` debug support.

**Tasks**

- [x] Draft and review the M9 design contract, including the C++ subset, ABI,
      runtime, threads/multicore, debug, and test gates.
- [x] Approve the final language surface and stack-based FP varargs ABI.
- [x] Create the M9 implementation/task tracker (`cpp-impl.md`) for review.
- [x] Approve the tracker and create the `m9-cxx` worktree/branch.
- [x] Add the stable `kr32::cxx::compile` API, standalone `kr32-cxx` driver, and
      the `cxx_tool_help` CTest.
- [x] Freeze C++ ABI revision 1 in `docs/abi.md` and
      `include/kr32/cxx/abi.h`; add host ABI/API tests, driver error-path tests,
      the `--emit obj` output contract, and the `KR32_BUILD_CXX_TOOL` option.
- [x] Choose a custom C++ front end; no external front-end dependency.
- [x] C++ preprocessor, lexer, AST, recursive-descent parser, and diagnostics
      (M9.2); host parser tests including a malformed-input corpus.
- [x] Integer C++ semantic analysis, namespace/`using` lookup, overload
      resolution, Itanium-like mangling, and direct assembly lowering (M9.3);
      functions, recursion, references, arrays, globals, `extern "C"`, and
      multi-TU tests run through the existing linker/VM.
- [ ] `kr32_cxx` module: front end, lowering, code generation, weak/COMDAT
      merge.
- [x] Classes, constructors/destructors, `this`, member access/mangling, static
      members, single inheritance, and memberwise copy (M9.5).
- [x] Vtables, virtual dispatch/overrides, abstract classes/pure virtuals,
      typeinfo/`typeid`, and pointer `dynamic_cast` (M9.6).
- [x] Function templates, weak template/inline symbols, and static
      initialization (M9.8). Class templates, COMDAT groups, static
      destruction, and guarded local statics remain deferred.
- [x] `new`/`delete`, class arrays, and static construction/destruction (M9.9).
- [x] R7 frame-chain unwinder with `.cpp_except` call-site ranges, cleanup
      landing pads, `_Unwind_Resume`, and restored `SP`/`R7`.
- [x] Refcounted class exceptions with `__cxa_begin_catch`/`end_catch`,
      `__cxa_rethrow`, and `__cxa_atexit` registry (M9.9).
- [x] Scalar `float`/`double` codegen, FP calls/spills, conversions, and ABI
      alignment (M9.4); `.float`/`.double` object-assembler support ported from
      main.
- [x] Variadic functions with promoted `float`/`double` arguments and `va_list`
      (M9.4 stack slots and builtins).
- [x] Matrix library and intrinsics: `mat4`/`vec4`, object-assembler tile
      encoding, `MMUL`/`MMAC`/`MVEC`/`MTRANS`/`VDOT`, and transform-stack
      push/pop/drop (M9.7).
- [x] Host thread/futex services in `vm::ClusterHost` over `core::Cluster`
      (M9.10 stage 1).
- [x] Guest TCB at `R11`, frozen offsets, per-thread stacks/TLS, guest join,
      and FPR/TSP context save/restore (M9.10 stage 2).
- [x] Compiler `thread_local`, LL/SC atomics, thread/mutex/condvar header
      surface, guarded local statics, and initialized TLS (M9.10).
- [x] C++ debug records for sources/lines/functions/locals/globals/classes/
      members/`this`, thread snapshots, DAP/GUI listing, and class-member
      expansion (M9.11).
- [x] `kr32 cxx`, `kr32 build file.cpp`, runtime auto-link, CLI help/diagnostics,
      docs, and Windows/WSL gate (M9.12, tag `m9`).
- [x] P0 post-merge fixes: multi-TU init/fini, TLS/EH merging, blocking guards,
      class static locals, and debug snapshot/provider plumbing (`m9-p0`).
- [ ] Deferred: ClusterHost-backed GUI/DAP session, full per-thread variable
      expansion, clickable GUI thread/source switching, and the extra
      compiler/configuration matrix.
- [ ] Tests: classes, templates, virtual calls, exceptions, threads, FP, matrix,
      and standard-library subset.

**Test plan and gate**

- [x] ABI layout/constant tests and API/driver harness tests pass on
      Windows/WSL (M9.1 gate: 21/21 CTest).
- [x] Preprocessor/lexer/parser/AST tests, including malformed input, pass on
      Windows/WSL (M9.2 gate: 131 cases / 4617 assertions).
- [x] Integer C++ functions, overloads, references, arrays, globals, `extern "C"`,
      and cross-TU linking compile, link, and run (M9.3 gate: 142 cases / 4646
      assertions).
- [x] Float/double arithmetic, calls, conversions, globals, `long double` alias,
      NaN comparison, and promoted stack varargs pass (M9.4 gate: 153 cases /
      4689 assertions).
- [x] Class layout, constructors/destructors, `this`, member access, single
      inheritance, static members, FP members, and copy pass (M9.5 gate: 159
      cases / 4702 assertions).
- [x] Virtual override dispatch, `typeid`, pointer `dynamic_cast`, and
      abstract-class rejection pass (M9.6 gate: 162 cases / 4711 assertions).
- [x] Matrix multiply/add/transpose, vector transform, dot, aliasing, and
      transform-stack push/pop/drop pass (M9.7 gate: 165 cases / 4735
      assertions).
- [x] Function templates, weak template/inline symbols across TUs, and static
      initialization order pass (M9.8 gate: 169 cases / 4745 assertions).
- [x] `new`/`delete`, class arrays, static construction/destruction, and
      refcounted class exceptions pass (M9.9).
- [x] R7 frame-chain unwinding, multi-frame cleanup landing pads,
      `_Unwind_Resume`, nested/cross-function try/catch, and rethrow pass
      (M9.9 gate: 178 cases / 4802 assertions).
- [x] C++ hello world and a class/template demo run.
- [x] Host thread/futex services over `core::Cluster` pass (M9.10 stage 1:
      183 cases / 4841 assertions).
- [x] Guest TCB layout, context save/restore, and guest join pass (M9.10
      stage 2: 186 cases / 4885 assertions).
- [x] `thread_local`, LL/SC atomics, two-core counter, and header surface pass
      (M9.10 stage 3: 191 cases / 4917 assertions).
- [x] Two C++ threads with a mutex-protected counter, guarded statics, and
      initialized TLS pass (M9.10 gate: 193 cases / 4951 assertions).
- [x] C++ debug classes/members/`this`/lines and thread snapshots pass (M9.11:
      195 cases / 4972 assertions; 23/23 CTest after CLI tests).
- [x] P0 multi-TU init/fini/TLS/EH, guard race, class static local, and live
      snapshot provider pass (m9-p0: 222 cases / 5317 assertions; 29/29 CTest).
- [x] Guest threads run on multiple KR32 cores with mutex/atomic/TLS tests.
- [ ] GUI steps through a `.cpp` file with correct lines and variables.
- [ ] Windows, WSL, and ARM64 builds pass.
- [ ] Commit and tag `m9`; log entry added.

### M10 — Display device, host surface, and console

**Goal:** make the framebuffer a usable output device: the v2 register
contract, a host window that presents committed frames, a bootable text
console, and a `SCREEN` view in the debugger. The programmer-facing API is
M11.

**Status:** in progress. D1 (device v2 and keyboard) and D2 (host surface,
`kr32 display`, screen shots) landed 2026-11-12; D3–D4 remain. The design is [docs/display.md](docs/display.md); the D0 review closed
2026-11-12 and its decisions are recorded in §12 there.
[docs/devices.md](docs/devices.md) still describes the v1 contract and absorbs
the v2 register table when M10 closes.

**Dependencies**

- **CR-1 — loader zero-fills `.bss`**, below. Done 2026-11-12; D1 re-runs the
  loader tests as part of its gate.
- SDL2 (vendored) and the embedded OS images, refreshed with
  `scripts/update-os.ps1` / `scripts/update-os.sh`.
- No dependency on the C++ toolchain.

**Prerequisite, outside this milestone's source scope — CR-1: zero `.bss` on image load**

Why: [docs/container.md](docs/container.md) says a `.bss` section "reserves
zero-filled memory at link", but `src/cli/cli.cpp:200-202` and
`src/vm/threads.cpp:31-34` skip `.bss` sections, and only
`src/debug/debug.cpp:184-188` zeroes them — per byte. `Machine::reset` clears
registers but not RAM, so a reload into an existing machine inherits stale
bytes. A fresh `Machine` per CLI run masks this today; M10's static kernel
buffers, and any later `static` initialised by assumption, make it a hazard.

- [x] Add one shared image loader,
      `container::load_into(core::Machine&, const Image&, LoadedImage&,
      std::string&)` in `src/container/container.cpp`, and use it from
      `src/cli/cli.cpp`, `src/vm/threads.cpp`, and `src/debug/debug.cpp`.
- [x] Zero every allocated `.bss` range in bulk through `Machine::zero_memory`,
      not per byte; `.tls` is captured in `LoadedImage` by the same helper.
- [x] Delete the three duplicated section loops.
- [x] Tests: dirty RAM, load an image with `.bss`, assert the range reads zero;
      load the same image twice into one machine, assert zero both times.
- [x] `--flat` output is unchanged; the linker path is untouched.

Implemented and verified 2026-11-12; the loader tests are part of
`unit_test_container` and run in the normal gate. CR-1 does not reopen M3, which
closed 2026-11-02.

**Tasks**

**D1 — Framebuffer v2 and keyboard device** — complete 2026-11-12.

- [x] Implement the v2 register contract ([docs/display.md](docs/display.md)
      §5): `WIDTH`/`HEIGHT`/`STRIDE`/`FORMAT`/`FRONT`/`BACK`, computed
      `MODE_OK`, sticky `FLIP_ERROR`, `ID` word.
- [x] `FLIP` commits the surface that was `BACK` before the call, swaps the
      offsets, and increments `FRAME` only on success.
- [x] Keyboard device: 64-event FIFO, packed event word, IRQ, overrun W1C.
- [x] Pointer page constants only (§8.5); no device and no host injection.
- [x] Unit tests for every §11.1 rule, including byte/half/word/unaligned
      accesses across a 4 KiB boundary, a fault at `base + 32 MiB`, and success
      at `base + 32 MiB - 4`.
- [x] Integration test: an assembly guest sets a mode, fills VRAM, flips, and
      reads back the committed descriptor and pixel bytes. The guest runs with
      paging enabled because the MMU refuses device addresses when it is off.
- [x] Verified CR-1: the loader tests pass and a `.bss` range reads zero after a
      reload.

**D2 — Host surface and `kr32 display`** — complete 2026-11-12.

- [x] `platform::Window::present_surface(vram, width, height, stride, scale)`
      with integer zoom, sharing the window and event loop with the Canvas
      path.
- [x] `kr32 display [image.k32]` and `kr32 display --os`: step in slices,
      present when `FRAME` changes, `Esc` quits, `F2` cycles zoom, and a halted
      guest keeps the last frame.
- [x] `--screen-shot X.ppm` writes only the committed frame.
- [x] Key events translated to packed event words; the scancode table lives in
      `src/platform`. Key-up events are delivered too.
- [x] A color-bar guest test image (`examples/color-bars.asm`). Standalone
      images get a flat identity map from the display command, because a
      plainly loaded executable cannot reach MMIO without page tables.

**D3 — Framebuffer console**

**Status:** implemented 2026-11-12 on the serial path; keyboard input is wired
through `console_getc` but has not yet been driven end-to-end in a window.

- [x] 8x16 font generator under `tools/`; the generated table is committed to
      `kr32os/kernel/font8x16.c` with OFL attribution.
- [x] `kr32os/kernel/fb.{h,c}`: probe by 32-bit word, mode set, fill, present,
      back surface.
- [x] Console: static 2-byte cells (character plus a 4-bit foreground and
      background index) and a 256x100 line ring in `console.c`, occupancy
      counter, dirty rectangles, cursor, scrolling, and `\f` clear.
- [x] 16-entry XRGB palette; `con_set_colors` sets the pen indices and each cell
      captures them at write time. ANSI SGR maps onto the same indices later.
- [x] Boot: probe `0xFFF0_4028`, set 800x600, print the banner, fall back to
      serial and note it once on failure.
- [x] Keyboard input through `console_getc`; `Ctrl-C` waits for the user-program
      process model in M11.
- [x] Serial mirror stays unconditional so logs and shell tests are unchanged.

Known cost, before D4 closes: a full-screen redraw is 473,600 pixel stores and
the kernel front end emits about 14 instructions per store, so boot takes roughly
7M steps and one scroll costs about as much. The OS test budgets were raised to
12M steps (24M for the fault test) because of this. Optimise the fill/redraw or
the front end's store codegen before the milestone gate.

Follow-up: drive `ls` and Enter through the keyboard FIFO in a windowed test.

**D4 — Debugger integration** — implemented 2026-11-12.

`Layout` gained `tabs` and `screen` (the source pane starts below a 26-pixel
strip); `draw_tabs` renders `SOURCE | SCREEN`; `draw_screen` caches the
committed frame keyed on `frame().number`, blits integer-scaled or
nearest-neighbour fit, letterboxes, and draws the focus dot; `hover` maps a
pixel to its guest address and colour; `click` selects a tab, focuses the
screen, and points the memory panel at the clicked pixel; `Esc` releases focus;
`app.cpp` routes key events to `devices_.keyboard()` while focused except for
Escape and F5/F6/F9-F11. OS mode selects the tab by default, and the new
`ui_headless_os` test renders it.

- [x] `SOURCE | SCREEN` tab strip over the top-left region; integer scale or
      fit with nearest-neighbour, centered and letterboxed.
- [x] Focus: click to give the guest keyboard focus, green dot, `Esc` releases
      focus before it quits, and debugger keys always win.
- [x] Hover a pixel: screen `(x, y)`, guest address, hex and RGB; clicking
      points the memory panel at that address.
- [x] `--screen-shot` includes the tab when active; `ui_headless_os` covers the
      screen.
- Aperture fast path: conditional on profiling, recorded if taken, and never
  part of this milestone's exit gate.

The source-pane test in `tests/unit/test_ui.cpp` moved its two click rows down
by the 26-pixel strip; its assertions are unchanged.

**Before the milestone closes:** the console redraw cost recorded under D3. A
7M-step boot means the debugger's SCREEN view sits at "no committed frame" until
the guest runs for ~7M steps, and each scroll costs about the same. Fix the
fill/redraw — a tighter blit (the kernel front end's ~14 instructions per store
is the problem) or a smaller console mode — before tagging `m10`.

**Test plan and gate**

- [x] `ctest --preset windows-msvc-debug` passes (83/83).
- [x] WSL gate: `cmake --preset linux-gcc`, build, and `ctest` pass **83/83**
      with the UI, once `ninja`, `pkg-config`, and `libsdl2-dev` were installed.
- [x] Golden-frame hashes are checked in and updated deliberately
      (`display_screen_shot`).
- [x] Console coverage: `os_console_screen` boots the embedded OS, requires an
      800x600 committed frame, asserts two runs are byte-identical, and requires
      both the foreground and background palette colours to appear.
- [ ] Cell assertions for a mid-line colour change and colours across a scroll
      are **deferred**: the kernel console has no host-side harness, and adding
      one means refactoring `console.c` behind a testable surface. The
      screenshot colour assertion covers steady-state rendering.
- [x] `scripts/update-os.ps1` run for Windows and `scripts/update-os.sh` in WSL
      for Linux; both image pairs are refreshed.
- [x] Docs updated: `devices.md` carries the v2 register table and keyboard,
      `display.md` points at it, `ui.md` gains the `SCREEN` tab, `testing.md`
      gains the new tests.
- [ ] Tag `m10` and push.
- Deferred: the console redraw optimisation. Boot costs ~7M steps and a scroll
  about the same; the test budgets absorb it and the milestone is not gated on
  it. Recorded under D3 for a later performance pass.

**C++ `unsigned char`, fixed 2026-11-13.** `BuiltinKind` now has `UnsignedChar`;
the parser maps `unsigned char` (in either order) to it; `uint8_t` aliases it;
size, type name, and mangling (`h`) are covered; `is_unsigned_integer` and
`promote` treat it as unsigned; byte loads emit `LOAD8U` through
`byte_load_mnemonic`, and stores emit `STORE8`; casts mask to 255 (and signed
char casts sign-extend). `test_cxx_codegen_1` covers 200 > 100, 200 < 128,
compound assignment overflow, `++`, parameter passing, arrays, `(unsigned char)-1`,
`sizeof`, and globals. The unit-bucket watchdog moved from 300 s to 600 s because
`test_cxx_codegen_2` alone runs about 214 s and crossed 300 s under parallel
load.

**`signed char` mangles as `c`, deliberately (2026-11-13).** The parser ignores
`signed`, so `char` and `signed char` are one type in this compiler and share
the mangling `c`; the Itanium ABI uses `a` for `signed char` because the
standard makes them distinct types. Keeping one type is consistent with the
type model and costs only standard type identity: overloading `char` against
`signed char` is impossible. A distinct `SignedChar` kind would repeat the
`UnsignedChar` change across the same sites; revisit only if a program needs
the distinction.

**Comparison codegen, fixed 2026-11-13.** The `PUSH R0` / `MOV R1, R0` /
`POP R0` sequence did not come from call arguments (those already evaluate
right-to-left through the simple-argument path). It came from a comparison whose
right operand was a constant that was not a single token, such as `x < (8)`:
the literal fast path only matched a bare `Number`/`Character`. The comparison
path now folds a constant operand with `try_fold_constant` and emits
`MOV R1, #imm; CMP R0, R1` without touching the stack.
`unit_test_cc_8` covers `if (x < (8))` and `return x < (8)`, requiring no
`PUSH R0` and a `MOV R1, #8`.

### M10.5 — Host terminal and renderer

**Goal:** replace the kernel framebuffer console with a host-implemented terminal
device, and give programs host-executed drawing through a graphics command
device. Specification: [docs/terminal.md](docs/terminal.md).

**Status:** complete 2026-11-13; tagged `m10.5`. The host terminal device, the
graphics command device, the kernel console driver, and the font atlas are in.
Both gates pass (Windows and WSL, 86/86 each). Decisions taken: 10x20 cells
(80x30 grid), one host renderer shared by the terminal and the graphics device,
the terminal as a host-side client that draws last with an explicit hide/show,
immediate-register graphics commands first (a FIFO only if measured), no escape
sequences in v1, and scrollback kept internal.

**Why:** the guest cannot move framebuffer data at interactive speed. Measured:
a scroll is 473,600 pixels, at least ~1.9M ticks even at four instructions per
pixel; a 41-line print took 10M ticks after the panning prototype, and 45M for
85 lines. The host runs about 4.5G ticks/s.

**Tasks**

- [x] Host `Renderer`: surface, clip, palette, fill, copy, line, text blit;
      deterministic integer rasterisation. The primitives live in
      `GraphicsDevice`; the terminal is the text/palette client. No separate
      `Renderer` class was needed with two clients.
- [x] Terminal device (page 8 at `0xFFF0_8000`): `DATA`, `STATUS`, `CONTROL`,
      `COMMAND`, `COLOR`, `ID`; 10x20 font table; 80x30 grid; 256-line
      scrollback ring.
- [x] Graphics command device (page 9): `SET_CLIP`, `SET_COLOR`, `FILL_RECT`,
      `COPY_RECT`, `DRAW_LINE`, `DRAW_TEXT`, `FLIP`; immediate registers.
- [x] Kernel `console.c` reduced to a device driver; `font8x16.c` and the grids
      deleted; both build scripts updated.
- [x] Embedded images regenerated; boot screen checked at 80x30.
- [x] Documentation: `devices.md` pages 8 and 9, `display.md` §7 rewritten and
      §11 pointers, `graphics.md` host-execution model, programmer's reference
      MMIO map.

**Test plan and gate**

- [x] Host unit tests for the terminal (putc, wrap, scroll, clear, control
      characters, colours, hide/show, geometry, no-framebuffer) and per
      graphics primitive (fill, copy, line, text, clip, validation):
      `tests/unit/test_terminal.cpp`, `tests/unit/test_graphics.cpp`.
- [x] `os_console_screen` and `run_os_*` stay green; the screenshot is 800x600.
- [x] Windows and WSL gates pass: 86/86 each.

### M11 — Graphics and console API

**Goal:** the programmer-facing API in [docs/graphics.md](docs/graphics.md):
`gfx_*` drawing, `con_*` console, `input_*`, with guest headers.

**Status:** complete 2026-11-13; tagged `m11`. The five guest headers, the
syscall path, the VRAM arena, the user runtime, and two pixel-tested examples
are in; both gates pass 89/89. Decisions taken 2026-11-13: user programs reach the devices
through a syscall command block (`sys_gfx`, 60); surfaces are VRAM-backed
handles allocated by the kernel; text is copied into a VRAM staging buffer
before `DRAW_TEXT`; the five guest headers land in both front ends at once; an
unknown `#include` warns and is dropped; M11 covers `fb`, `gfx`, `con`, and
`input`.

**Tasks**

- [x] Guest headers `<kr32/fb.h>`, `<kr32/display.h>`, `<kr32/gfx.h>`,
      `<kr32/con.h>`, `<kr32/input.h>` in `src/cc/cc.cpp` and
      `src/cxx/preprocessor.cpp`, from the shared list in
      [docs/graphics.md](docs/graphics.md) §8.
- [x] Warn on an unknown `#include` instead of dropping it silently (C; the C++
      front end already errors, which is stricter and kept).
- [x] `sys_gfx` (70): copy and validate the command block, write page 9.
- [x] VRAM arena and `gfx_create`/`gfx_destroy`; the kernel stages the text
      inside `DRAW_TEXT`.
- [x] Kernel `gfx_*` command wrappers, `con_*` over `sys_write`, `input_*`
      over the keyboard device with syscalls 71-74.
- [x] Two examples: `kr32os/user/gfxdemo.c` and `kr32os/user/gfxcheck.c` run
      under the shell; `os_gfx_demo` and `os_gfx_check` check their pixels.
- [x] `gfx_blit` ships as one strided copy; keyed and masked blits, clipped
      text, `con_readline` for user programs, and `input_wait` are deferred
      and marked so in [docs/graphics.md](docs/graphics.md).

**Test plan and gate**

- [x] Compiler tests include each header and call a symbol in C and C++.
- [x] The examples run under KR32OS and both pixel tests pass.
- [x] Windows and WSL gates pass: 89/89 each.

## 6. Cross-cutting test plan

### 6.1 Test layers

| Layer | Tool | What it covers |
| --- | --- | --- |
| Unit | doctest | ISA semantics, assembler, linker, container, MMU, debug info, UI layout |
| Golden | checked-in vectors | Encodings, containers, images, framebuffer screenshots |
| Integration | CTest | Build → link → run programs; sandbox; debugger sessions |
| CLI | CTest | Exit codes, help, diagnostics, round-trips |
| UI | offscreen canvas | Panel layout, painting, hit testing, golden frames |
| DAP | protocol tests | Launch, breakpoints, stepping, memory, evaluate |
| Cross-platform | CI | Windows MSVC, Ubuntu GCC/Clang, ARM64 |
| Sanitizers | ASan + UBSan | Memory safety and undefined behaviour |
| Determinism | repeat runs | Same image and inputs produce identical state |

### 6.2 Commands

Windows (Developer PowerShell or full paths):

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset windows-msvc
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset windows-msvc
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --preset windows-msvc
```

WSL:

```bash
cd /mnt/c/Users/kevin/source/repos/KR32
cmake --preset linux-gcc
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

Sanitizers:

```bash
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan
```

Headless UI smoke:

```bash
SDL_VIDEODRIVER=dummy ./build/linux-gcc/kr32 --smoke-ui
```

### 6.3 Definition of done

A milestone is done only when:

- every task in the milestone is checked;
- the test gate passes on Windows and WSL (and ARM64 when available);
- new behaviour has unit or integration coverage;
- the relevant documents are updated;
- the change is committed;
- the milestone tag is created;
- the running log has an entry.

No milestone advances with known failing tests. Flaky tests block until fixed
or explicitly quarantined with an issue and a date.

### 6.4 Golden and fixture policy

- Golden vectors are generated by an independent encoder or written by hand,
  not by the code under test.
- Small container/image fixtures may be committed; list them in the test plan.
- Large or generated artifacts belong in `build/` or `dist/`, not Git.
- UI golden frames are small PNGs and are committed only when stable.

## 7. C++ readiness checklist

These are design constraints for the C-first milestones. Review at M3 and M5.

- [ ] Container supports weak/COMDAT flags, section merging, and additional
      relocation types.
- [ ] Assembler supports `.weak`, `.section`, `.type`, `.size`, `.loc`, `.file`,
      alignment, and symbol visibility.
- [ ] Linker resolves weak symbols, merges sections, and can grow a COMDAT path.
- [ ] One ABI document covers C and C++: ILP32, register roles, aggregates,
      varargs, callee-saves, exceptions, static initialization.
- [ ] Debug info records types, frames, variables, globals, namespaces, and
      mangled names.
- [ ] `DebugSession` exposes typed values, objects, and expression evaluation,
      not only raw words.
- [ ] Runtime plan includes libc subset, `new`/`delete`, exceptions, RTTI, and
      static initialization.
- [ ] Build pipeline can take `.cpp` → assembly/object → link once `kr32_cxx`
      exists.
- [ ] No C-only assumption is baked into the object format, ABI, or debug
      records.

## 8. Risks and open items

| Risk | Impact | Mitigation |
| --- | --- | --- |
| SDL2 and `pkg-config` absent in WSL | UI cannot configure there | Install `libsdl2-dev` and `pkg-config` before M6 |
| CMake/Ninja not on `PATH` | Windows commands fail in plain shells | Use VS paths or a helper script |
| WSL builds over `/mnt/c` are slow | Slow iteration | Clone to `~/src/KR32` for heavy builds |
| KR16 UI is SDL3, KR32 is SDL2 | Porting work | Keep `src/ui` portable; port only the backend |
| C++ front end choice and license | M9 schedule | Decide before M9; EDG is one option |
| ARM64 CI availability | Cross-platform gate | Use native ARM64 machine or a runner |
| ISA not yet frozen | Encoding churn | Finish golden-vector review; freeze v1.0 |
| JSON/DAP library choice | DAP scope | Vendor a small MIT library and record it |
| C compiler scope creep | M4 schedule | Freeze the C subset at the start of M4 |

## 9. Running log

Newest entries at the bottom. Use the local time zone.

- **2026-11-02 00:33 -08:00** — `implementation.md` created; environment,
  dependencies, milestone map, test plan, and C++ readiness checklist recorded.
- **2026-11-02 00:33 -08:00** — WSL toolchain checked: Ubuntu 26.04 LTS, GCC
  15.2.0, Clang 21.1.8, CMake 4.2.3, Ninja 1.13.2; `pkg-config` and SDL2 are
  not installed yet.
- **2026-11-02 00:33 -08:00** — Git identity verified:
  `kevinwross <kevinro@live.com>`.
- **2026-11-02 00:35 -08:00** — Local Git repository initialised on `main`;
  initial commit `6062fdb` contains the planning docs, ISA draft,
  `implementation.md`, `.gitignore`, and `.gitattributes`.
- **2026-11-02 00:35 -08:00** — M0 started. The repository, checklists, and
  dependency notes are in place; CMake scaffold is next.
- **2026-11-02 00:35 -08:00** — Repository checkpoint `5d601cd` records the
  checklist environment notes. WSL full-access note added.
- **2026-11-02 00:43 -08:00** — Windows dependency check: CMake 4.3.1, Ninja
  1.13.2, MSVC 19.51.36260, Python 3.14.6, Git 2.55.0, and a C++20 concepts
  smoke compile/run pass. SDL2 development files are not installed.
- **2026-11-02 00:43 -08:00** — WSL dependency check: Ubuntu 26.04, SDL2
  2.32.10, `pkg-config` 2.5.1, GCC 15.2.0, Clang 21.1.8; C++20 and an SDL2 link
  test pass.
- **2026-11-02 00:43 -08:00** — Windows SDL2 is the only open dependency before
  `KR32_BUILD_UI=ON`; the headless M0 build can proceed without it.
- **2026-11-02 00:46 -08:00** — SDL2 2.32.10 installed at `third_party/SDL2`
  via `scripts/install-sdl2.ps1`; the directory is gitignored, `SDL2_DIR` is
  set, and a minimal CMake + SDL2 link/run test prints `SDL 2.32.10`.
- **2026-11-02 00:46 -08:00** — Windows dependencies are complete; the headless
  and UI builds can both proceed.
- **2026-11-02 00:54 -08:00** — M0 scaffold implemented: CMake project and
  presets, module targets, CLI dispatcher, platform boundary, vendored doctest,
  golden-vector harness, CI workflow, install/package placeholder, README.
- **2026-11-02 00:54 -08:00** — M0 local gate passed: Windows MSVC Debug/Release
  and Ninja Debug/Release; WSL GCC/Clang/ASan; UI ON and OFF; 7 CTest tests;
  boundary negative test; `kr32 version`, `help`, and `--smoke-ui`.
- **2026-11-02 00:54 -08:00** — CI workflow is committed; its first remote run is
  pending because the repository has no remote yet.
- **2026-11-02 00:54 -08:00** — Tagged `m0`.
- **2026-11-02 01:08 -08:00** — M1 implemented: integer machine with RAM/ROM/guard,
      flags, faults, branches, stack, loads/stores, shifts, multiply/divide;
      two-pass assembler with labels and directives; disassembler with symbol
      listings; `as`, `dis`, `vm`, and `run`; `examples/demo.asm`.
- **2026-11-02 01:08 -08:00** — M1 gate passed: 31 unit cases and 3166
      assertions, 100k-word decode property test, assembler/disassembler
      round-trip, demo run, determinism; Windows MSVC Debug/Release and Ninja;
      WSL GCC/Clang and ASan/UBSan.
- **2026-11-02 01:08 -08:00** — Tagged `m1`.
- **2026-11-02 01:19 -08:00** — M2 implemented: control registers, two-level
      paging with permissions and A/D, user/kernel mode, exception delivery and
      `IRET`, `#PF`/`#GP`/`#ALIGN`/`#DF`, interrupts, `LL`/`SC`/`FENCE`,
      `COREID`, a cluster IPB hook, `--bios`, and a BIOS-to-user boot test.
- **2026-11-02 01:19 -08:00** — M2 gate passed: 49 unit cases / 3436 assertions,
      BIOS smoke test, and 9 CTest cases on Windows; WSL GCC/Clang/ASan pass.
- **2026-11-02 01:19 -08:00** — Tagged `m2`.
- **2026-11-02 01:27 -08:00** — M2b implemented: 16 FPRs, FP escape arithmetic,
      compares, conversions and moves; canonical NaN, denormal, saturating
      conversion, `FLOAD`/`FSTORE`; 16 4x4 tiles, `MMUL`/`MMAC`/`MVEC`/`MTRANS`/
      `VDOT`, tile loads/stores, transform stack; assembler/disassembler and
      `fp-demo`/`matrix-demo`.
- **2026-11-02 01:27 -08:00** — M2b gate passed: 64 unit cases / 3887 assertions,
      11 CTest cases including both FP/matrix demos; Windows MSVC Debug/Release
      and Ninja; WSL GCC/Clang/ASan.
- **2026-11-02 01:27 -08:00** — Tagged `m2b`.
- **2026-11-02 01:37 -08:00** — M3 implemented: `.k32` container with CRC,
      sections, symbols, relocations, and debug sections; object assembler;
      linker with section merging, symbol resolution, map, strip, and runtime
      symbols; `as --object`, `ld`, `size`, `build`, and `.k32` VM loading.
- **2026-11-02 01:37 -08:00** — M3 gate passed: 74 unit cases / 4033 assertions,
      13 CTest cases including `build_hello` and two-object `link_smoke`;
      Windows MSVC Debug/Release and Ninja; WSL GCC/Clang/ASan.
- **2026-11-02 01:37 -08:00** — Tagged `m3`.
- **2026-11-02 01:48 -08:00** — M4 bootstrap in progress: C lexer/parser/codegen
      for integers, chars, pointers, arrays, functions, control flow, strings,
      and recursion; `_start`/`putchar`/`exit` runtime; SYSCALL host callback;
      `kr32 cc` and `kr32 build file.c`; `examples/hello.c`.
- **2026-11-02 01:55 -08:00** — M4 completed: structs/unions/enums/typedefs,
      `_Bool`/casts, `switch` and `goto`, object/function macros, built-in
      headers, conditionals, `memcpy`/`memset`/`strlen`, bump `malloc`, and the
      shared ABI note in `docs/abi.md`.
- **2026-11-02 01:55 -08:00** — M4 gate passed: 88 unit cases / 4157 assertions,
      14 CTest cases including `build_c_hello`; Windows MSVC Debug/Release and
      Ninja; WSL GCC/Clang/ASan.
- **2026-11-02 01:55 -08:00** — Tagged `m4`.
- **2026-11-02 02:02 -08:00** — M5 completed: `.debug_info` line/source/function/
      variable/global/type records; `DebugSession` load, breakpoints, stepping,
      call stack, variable/memory access, and console capture; `kr32 debug`
      dump; debug and stepping tests; format/API note in `docs/debug.md`.
- **2026-11-02 02:02 -08:00** — M5 gate passed: 92 unit cases / 4222 assertions,
      15 CTest cases including `debug_dump`; Windows MSVC Debug, Release, Ninja;
      WSL GCC/Clang/ASan.
- **2026-11-02 02:02 -08:00** — Tagged `m5`.
- **2026-11-02 02:12 -08:00** — M6 completed: portable software canvas and 5x7
      font, SDL2 window/texture backend, source/disassembly/register/variable/
      watch/memory/call-stack/console panels, FPR/tile/MMU/transform/core
      machine tab, breakpoints, click/keyboard stepping, C-source Build, error
      dialog, and headless PPM/hash screenshots.
- **2026-11-02 02:12 -08:00** — M6 gate passed: 94 unit cases / 4229 assertions,
      16 CTest cases including `ui_smoke`, `ui_headless`, and `debug_dump`;
      Windows MSVC Debug; WSL GCC/Clang; `KR32_BUILD_UI=OFF` build/CTest.
- **2026-11-02 02:12 -08:00** — Tagged `m6`.
- **2026-11-02 02:26 -08:00** — M7 completed: hosted `SYSCALL` console/file/
      time/random/args/exit ABI, sandbox root/read-only/quota/path checks,
      file and stdio runtime wrappers, `kr32 shell` with history and piped
      scripts, and a DAP server over stdio and loopback TCP sharing
      `DebugSession`. Added `docs/host.md`, `examples/fileio.c`, host/DAP/shell
      smoke tests.
- **2026-11-02 02:26 -08:00** — M7 gate passed: 97 unit cases / 4267 assertions,
      19 CTest cases including `host_smoke`, `dap_smoke`, and `shell_smoke`;
      Windows MSVC Debug/Release/Ninja; WSL GCC/Clang/ASan. TCP DAP was also
      smoke-tested with a local client.
- **2026-11-02 02:26 -08:00** — Tagged `m7`.
- **2026-11-02 02:35 -08:00** — M8 completed: MMIO timer/block/console devices,
      IRQ route table, tick-driven timing, block DMA, shared-RAM cooperative
      cluster, round-robin scheduler, IPIs, reservation invalidation, and
      deterministic LL/SC spinlock runs. Added `docs/devices.md` and device/
      cluster tests.
- **2026-11-02 02:35 -08:00** — M8 gate passed: 102 unit cases / 4320 assertions,
      19 CTest cases; Windows MSVC Debug/Release/Ninja; WSL GCC/Clang/ASan.
- **2026-11-02 02:35 -08:00** — Tagged `m8`.
- **2026-11-02 07:03 -08:00** — Post-M8 UI polish: replaced the built-in 5x7
      bitmap font with embedded Inter and JetBrains Mono TTF rendering via
      stb_truetype; restored lowercase C source and mono line spacing; added
      separate source-line Over/In/Out and disassembly instruction-step
      Over/In/Out controls. Added `DebugSession` line stepping and tests.
      Verification: 104 unit cases / 4346 assertions, 19 CTest on Windows and
      WSL UI builds, 17 CTest headless UI-off/ASan.
- **2026-11-02 07:10 -08:00** — Made the debugger window genuinely resizable:
      SDL resize events create a new software canvas and texture at the client
      size; the layout now computes pane widths/heights responsively instead of
      stretching the original 1024x640 surface. Verification: 104 unit cases /
      4346 assertions, 19 CTest Windows Debug/Release/Ninja and WSL GCC/Clang,
      17 headless UI-off/ASan; a 1280x900 live-window capture confirmed the
      non-stretched layout.
- **2026-11-02 07:16 -08:00** — Added KR16-style gutters: breakpoint squares,
      right-aligned line numbers/addresses, gutter hover, left-click breakpoint
      toggling, and a right-click context menu with Toggle breakpoint and Run to
      line/address. Run-to uses a temporary breakpoint and preserves existing
      user breakpoints. Verified live against `examples/hello.c` (line 5
      breakpoint and Run to line 8).
- **2026-11-02 08:05 -08:00** — Gave the disassembly pane the same two-column
      gutter treatment as the source pane: a dedicated breakpoint column plus a
      right-aligned address column with separators, gutter hover, left-click
      breakpoint toggling, and the same right-click Toggle/Run-to menu. Verified
      live with a disassembly gutter right-click and Run to address menu.
- **2026-11-02 08:10 -08:00** — Widened the disassembly address gutter and added
      padding between the breakpoint column and the address column so it mirrors
      the source gutter geometry instead of addresses touching the breakpoint
      separator. Screenshots and docs updated.
- **2026-11-02 08:15 -08:00** — Added KR16-style disassembly hover help:
      mnemonic hover highlights the mnemonic and shows an instruction
      description; operand hover highlights the operand and describes every
      operand's addressing mode. Added `help.cpp` and help unit tests; verified
      live with PUSH and R7 hover tooltips.
- **2026-11-02 08:30 -08:00** — Added the raw instruction-encoding column:
      the disassembly now shows a labelled HEX word column (and extension word
      when present) between the address gutter and mnemonic, with a responsive
      1024/1440 layout. Changed the initial window and headless surface to
      1440x900 so source, HEX, and mnemonic columns have room; narrow layouts
      switch the source pane to the 13 px mono role.
- **2026-11-02 08:39 -08:00** — Grouped all register state into the REGISTERS
      panel: GPRs, PC/SP/FLAGS, F0-F15, TSP/T0, PTBR/PTE, and control registers.
      Replaced the bottom MACHINE tab with VARIABLES, which now hosts locals,
      globals/watch values, and the call stack. Updated headless output,
      screenshots, and docs.
- **2026-11-02 08:49 -08:00** — Reworked the debugger around the KR16-style
      always-visible layout: a CORES panel with one expandable row per core,
      plus four persistent bottom panels for MEMORY (four address/hex/ASCII
      lines), VARIABLES, CALL STACK, and CONSOLE. Added core-row
      select/collapse/expand hit testing and a `Session::core_count()`/`core()`
      seam for future multicore sessions. Updated headless rendering to one
      1440x900 frame and refreshed docs/screenshots.
- **2026-11-02 09:02 -08:00** — Docked the four-line MEMORY dump beneath the
      source pane, leaving VARIABLES, CALL STACK, and CONSOLE as the wider
      always-visible bottom row. Added an editable memory start address with
      Enter/Escape handling, a PTR-to-SP button, and ^/v 16-byte scrolling
      buttons. Added Enter/Backspace platform key mappings and refreshed the
      layout screenshot/docs.
- **2026-11-02 09:18 -08:00** — Drafted [docs/cpp-plan.md](docs/cpp-plan.md)
      for M9: custom C++ subset front end, KR32 C++ ABI, table-driven
      exceptions, guest threads on KR32 cores with TLS/atomics/mutex/condvar,
      standalone driver, and phase gates. Added `include/kr32/cxx/cxx.h`, the
      `kr32-cxx` driver skeleton, and the `cxx_tool_help` CTest; the compiler
      itself remains a diagnostic stub pending plan review.
- **2026-11-02 09:27 -08:00** — Reviewed the M9 choices: custom C++11 subset,
      one thread per core with cooperative oversubscription, `R11` as TP,
      library matrix types plus intrinsics, `double` align 8/16-byte call
      boundaries, and explicit per-thread transform stacks. Added `float`/
      `double`, FP ABI, `mat4`/`vec4` intrinsics, transform-stack rules, and
      FP/matrix test gates to the plan and checklist. Remaining decisions are
      listed in `docs/cpp-plan.md` section 15.
- **2026-11-02 09:30 -08:00** — Completed the M9 review decisions: full
      destructor unwinding, scalar FP library only, FP varargs with stack-passed
      promoted doubles, Itanium-like mangling, class-member/`this` debug gate,
      and a separate `m9-cxx` worktree. Updated the plan and checklist; only the
      section 4.1 language surface and stack-varargs ABI detail remain for final
      approval.
- **2026-11-02 09:34 -08:00** — Approved the final M9 language surface and
      stack-varargs ABI. Created [cpp-impl.md](cpp-impl.md) with M9.0-M9.12
      phases, gates, test matrix, and the current skeleton baseline. No
      worktree or compiler implementation started; tracker review is the next
      gate.
- **2026-11-02 09:35 -08:00** — Tracker approved; created the `m9-cxx`
      worktree/branch and assigned phase M9.1 (ABI, API, test harness) to the
      first subagent. M9.0 is complete; M9.1 is in progress.
- **2026-11-02 09:44 -08:00** — Completed M9.1. Froze C++ ABI revision 1 in
      [docs/abi.md](docs/abi.md) (FP layout/registers, varargs, `this` and
      mangling, vtable/RTTI, exception cleanup, `R11` TP/TCB/TLS, tiles and
      transform stack, weak/COMDAT, static initialization) with matching
      constants in `include/kr32/cxx/abi.h`. Added host ABI/API tests under
      `tests/cxx`, driver error-path tests, the `--emit obj` contract, and the
      `KR32_BUILD_CXX_TOOL` option/install rules. Windows MSVC Debug and WSL GCC
      headless both pass 21/21 CTest tests; `kr32_tests` reports 115 test cases
      and 4473 assertions. M9.2 (front end) is next.
- **2026-11-02 09:55 -08:00** — UI interaction pass: made the memory address
      field/label reliably clickable (including keypad input), added register
      cell editing for GPR/PC/SP, clickable Z/N/C/V/I flag boxes, variable
      type/address/value hover tooltips, and wheel/scrollbar scrolling across
      source, disassembly, cores, variables, call stack, and console. Updated
      screenshots and UI docs.
- **2026-11-02 10:35 -08:00** — Cores/FPR refinement: separated PC/FLAGS
      vertical spacing, increased register row spacing, switched F0-F15 to one
      full 64-bit decimal/scientific value per line, added FPR hover details and
      a decimal/scientific FPR edit popup, and refreshed screenshots/docs.
- **2026-11-02 10:37 -08:00** — Added scrollbar thumb dragging and mouse-up
      handling; improved memory/register edit feedback. Re-verified UI after the
      FPR layout/editor changes.
- **2026-11-02 10:43 -08:00** — Restored the FPR panel to two columns of eight,
      widened the Cores panel, and added a DOUBLE/FLOAT interpretation toggle.
      FPR editing and hover now follow the active view; the default is double.
- **2026-11-02 10:50 -08:00** — Accepted all M9.1 ABI review recommendations
      (`this` in R0, void ctor/dtor returns, uniform 8-byte varargs, 8/16-byte
      aggregate rule, address-point vtable/RTTI, `.cpp_except` exceptions,
      direction-only TCB offsets deferred to M9.10, EBO disabled, `long double`
      alias, bare-pointer `va_list`, `KR32_BUILD_CXX_TOOL` default ON). No ABI
      contract change; [docs/abi.md](docs/abi.md) now records the approved review
      decisions and [cpp-impl.md](cpp-impl.md) remains M9.1 done. M9.2 is next.
- **2026-11-02 10:59 -08:00** — Added `examples/types.c`: scalar `_Bool`,
      `char`, signed/unsigned char, `int`, and signed/unsigned int variables
      with per-type square functions, console output, and a `build_c_types`
      CTest. The example builds/runs under the M4 C compiler and can be loaded
      into the GUI.
- **2026-11-02 11:09 -08:00** — Added `float`/`double` to the M4 C compiler:
      literals and exponents, FPR locals/globals, arithmetic/comparisons/casts,
      mixed conversions, FP function arguments/returns, FP conditions, and
      globals. Added `.float`/`.double` data directives to both assemblers, FP
      opcode support to the object assembler, FP debug type/variable records,
      `examples/floats.c`, and FP unit/integration tests.
- **2026-11-02 11:10 -08:00** — Completed M9.2. Added the C++ preprocessor,
      lexer, AST, and recursive-descent parser under `src/cxx`, wired
      `kr32::cxx::compile` through preprocess → lex → parse, and added host
      parser tests including a malformed-input corpus. Windows MSVC Debug and WSL
      GCC headless both pass 21/21 CTest tests; `kr32_tests` reports 131 test
      cases and 4617 assertions. M9.3 (semantic analysis, mangling, integer
      code generation) is next.
- **2026-11-02 11:16 -08:00** — Added C variadic functions and FP varargs:
      fixed args use the normal register ABI, variadic args use uniform
      8-byte stack slots (float promotes to double; char/bool promote to int),
      and `stdarg.h` supplies `va_list`, `va_start`, `va_arg`, `va_copy`, and
      `va_end`. Added `examples/varargs.c`, CTest coverage, and ABI docs.
- **2026-11-02 11:25 -08:00** — Completed M9.3. Added symbol collection,
      namespace scopes, `using`/`using namespace`, overload resolution,
      Itanium-like mangling, and direct assembly lowering for functions, calls,
      locals, globals, arrays, pointers, references, integer operations, and
      control flow. `compile()` now returns assembly for the supported subset.
      Added codegen tests for recursion, namespaces/overloads, references,
      arrays/loops, globals, `extern "C"`, multi-TU linking, and unsupported
      class diagnostics. Windows/WSL both pass 21/21 CTest tests; `kr32_tests`
      reports 142 test cases and 4646 assertions. M9.4 (scalar `float`/`double`)
      is next; the main-branch `.float`/`.double` data directives and FP
      instruction support are available for that phase.
- **2026-11-02 11:32 -08:00** — Completed M9.4. Ported the main-branch
      object-assembler FP support (`.float`/`.double`, `FLOAD`/`FSTORE`, FP
      arithmetic/compare, `FCVT`) and added scalar `float`/`double` codegen:
      literals, mixed int/FP arithmetic and conversions, FP arguments in
      `F0-F7`, results in `F0`, frame spills, FP globals, references to
      `double`, `long double` as a `double` alias, NaN comparisons, and
      stack-passed variadic arguments with promoted doubles plus `va_list`
      builtins. Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports
      153 test cases and 4689 assertions. M9.5 (classes,
      constructors/destructors, single inheritance) is next.
- **2026-11-02 11:46 -08:00** — Built the freestanding C libc layer in KR32 C:
      `sizeof`, `_Alignof`, `offsetof`, `_Static_assert`, and `stddef.h`; a new
      linker `__heap_end`; a boundary-tag best-fit allocator with address-ordered
      free lists and bidirectional coalescing (`malloc`, `free`, `calloc`,
      `realloc`); `printf` with integer/string/pointer and FP (`f/e/g`) formats;
      FP math builtins; LL/SC/FENCE; BSWAP/CLZ/CTZ/POPCNT and overflow builtins;
      ISA/object-assembler/disassembler support for `0x62`; and tests/docs.
- **2026-11-02 12:11 -08:00** — Fixed the GUI LOAD button: it now opens the
      platform file picker (Windows `GetOpenFileName`, Linux zenity/kdialog)
      and loads the selected `.k32` or `.c` file. Previously LOAD and BUILD both
      called `restart()`.
- **2026-11-02 12:15 -08:00** — Completed M9.5. Added class layout (base at
      offset zero, natural member alignment, empty classes, FP members), `this`
      in `R0`, member access and calls, constructors with member initializer
      lists, destructors at scope exit and return paths, single inheritance
      with base constructor/destructor order, static members, and memberwise
      copy construction/assignment. Windows/WSL both pass 21/21 CTest tests;
      `kr32_tests` reports 159 test cases and 4702 assertions. M9.6 (virtual
      dispatch and RTTI) is next.
- **2026-11-02 12:20 -08:00** — Completed M9.6. Added vtable address-point
      layout, vptr installation in constructors/destructors, inherited and
      overridden virtual slots, virtual calls through pointers, abstract-class
      rejection/pure-virtual stubs, typeinfo (`_ZTI`/`_ZTS`), `typeid`, and
      pointer `dynamic_cast`. Windows/WSL both pass 21/21 CTest tests;
      `kr32_tests` reports 162 test cases and 4711 assertions. M9.7
      (matrix/transform codegen) is next.
- **2026-11-02 12:25 -08:00** — Clarified FPR display for float producers:
      registers with a zero upper half are labelled with their `f32` value in
      DOUBLE view, and the hover tooltip shows both interpretations and raw hex.
      Verified `float 2.5f` loads as `0x40200000`; the earlier `5.3e-315` was
      the raw 64-bit double interpretation, not a bad load.
- **2026-11-02 12:26 -08:00** — Completed M9.7. Ported tile and
      transform-stack instruction encoding into the object assembler, added
      `include/kr32/matrix.hpp` and freestanding preprocessor injection for
      `kr32::vec4`/`mat4`, and lowered matrix multiply/add/transpose, vector
      transform, dot, and transform-stack push/pop/drop to tile instructions.
      Added tile-encoding and matrix/aliasing/stack tests. Windows/WSL both pass
      21/21 CTest tests; `kr32_tests` reports 165 test cases and 4735
      assertions. M9.8 (templates and static initialization) is next.
- **2026-11-02 12:31 -08:00** — Completed M9.8 for function templates, weak
      symbols, and static initialization. Added explicit template-argument
      capture, instantiation by explicit arguments/deduction with
      deduplication, weak emission for template/inline symbols, `.weak`
      object-assembler support, and pre-main initialization of non-constant
      globals in declaration order. Added multi-TU template/inline and static
      init tests. Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports
      169 test cases and 4745 assertions. Class templates, COMDAT groups, static
      destruction, and guarded local statics remain deferred; M9.9
      (`new`/`delete` and exceptions) is next.
- **2026-11-02 12:33 -08:00** — Updated `examples/types.c` to use libc
      `printf` with `%d`, `%u`, `%.2f`, and `%.4f` instead of the hand-written
      `putchar` helpers, demonstrating the new libc and FP formatting.
- **2026-11-02 12:35 -08:00** — Fixed synthetic-header line accounting: each
      `<stdio.h>`/`<stdlib.h>`/`<string.h>`/`<stdarg.h>`/`<stddef.h>`/`<math.h>`
      injection now occupies exactly one source line, so debugger line mappings
      no longer shift after an include. Added a regression test with `stdio.h`.
- **2026-11-02 12:36 -08:00** — Started M9.9. Added `new`/`delete` lowering to
      `malloc`/`free` for scalars, objects, and scalar arrays, including
      constructor and destructor calls. Windows/WSL both pass 21/21 CTest tests;
      `kr32_tests` reports 171 test cases and 4753 assertions. Exceptions,
      table-driven cleanup/full unwinding, class-array helpers, and static
      destruction remain; M9.9 stays in progress.
- **2026-11-02 12:38 -08:00** — Scoped debug line lookup to the containing
      function. Runtime/libc addresses with no line records no longer inherit
      the last line of `main`, which caused line 71 to flash when stepping into
      `printf`.
- **2026-11-02 12:48 -08:00** — Made libc source-steppable: moved the C libc to
      `src/cc/libc.c`, embed it via CMake, keep its debug records with source id
      1, and load/switch between all debug source files in the GUI. Stepping
      into `printf`/`malloc` now shows `src/cc/libc.c` and its locals.
- **2026-11-02 12:51 -08:00** — Extended M9.9 with scalar/pointer exceptions.
      Added throw, catch-by-type, catch-all, nested try/catch, `throw;`,
      cross-function propagation via per-call pending checks, and destructor
      unwinding for live locals/caller frames. Windows/WSL both pass 21/21 CTest
      tests; `kr32_tests` reports 174 test cases and 4765 assertions. M9.9
      remains in progress for the table-driven `.cpp_except`/LSDA/`__cxa_*`
      runtime, class exceptions, class arrays, static destruction, and
      constructor-completion flags.
- **2026-11-02 12:56 -08:00** — Added class-array `new[]`/`delete[]` support with
      a count cookie, default construction of every element, reverse
      destruction, and cookie-base free. Windows/WSL both pass 21/21 CTest tests;
      `kr32_tests` reports 174 test cases and 4767 assertions. M9.9 remains in
      progress for the table-driven ABI exception runtime, class exceptions,
      static destruction, and constructor-completion flags.
- **2026-11-02 13:01 -08:00** — Added static construction and reverse-order
      destruction for global objects via `__kr32_cxx_fini`, plus
      `kr32/runtime.hpp` declarations with C-linkage runtime symbols.
      Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 175 test
      cases and 4769 assertions. M9.9 remains in progress for the table-driven
      ABI exception runtime, class exceptions, and constructor-completion
      flags.
- **2026-11-02 13:02 -08:00** — Added the remaining C string/memory routines:
      `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`,
      `strrchr`, `strstr`, `strtok`, `strdup`, `memmove`, `memcmp`, and
      `memchr`, with `<string.h>` prototypes, unit coverage, and
      `examples/strings.c`.
- **2026-11-02 13:09 -08:00** — Added `examples/binarytree.c`, a KR32 C AVL
      tree of 30 engineer surnames using `malloc`, `strdup`, `strcmp`, and
      `printf`; validated balance at height 6 and added a CTest. Also broke
      self-referential C struct type cycles in the compiler with weak pointer
      element types.
- **2026-11-02 13:17 -08:00** — Added `.cpp_except` LSDA records and
      `__kr32_personality_v0` table-driven catch selection, class exception
      objects with `__cxa_allocate_exception`/`begin_catch`/`end_catch`,
      constructor-completion cleanup for throwing class-array constructors, and
      a fixed-capacity `__cxa_atexit` registry. Windows/WSL both pass 21/21
      CTest tests; `kr32_tests` reports 177 test cases and 4798 assertions.
      M9.9 remains in progress for the true R7-chain unwinder/cleanup landing
      pads, exception-object destruction, and general temporary completion
      flags.
- **2026-11-02 13:33 -08:00** — Completed M9.9. Replaced the pending-flag path
      with an R7 frame-chain unwinder (`.cpp_except` function descriptors and
      call-site ranges, cleanup landing pads, restored `SP`/`R7`,
      `_Unwind_Resume`, `SETSP` stack restore), refcounted class-exception
      destruction, `__cxa_rethrow`, and class-array constructor cleanup that
      resumes through handlers. Windows/WSL both pass 21/21 CTest tests;
      `kr32_tests` reports 178 test cases and 4802 assertions. M9.10
      (threads/TLS/atomics) is next.
- **2026-11-02 13:52 -08:00** — Completed M9.10 stage 1: `vm::ClusterHost`
      over `core::Cluster` with one guest thread per core, create/join/yield/
      exit, futex wait/wake, and syscall vectors 44..50. Added stage-1 thread
      tests. Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 183
      test cases and 4841 assertions. M9.10 remains in progress for guest
      TCB/TLS, FPR/TSP context, compiler `thread_local`/atomics, and
      synchronization headers.
- **2026-11-02 14:03 -08:00** — Completed M9.10 stage 2: exact TCB layout at
      `R11`, per-thread stacks/TLS, guest join, and full GPR/SP/PC/FLAGS/FPR/TSP
      context save-restore. Added TCB, context, and guest-join tests. Windows/WSL
      both pass 21/21 CTest tests; `kr32_tests` reports 186 test cases and 4885
      assertions. M9.10 remains in progress for compiler `thread_local`/atomics
      and synchronization headers.
- **2026-11-02 14:16 -08:00** — Completed M9.10 stage 3: compiler
      `thread_local` via R11-relative TLS, LL/SC atomics, object-assembler
      LL/SC/FENCE, and the `kr32/thread.hpp` thread/mutex/condition_variable/
      atomic surface. Added two-core LL/SC counter, TLS, atomic, and header
      tests. Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 191
      test cases and 4917 assertions. M9.10 remains in progress for guarded
      statics, initialized TLS, and multi-thread mutex/condvar tests.
- **2026-11-02 14:27 -08:00** — Completed M9.10. Added guarded function-local
      statics, initialized `.tls` images loaded into thread TCBs, and a
      two-thread C++ mutex-protected counter test. Fixed scalar call
      initializers, `!` codegen, and cluster LL/SC reservation invalidation.
      Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 193 test
      cases and 4951 assertions. M9.11 (C++ debug/GUI/DAP) is next.
- **2026-11-02 14:48 -08:00** — Started M9.11. Added C++ debug records for
      sources, lines, functions, locals, globals, classes, members, `this`, and
      types; extended `debug::Info` with class/member records. Added
      `debug::ThreadInfo` snapshots/selection, DAP `threads` listing and
      synthetic per-thread frames, and a GUI thread prefix in the call-stack
      panel. Windows/WSL both pass 21/21 CTest tests; `kr32_tests` reports 195
      test cases and 4972 assertions. M9.11 remains in progress for full
      per-thread stacks/variables and GUI thread selection.
- **2026-11-02 15:07 -08:00** — Completed M9.11/M9.12. Added DAP/GUI class
      member expansion, wired `kr32 cxx` and `kr32 build file.cpp` with C
      runtime auto-link, updated README/debug docs, added `cli_cxx_help` and
      `run_cxx_demo` CTest cases, and passed Windows/WSL (23/23 CTest; 195 test
      cases / 4972 assertions). Tag `m9` marks the final M9.12 commit. Deeper
      per-thread variable expansion and the extra compiler/configuration matrix
      remain deferred.
- **2026-11-02 17:23 -08:00** — Merged `m9-cxx` into `main`. Resolved the
      C/libc and C++ ABI, object-assembler, UI, test, and tracker conflicts;
      both front ends now share one freestanding C runtime/libc. The merged tree
      passes the UI-enabled Windows matrix and WSL matrix, and
      `kr32 build examples/hello.cpp --run` returns 42.
- **2026-11-02 17:36 -08:00** — Post-merge cleanup: fixed the C++ test harness
      to link the 1 MiB test VM at the matching heap boundary, implemented the
      public `cxx::Emit::Object` path, decoded `SETSP` in the disassembler, and
      merged the C/C++ logs and documentation. Full merged gate: 217 unit cases
      / 5202 assertions; 31/31 Windows CTest; 29/29 WSL CTest.
- **2026-11-02 17:41 -08:00** — Integrated C++ source loading into the GUI:
      LOAD accepts `.cpp`/`.cc`/`.cxx`, compiles with `cxx::compile`, and links
      the same shared C libc. Headless GUI C++ load passes. Remaining P0
      multi-TU/thread-debug work is assigned to a follow-up branch; see
      `cpp-handoff.md`.
- **2026-11-02 17:50 -08:00** — Exposed the shared C libc to C++ through
      `extern "C"` freestanding headers (`stdio.h`, `stdlib.h`, `string.h`,
      `math.h`, `stddef.h`), added `examples/cxx_libc.cpp`, and verified C++
      calls `printf`/`strcpy` from the same libc. Final merged gate: 217 unit
      cases / 5202 assertions; 33/33 Windows Debug/Release/Ninja; 30/30 WSL
      GCC/Clang/ASan.
- **2026-11-02 17:52 -08:00** — Created `m9-p0` from merged `main` and fixed
      handoff P0 items 1–6: per-TU init/fini arrays, link-time TLS layout,
      merged exception tables, blocking `__cxa_guard_acquire`, and class static
      locals, plus a 3-TU harness. Added `ClusterHost::debug_threads`, a
      `debug::Session` thread provider/refresh, and DAP/GUI refresh hooks for
      P0 7. Windows/WSL (UI off) both pass 29/29 CTest; `kr32_tests` reports 222
      cases / 5317 assertions. Tag `m9` remains at `9707b55`; end-to-end
      ClusterHost-backed GUI/DAP sessions remain open.

- **2026-11-02 17:59 -08:00** — Merged `m9-p0` into `main`. Resolved the
      handoff/tracker conflicts and combined the live-thread hooks with the
      UI-enabled main branch. Final gate: 225 unit cases / 5331 assertions;
      33/33 Windows Debug/Release; 30/30 WSL GCC/Clang/ASan. Remaining work is
      the GUI/DAP-owned ClusterHost session and P1 feature work.
- **2026-11-02 19:35 -08:00** — Fixed the load-error popup: solid opaque panel,
      dimmed modal backdrop, white wrapped message text, and centered layout.
      Startup load failures now show the same popup. Added a UI pixel test;
      226 cases / 5333 assertions, 33/33 Windows, 30/30 WSL.
- **2026-11-02 21:50 -08:00** — Fixed source-line OVER when the next source line
      is inside a skipped branch. Step-over now repeatedly advances by one
      instruction (skipping calls) until control reaches a different source
      line, instead of requiring a single precomputed target line. Added a
      false-if regression and renamed the libc demo to `libc-test.c`.
      227 cases / 5345 assertions; 33/33 Windows; 30/30 WSL.
- **2026-11-02 22:06 -08:00** — Audited the KR16 hover surface and filled the KR32 gaps:
      GPR/PC/SP tooltips, flag meaning/state tooltips, memory-byte tooltips
      with cell highlighting, and richer variable tooltips for decimal/signed
      values, characters, strings, pointers, arrays, and aggregates. The
      existing instruction/addressing and FPR hovers remain.
- **2026-11-02 22:15 -08:00** — Added exported heap accounting globals
      `heap_allocated_blocks` and `heap_free_blocks` (updated by malloc, split,
      free, and coalesce) and `_heapwalk()` to validate the physical chain,
      headers/footers, free-list links, and counts, then resynchronize the
      counters. Verified the globals in the GUI variables panel and added
      heapwalk regression coverage; 228 cases / 5359 assertions.
- **2026-11-02 22:46 -08:00** — Expanded both language front ends: C now supports all
      compound assignments, prefix/postfix increments, pointer-scaled
      arithmetic, multidimensional arrays, conditional expressions, and
      correct switch fallthrough/default dispatch. C++ now preserves
      multidimensional-array dimension order, scales pointer increments and
      compound assignments, and implements switch with fallthrough. Added
      C/C++ regression coverage; 240 cases / 5482 assertions.
- **2026-11-02 23:02 -08:00** — Added C `do`/`while`, C++ `goto`/labels,
      and C++ range-based `for` over arrays. Added regression coverage for
      all three and retained the earlier compound/array/ternary/switch work.
      242 cases / 5500 assertions.
- **2026-11-02 23:09 -08:00** — Added the C unsigned integer model:
      `unsigned int`/`unsigned char`, `u`/`U` literals, zero-extending
      loads, unsigned comparisons/division/remainder/logical right shifts,
      and unsigned `size_t`. Fixed signed division/remainder to use
      `DIVS`/`MODS`, added those object-assembler aliases, and treated
      `long` as the 32-bit ILP32 type. `short`/`long long` remain diagnosed.
      243 cases / 5514 assertions.
- **2026-11-02 23:22 -08:00** — Added local aggregate initializers for C
      (`int a[3] = {…}`, nested arrays, structs/unions, zero-filled tails)
      and C++ (nested arrays and C-style aggregate structs). Global aggregate
      initializers and designated initializers remain next-stage work.
      245 cases / 5530 assertions.
- **2026-11-02 23:25 -08:00** — Added C++ access-control enforcement for
      data members, member functions, constructors, and protected base
      access. Private/protected violations now produce diagnostics instead
      of silently compiling. Friends and static-member access remain
      next-stage work. 246 cases / 5536 assertions.
- **2026-11-02 23:32 -08:00** — Added global aggregate and array
      initializers for C and C++: nested arrays, structs, string-initialized
      char arrays, and zero-filled tails. Designated initializers and
      non-constant global initializers remain next-stage work.
      248 cases / 5552 assertions.
- **2026-11-02 23:39 -08:00** — Added first class-template support:
      `template <typename T> class ...` with parameterized fields, member
      functions, constructors, and multiple instantiations. Instantiated
      class names and symbols are sanitized for the assembler. COMDAT/
      multi-TU deduplication, external member definitions, non-type template
      parameters, and nested templates remain deferred. 249 cases / 5554
      assertions.
- **2026-11-02 23:44 -08:00** — Made class-template member functions weak
      in every instantiation, so two translation units that instantiate the
      same `Box<int>` link through the existing weak-symbol merge instead of
      colliding. Added a two-TU class-template link/run regression. COMDAT
      section groups and nontrivial template static data remain deferred.
      250 cases / 5558 assertions.
- **2026-11-02 23:53 -08:00** — Added the first by-value class ABI:
      caller-owned copies for class parameters, hidden `R4` destination
      pointers for class returns, and caller-side `sret` temporaries. Class
      parameters are copied in the callee so mutations do not affect the
      caller; class-returning calls can feed declarations, assignments, and
      other calls. `docs/abi.md` records the implemented convention and notes
      that the full revision-1 aggregate classification is still target
      work. 251 cases / 5560 assertions.
- **2026-11-03 00:01 -08:00** — Routed C++ static destruction through
      a weak `__kr32_cxx_fini` guard and made the C runtime `exit()` call it,
      so `exit()` now runs `.fini_array` exactly once. Added assembler support
      for undefined weak symbols and linker resolution of weak undefined
      relocations to zero. Added an `exit()` static-destructor regression.
      252 cases / 5563 assertions.
- **2026-11-03 00:08 -08:00** — Expanded the thread header surface:
      `std::thread`, `std::mutex`, `std::condition_variable`,
      `std::atomic_int`, and `std::this_thread` now resolve through injected
      `<thread>`, `<mutex>`, `<condition_variable>`, and `<atomic>` headers.
      Added default-constructible/joinable thread state, `this_thread::yield`
      and `get_id`, and a single-injection guard for the shared header block.
      253 cases / 5565 assertions.
- **2026-11-03 00:14 -08:00** — Added C `short`/`unsigned short`
      as 16-bit storage: `LOAD16S`/`LOAD16U`/`STORE16`, sign-extension on
      conversion, 2-byte struct fields and arrays, and correct global scalar
      emission. Function parameters widen to 4-byte slots while preserving
      16-bit lvalues. `long long` remains unsupported. 254 cases / 5579
      assertions.
- **2026-11-03 00:20 -08:00** — Added C designated initializers for local
      and global aggregates: `.field = value` for structs and `[index] =
      value` for arrays, including continuation after a designator and
      zero-filled gaps. Nested aggregate designators remain future work.
      255 cases / 5593 assertions.
- **2026-11-03 07:14 -08:00** — Completed the first recommendation
      sprint: C++ unsigned literals, arithmetic conversions, unsigned
      comparisons/division/modulo/shifts, and unsigned overload selection;
      friend functions/classes plus private static-member access enforcement;
      and nested C designated initializers plus C++ field/array designators.
      Added regressions for each. 259 cases / 5618 assertions.
- **2026-11-03 07:25 -08:00** — Added per-thread TLS object lifecycle:
      thread-local class constructors run in `.Lcxx_init` for the main thread
      and through a `__kr32_thread_entry` trampoline for created threads;
      `__kr32_tls_fini` destroys them on normal thread return. ClusterHost
      discovers the weak trampoline symbol and passes the original entry/arg.
      Added main-thread and created-thread TLS lifecycle regressions.
      261 cases / 5632 assertions.
- **2026-11-03 07:31 -08:00** — Added captureless lambdas. Lambda bodies
      are materialized as unique functions, direct invocation works, and
      `auto f = []...` stores a callable function target for `f(...)` calls.
      Lambda captures are diagnosed as deferred. 262 cases / 5636 assertions.
- **2026-11-03 07:39 -08:00** — Added by-value lambda captures. Closure
      objects store captured locals, lambda functions receive a hidden
      closure pointer, and capture references in bodies load through it.
      `std::thread` now accepts function pointers and captureless lambdas via
      a `void*` entry overload; reference/default captures and capturing
      lambdas converted to function pointers are diagnosed. 264 cases /
      5639 assertions.
- **2026-11-03 07:44 -08:00** — Completed lambda capture forms:
      explicit by-value, explicit by-reference, `[=]`, and `[&]` closure
      captures, with reference holds stored as addresses and resolved through
      the closure. Added `std::thread::detach()` to both thread wrappers.
      265 cases / 5643 assertions.
- **2026-11-03 07:50 -08:00** — Thread API polish: `mutex::try_lock`,
      recursive mutexes with owner/count tracking, and
      `thread::hardware_concurrency()` on both thread wrappers. 266 cases /
      5645 assertions.
- **2026-11-03 07:58 -08:00** — C `long long` phase 1: 64-bit type
      metadata (`sizeof`/alignment 8), 64-bit literals, pair loads/stores,
      add/subtract with carry/borrow, bitwise operators, 64-bit comparisons,
      global and local storage, and mixed int-to-long-long promotion. Multiply,
      divide, modulo, shifts, and long-long function arguments/returns are
      diagnosed as deferred. 267 cases / 5659 assertions.
- **2026-11-03 08:04 -08:00** — Long-long phase 2: 64-bit multiply
      through 32-bit partial products and `MULHU`, plus variable 64-bit
      left/right shifts with signed arithmetic right shift. Added
      `MULH`/`MULHU` to the object assembler. Divide/modulo and long-long
      calls remain deferred. 267 cases / 5659 assertions.
- **2026-11-03 08:21 -08:00** — Long-long phase 3: 64-bit function
      parameters consume two integer registers, calls stage/load both
      halves, and 64-bit returns preserve `R0`/`R1`. Added C regression for
      passing and returning `long long`. Divide/modulo and `printf` support
      remain deferred. 268 cases / 5673 assertions.
- **2026-11-03 08:26 -08:00** — Added unsigned 64-bit divide and
      remainder helpers in shared libc C source (`__kr32_ulldiv`,
      `__kr32_ulmod`), wired long-long `/` and `%` to them, and documented
      signed long-long division as deferred. 269 cases / 5687 assertions.
- **2026-11-03 08:34 -08:00** — Completed signed long-long division:
      fixed 64-bit relational comparison to read the right-hand high word,
      enabled the signed libc divide/remainder helpers, and added signed
      quotient/remainder regressions. 270 cases / 5701 assertions.
- **2026-11-03 08:43 -08:00** — Added `printf` long-long formats
      (`%lld`, `%llu`, `%llx`, `%llo`), 64-bit `va_arg`, and variadic
      two-word staging. Fixed 64-bit `!=` comparison and raised the test
      harness step budget for slower 64-bit formatting. 271 cases / 5715
      assertions.
- **2026-11-03 08:50 -08:00** — Added explicit class-template
      specializations (`template <> class Box<int>`): parser records
      specialization arguments, codegen registers specialization keys, and
      instantiation prefers them over the generic template. 272 cases /
      5717 assertions.
- **2026-11-03 08:56 -08:00** — Range-for now accepts braced initializer
      lists by materializing a temporary array and iterating it; added
      regression. 273 cases / 5719 assertions.
- **2026-11-03 09:02 -08:00** — Added `std::initializer_list<T>`
      function parameters and braced-call arguments: the compiler materializes
      the backing array and list object, and overload resolution recognizes
      initializer-list parameters. Sanitized namespaced class-template keys
      for assembler-safe symbols. 274 cases / 5721 assertions.
- **2026-11-03 09:08 -08:00** — Added `auto values = {1, 2, 3};`
      initializer-list variables: infer element type, instantiate
      `std::initializer_list`, materialize the backing array, and copy the
      list object into the local. 275 cases / 5723 assertions.
- **2026-11-03 09:13 -08:00** — Range-for now iterates named
      `std::initializer_list` variables, reading `data_`/`size_` from the
      list object. 276 cases / 5725 assertions.
- **2026-11-03 09:20 -08:00** — C++ long-long phase 1: type parsing,
      64-bit local storage, sign normalization, arithmetic/comparisons,
      pair temporaries, and signed/unsigned divide/remainder through the
      shared libc helpers. Function parameters/returns and `printf` remain
      deferred. 277 cases / 5727 assertions.
- **2026-11-03 09:27 -08:00** — C++ long-long phase 2: two-register
      parameters, 64-bit call staging/loading, 64-bit returns, and variadic
      pair staging so `printf("%lld", ...)` works. Large `long long` literals
      remain deferred. 279 cases / 5731 assertions.
- **2026-11-03 09:34 -08:00** — C++ long-long literals beyond 32 bits now
      emit both halves, including negative literals; added 64-bit addition
      carry propagation and regressions. 280 cases / 5733 assertions.
- **2026-11-03 09:39 -08:00** — C++ long-long globals, fields, and
      static data members now emit/load both halves; constant globals emit
      two words. Added field/global/static regressions. 282 cases / 5737
      assertions.
- **2026-11-03 09:44 -08:00** — Verified and covered 64-bit elements in
      C++ arrays and references; TLS image initialization now copies the full
      eight bytes (TLS long-long execution still fails and remains deferred).
      283 cases / 5741 assertions.
- **2026-11-03 09:49 -08:00** — Added partial class-template
      specializations with one-level pointer patterns (`Box<T*>`), including
      template-parameter binding and generic fallback. Added regression. 284
      cases / 5743 assertions.
- **2026-11-03 09:55 -08:00** — Added `std::thread::id` as a nested
      alias plus `std::thread::get_id()`, in both the header and injected
      preprocessor declarations; added regression. 285 cases / 5745
      assertions.
- **2026-11-03 10:02 -08:00** — Added `kr32::thread::id` and
      `get_id()`; both native and `std` wrappers now expose thread ids.
      285 cases / 5747 assertions.
- **2026-11-03 10:08 -08:00** — `std::this_thread::get_id()` now
      returns `std::thread::id` (typed) instead of `int`; added a regression
      that copies and compares the id. 285 cases / 5749 assertions.
- **2026-11-03 10:14 -08:00** — Added zero-wait timed thread API
      surface: `std::this_thread::sleep_for`, `std::mutex::try_lock_for`,
      and `std::recursive_mutex::try_lock_for`. Timeouts currently do not
      block; only immediate acquisition is implemented. 286 cases / 5751
      assertions.
- **2026-11-03 10:20 -08:00** — Timed thread helpers now busy-wait on
      host seconds instead of returning immediately; `sleep_for` and
      `try_lock_for` honor coarse whole-second timeouts. 286 cases / 5751
      assertions.
- **2026-11-03 10:28 -08:00** — Added `std::this_thread::sleep_until`
      with past-deadline fast return and coarse host-second waiting.
      287 cases / 5753 assertions.
- **2026-11-03 10:34 -08:00** — Added condition-variable
      `wait_for`/`wait_until` with coarse busy polling, and qualified the
      mutex reference parameter so `condition_variable::wait` resolves.
      288 cases / 5755 assertions.
- **2026-11-03 10:40 -08:00** — Added `docs/templates.md` summarizing
      supported template forms (function, class, explicit and pointer-pattern
      partial specializations) and documenting deferred template work.
      288 cases / 5755 assertions.
- **2026-11-03 10:45 -08:00** — Added out-of-class template member
      definitions (`template <typename T> T Box<T>::get()`), attached to
      generic instantiations and explicit specializations. 290 cases / 5759
      assertions.
- **2026-11-03 10:54 -08:00** — Added class-template static data
      member definitions; initializers attach per instantiation and late
      template-static globals emit after function code generation.
      291 cases / 5761 assertions.
- **2026-11-03 11:00 -08:00** — Added scope-qualified access to
      class-template static data (`Box<int>::value`) by resolving template
      arguments in expression names. 291 cases / 5761 assertions.
- **2026-11-03 11:07 -08:00** — Added integer non-type template
      parameters for class templates, including value-aware mangling and
      bound-constant lookup inside member bodies. 292 cases / 5763
      assertions.
- **2026-11-03 11:13 -08:00** — Added explicit function-template
      specializations (`template <> int twice<int>(int)`), matching them
      against call arguments and reusing the generic instantiation symbol.
      293 cases / 5767 assertions.
- **2026-11-03 11:20 -08:00** — Fixed local scalar declarations initialized by
      function-template specializations, which previously unwrapped the
      argument as a cast; repeated specialized calls now use the specialized
      body. 293 cases / 5767 assertions.
- **2026-11-03 11:27 -08:00** — Fixed mixed specialization/generic
      overload selection by excluding template-instantiated functions from
      ordinary candidates; specialized and generic calls can coexist.
      293 cases / 5769 assertions.
- **2026-11-03 11:34 -08:00** — Non-type template parameters now size
      class-template array members, with bound-name substitution during
      instantiation. 294 cases / 5771 assertions.
- **2026-11-03 11:40 -08:00** — Non-type template arguments now accept
      integer constant expressions with arithmetic, shifts, and bitwise
      operators. 295 cases / 5773 assertions.
- **2026-11-03 11:47 -08:00** — Partial class-template matching now supports
      reference patterns and multi-level pointer patterns. 296 cases / 5775
      assertions.
- **2026-11-03 11:51 -08:00** — Added fixed-size array template arguments
      and partial-specialization patterns such as `T[3]`. 297 cases / 5777
      assertions.
- **2026-11-03 11:59 -08:00** — `std::thread` now accepts capturing lambda
      closures by materializing the closure and generating a per-lambda thread
      entry thunk. Compile-level coverage added; execution requires a
      multi-core cluster. 298 cases / 5778 assertions.
- **2026-11-03 12:08 -08:00** — Added `initializer_list::begin`/`end`
      intrinsics and fixed member-call initializers for scalar locals,
      including pointer-returning methods. 300 cases / 5782 assertions.
- **2026-11-03 12:15 -08:00** — Verified `thread_local long long` in the
      test harness and fixed the ClusterHost TLS template copy to start at
      the TCB TLS offset. 301 cases / 5784 assertions.
- **2026-11-03 12:22 -08:00** — CLI executable loading now captures the
      `.tls` section, installs a TCB/TLS template at `R11`, and initializes
      thread-local data. Standalone `kr32 vm`/`build --run` now handles int
      and `long long` thread-locals. 301 cases / 5784 assertions.
- **2026-11-03 12:30 -08:00** — Added 64-bit C++ compound assignments
      (`+=`, `-=`, `*=`, `/=`, `%=`, bitwise, shifts) with carry/borrow and
      helper calls. 302 cases / 5786 assertions.
- **2026-11-03 12:38 -08:00** — Added C++ long-long cast extension and
      wide shift handling for counts of 32 or more. 303 cases / 5790
      assertions.
- **2026-11-03 12:45 -08:00** — Initializer lists now store 64-bit
      elements as full pairs, enabling `initializer_list<long long>` and
      arrays of `long long`. 304 cases / 5792 assertions.
- **2026-11-03 12:52 -08:00** — Added 64-bit integer to `double`/`float`
      conversion using high-word scaling and unsigned low-word conversion.
      305 cases / 5794 assertions.
- **2026-11-03 13:00 -08:00** — Added floating-point to signed 64-bit
      conversion with high/low decomposition, negation, and 32-bit
      reconstruction. 306 cases / 5796 assertions.
- **2026-11-03 13:08 -08:00** — Added register classification for trivial
      four-byte class parameters; larger aggregates still use hidden-pointer
      copies/sret. 307 cases / 5798 assertions.
- **2026-11-03 13:16 -08:00** — Trivial four-byte class returns now use a
      register instead of `sret`, with callers materializing the returned
      object locally. 308 cases / 5800 assertions.
- **2026-11-03 13:24 -08:00** — Extended register-class ABI coverage to
      one- and two-byte trivial classes; 8-byte aggregates still use the
      hidden-pointer path. 309 cases / 5802 assertions.
- **2026-11-03 13:34 -08:00** — Two-word trivial aggregates (up to eight
      bytes) now return in `R0`/`R1`; callers preserve the high word while
      materializing the result. Eight-byte parameters still use hidden
      pointers. 310 cases / 5804 assertions.
- **2026-11-03 13:42 -08:00** — Two-word trivial aggregates now also pass
      in register pairs; initializer-list classes remain on the hidden-pointer
      path. 310 cases / 5804 assertions.
- **2026-11-03 13:50 -08:00** — Documented and regression-tested register
      ABI for one- and two-word floating aggregates (`float`/`double` members).
      311 cases / 5806 assertions.
- **2026-11-03 13:58 -08:00** — Added mixed integer/floating aggregate
      register-ABI coverage. 312 cases / 5808 assertions.
- **2026-11-03 14:06 -08:00** — Added multi-core cluster runtime coverage
      for capturing-lambda `std::thread` entry thunks, verifying captured
      values and thread exit codes. 313 cases / 5813 assertions.
- **2026-11-03 14:14 -08:00** — Added multi-core runtime coverage for
      captureless-lambda `std::thread` entries. 314 cases / 5818 assertions.
- **2026-11-03 14:22 -08:00** — Added multi-core runtime coverage for
      function-pointer `std::thread` entries. 315 cases / 5823 assertions.
- **2026-11-03 14:30 -08:00** — Fixed capturing-thread construction to store
      the thread handle at the object address, restoring `join()` and mutex
      synchronization across cluster threads. 316 cases / 5827 assertions.
- **2026-11-03 14:38 -08:00** — Added cluster runtime coverage for capturing
      threads writing shared globals; `join()` makes the write visible to the
      primary thread. 317 cases / 5831 assertions.
- **2026-11-03 14:46 -08:00** — Added cluster runtime coverage for detached
      `std::thread` workers running to completion. 318 cases / 5835
      assertions.
- **2026-11-03 14:54 -08:00** — Non-type template array bounds now accept
      constant offsets such as `N + 1`. 319 cases / 5837 assertions.
- **2026-11-03 15:02 -08:00** — Constructors whose single parameter is
      `std::initializer_list` now accept braced arguments. 320 cases / 5839
      assertions.
- **2026-11-03 15:10 -08:00** — Template static data members are emitted
      weak and merge across translation units. 321 cases / 5843 assertions.
- **2026-11-03 15:18 -08:00** — Template function-local statics and their
      guards are emitted weak with function-qualified symbols, merging across
      translation units. 322 cases / 5847 assertions.
- **2026-11-03 15:26 -08:00** — Functions returning `std::initializer_list`
      accept braced lists, backed by compiler-emitted static storage. 323
      cases / 5849 assertions.
- **2026-11-03 15:34 -08:00** — `auto` locals deduce class return types from
      call initializers, including `std::initializer_list`. 324 cases / 5851
      assertions.
- **2026-11-03 15:42 -08:00** — Template array bounds accept constant
      multipliers with offsets such as `N * 2 + 1`. 325 cases / 5853
      assertions.
- **2026-11-03 15:50 -08:00** — Constructor selection now scores class
      arguments, so copy constructors are preferred over scalar overloads.
      326 cases / 5855 assertions.
- **2026-11-03 15:58 -08:00** — Constructor argument scoring unwraps reference
      parameters, so reference copy constructors are selected correctly. 327
      cases / 5857 assertions.
- **2026-11-03 16:06 -08:00** — Added regression coverage for aggregates
      larger than eight bytes using hidden-pointer parameters and `sret`
      returns. 328 cases / 5859 assertions.
- **2026-11-03 16:14 -08:00** — Range-for now copies 64-bit elements as
      full pairs, fixing `initializer_list<long long>` constructor loops.
      329 cases / 5861 assertions.
- **2026-11-03 16:22 -08:00** — Covered empty braced initializer lists for
      constructors and functions returning `std::initializer_list`. 331 cases
      / 5865 assertions.
- **2026-11-03 16:30 -08:00** — Added `std::thread::release`/`adopt` for
      explicit handle transfer, with cluster runtime coverage of the moved
      worker. 332 cases / 5871 assertions.
- **2026-11-03 16:38 -08:00** — Implemented `.section` support, COMDAT
      template-function sections, and linker deduplication of matching
      COMDAT sections. 333 cases / 5877 assertions.
- **2026-11-03 16:46 -08:00** — `auto` locals now deduce every non-void call
      return type, including pointers, `long long`, floating point, and
      classes. 334 cases / 5879 assertions.
- **2026-11-03 16:54 -08:00** — Added C++ debug-info coverage for template
      and lambda functions, verifying parsed functions, lines, and variables.
      335 cases / 5888 assertions.
- **2026-11-03 17:02 -08:00** — Completed the roadmap audit and added
      `docs/roadmap.md` with completed areas and known limitations. Final
      suite: 335 cases / 5888 assertions.
- **2026-11-03 18:07 -08:00** — Closed the prioritized roadmap gaps:
      FP-only aggregates up to 16 bytes now use FPRs, integer/FP/memory
      classification boundaries match the ABI, `std::move` and thread move
      construction work, dependent template array bounds support general
      constant arithmetic, nested template-id partial patterns match, COMDAT
      groups cover template data and local statics, `long long` suffixes select
      64-bit arithmetic, and dynamic `thread_local` initialization runs from
      the C++ init array. 343 cases / 5914 assertions.
- **2026-11-03 18:19 -08:00** — FP-only aggregate passing now pads the
      second eightbyte slot and uses word transfers for 9–12 byte aggregates,
      avoiding frame overruns while preserving `F0`/`F1` returns. 344 cases /
      5916 assertions.
- **2026-11-03 18:20 -08:00** — Added an end-to-end CLI dynamic-TLS
      regression (`examples/cxx_tls.cpp`) to the CTest gate. 344 cases /
      5916 assertions.
- **2026-11-03 19:59 -08:00** — Fixed register/FPR hit targets in the
      debugger UI. Cells are now stored by register index instead of
      row-major draw order, so clicking R1 edits R1 and FPR/FPR targets align.
- **2026-11-03 20:22 -08:00** — Added selectable T0/TSP matrix cells with
      floating-point edit popups, widened cell spacing, and hover tooltips for
      tile cells, TSP, CR3/CR6/CR10, and PTBR. Control rows have more vertical
      spacing.
- **2026-11-04 08:14 -08:00** — Added the KR32 settings gear and dialog with
      guest command-line arguments (`sys_argc`/`sys_arg`), stop-at-main and
      assembler-listing controls, persisted settings, plus tests for argument
      delivery and settings-dialog rendering.
- **2026-11-04 08:17 -08:00** — Added settings-dialog regression coverage and
      fixed the minimum-width layout clamp that could assert at 800px wide.
- **2026-11-04 08:37 -08:00** — The program loader now builds `argc`/`argv` on the guest stack
      before entry and passes them to `main`; added
      `examples/mainentry.c` and a CLI regression.
- **2026-11-04 08:52 -08:00** — `main(argc, argv)` is now populated by the
      program loader from `HostOptions.args`; added
      `examples/mainentry.c`, settings-dialog controls, and C/C++ argv
      regressions. 347 cases / 5934 assertions.
- **2026-11-04 09:06 -08:00** — Added `kr32 ld --flat --base`, a headerless
      contiguous image writer with zero-filled gaps/BSS, base validation, and
      flat-link tests for KR32OS.
- **2026-11-04 09:55 -08:00** — Added the 32 MiB framebuffer aperture at
      `0xFC00_0000`, framebuffer MMIO control page, device-backed pixel storage,
      and updated the ISA/KR32OS memory maps.
- **2026-11-04 10:11 -08:00** — Added `kr32 os` and `kr32 gui --os` with
      default kernel/BIOS resolution, raw OS loading, and an OS boot smoke test.
- **2026-11-04 10:15 -08:00** — Documented the BIOS as a separate
      `kr32os/bios/` ROM component with an assembly reset/privileged path and
      optional freestanding C helpers; updated the OS layout and build pipeline.
- **2026-11-04 10:27 -08:00** — Recorded OS design decisions: bump allocator
      first, stable numbered syscalls with validated user pointers, and
      multicore after the scheduler milestone.
- **2026-11-04 10:40 -08:00** — Recorded the serial tty, console input,
      user address-space, bitmap allocator, panic, and embedded-initrd design
      decisions.
- **2026-11-04 10:44 -08:00** — Recorded OS user-runtime, syscall table,
      process model, initrd, timer quantum, image limit, and filesystem design
      decisions.
- **2026-11-04 10:49 -08:00** — Recorded POSIX errno values, the fixed
      BootInfo page/R2 ABI, generated initrd assembly embedding, and the minimal
      writable-filesystem layout.
- **2026-11-04 10:52 -08:00** — Recorded block LBA/sector layout, deferred
      free-list timing, parked secondary-core startup, and zombie process exit
      policy.
- **2026-11-04 10:55 -08:00** — Recorded hybrid block completion, minimal
      shell commands, waitpid reaping, and full-state context-switch decisions.
- **2026-11-04 10:57 -08:00** — Recorded inode layout, circular run queue,
      user-fault termination, and fixed process-table/PID decisions.
- **2026-11-04 11:00 -08:00** — Recorded the 256-entry process table, POSIX-like
      wait status, K32F superblock, and `ls`/`cat`/`run` shell commands.
- **2026-11-04 11:02 -08:00** — Recorded K32I/64-entry initrd limits,
      -EAGAIN on process-table exhaustion, argc/argv plus serial fds for `run`,
      and spawn-now/fork-later process creation.
- **2026-11-04 11:04 -08:00** — Recorded console-only initial fd table, shell
      quoting rules, flat-initrd path model, and M5 block-IRQ transition.
- **2026-11-04 11:06 -08:00** — Recorded W^X user mappings, kernel-only
      framebuffer access, init-test behavior, and eager fork plus initrd exec.
- **2026-11-04 11:09 -08:00** — Recorded 32-byte directory entries, kernel
      shell through M5, framebuffer text/fill API, and interruptible idle loop.
- **2026-11-04 11:17 -08:00** — Added the reviewed-pending M0-M4 KR32OS
      implementation plan covering host prerequisites, milestones, gates, and
      logging/handoff instructions.
- **2026-11-04 11:21 -08:00** — Approved the five M0-M4 plan decisions:
      CRC-checked kernel.meta, form-feed clear, saved-context trampoline,
      M0-first ordering, and a 10,000-instruction virtual timer tick.
- **2026-11-04 11:23 -08:00** — KR32OS M0 complete: PowerShell/Bash/CMake
      build scaffolding, flat BIOS and kernel, banner, and `run_os_m0` CTest.
      Windows and WSL builds pass.
- **2026-11-04 11:32 -08:00** — KR32OS M1 complete: BootInfo loader/ABI,
      BIOS page tables and guard page, vector table, paging enable, kernel
      banner, and boot regression. Windows build/test pass.
- **2026-11-04 11:43 -08:00** — KR32OS M2/M3 complete: serial tty device and
      UI input path, polled kernel shell, vector/exception reporting, virtual
      timer IRQ, and fault/timer regressions. MMIO is now reachable through
      paging.
- **2026-11-04 12:00 -08:00** — KR32OS M4 core complete: user runtime,
      K32I initrd, W^X user mappings, 256-entry process table, user-return
      trampoline, syscall write/exit, and shell `ls`/`cat`/`run`. Deferred:
      bitmap/heap, kernel.meta, remaining syscall vectors, user-fault kill.
      Windows 40/40 and WSL 37/37 gates pass.
- **2026-11-04 12:13 -08:00** — KR32OS M4 infrastructure completed:
      `kernel.meta` CRC sidecar and BootInfo initrd range, bitmap page
      allocator and bump heap with `mem` statistics, all six syscall vectors,
      initrd fault/W^X test programs, user-fault containment (exit 139), and
      immediate reaping. Windows unit suite 353/353.
- **2026-11-04 13:33 -08:00** — OS source debugging: link `.sym` sidecars
      (CRC-checked), `.k32` OS images, source-id assignment for C objects,
      assembly `.file` line records for BIOS/boot/stubs, and automatic source
      following for assembly and C. `gui --os` now loads `.bin` plus its
      sibling `.sym`; both raw and container OS images are supported.
- **2026-11-04 14:28 -08:00** — ISA bulk stack operations: `PUSHR`/`POPR`
      for R1-R15 and `PUSHFP`/`POPFP` for all FPRs, with encoder, decoder,
      assemblers, disassembler, help, docs, tests, and OS syscall/timer stubs
      updated. TSP remains saved explicitly through `RDSP`/`WRSP`.
- **2026-11-04 18:47 -08:00** — C compiler quoted-include support:
      local headers are recursively expanded with `#pragma once`, cycles are
      stopped, and `#line` markers preserve source line numbers after includes.
      The initrd parser now uses typed `initrd_header`/`initrd_entry` structures
      instead of raw offsets.
- **2026-11-04 19:26 -08:00** — Debugger variable view: separate
      collapsible Locals/Globals sections, automatic struct/array expansion,
      pointer target inspection, and double-click register/PC/SP memory
      navigation. C debug metadata now emits struct members and array types.
- **2026-11-04 19:48 -08:00** — Variable arrays are collapsible and paginated:
      arrays show 16 elements per page with `< page N/M >` navigation, and
      expansion state is retained per local/global path.
- **2026-11-04 20:23 -08:00** — Array pager clicks: the left arrow steps back
      and the page text/right arrow advances; paging reports its new page in the
      status line. The previous left-half/right-half split made a click on the
      page number a no-op on page 1.
- **2026-11-04 20:44 -08:00** — Kernel address-space cleanup: `mem_phys_t`/
      `mem_virt_t` and physical-to-virtual helpers; user code frames come from
      the bitmap allocator; `sys_write` translates user virtual addresses with
      `user_virt_to_phys`; user stack remains an intentionally reserved
      identity-mapped physical window.
- **2026-11-04 21:12 -08:00** — Configurable debugger run budget in Settings;
      zero means unlimited. The debugger reports an exhausted run budget instead
      of silently appearing to stop at a breakpoint.
- **2026-11-04 21:27 -08:00** — Unlimited debugger runs execute in 100k-step
      slices from the UI frame loop, keeping the window responsive and letting
      Run pause the guest. Run-to uses the same sliced path when the limit is 0.
- **2026-11-05 00:51 -08:00** — Debugger toolbar stats: instruction count,
      active run time, instructions/second, and a CLR reset button. RUN turns
      green while an unlimited run is active and clears when paused/stopped.
- **2026-11-05 01:05 -08:00** — All Run/Run-To paths now execute
      100k-instruction slices. The RUN button is green while active; the
      configured budget still stops bounded runs and Run pauses unlimited ones.
- **2026-11-05 01:38 -08:00** — Console run-mode input no longer pauses
      execution; F6 breaks a running guest. The console title highlights green
      on hover, toolbar stats are brighter, and a bottom status bar shows the
      current status plus mode-specific key hints.
- **2026-11-05 02:22 -08:00** — Fixed matrix debug symbols: C++ array debug
      types now include element counts (`float[16]`), so `mat4` expands in the
      variables panel. Float scalars and members render as decimal values
      instead of hex bit patterns, aggregate rows show `{...}`, and tooltips use
      the same formatting. Built-in C++ header injection now emits `#line`
      restoration records, so source lines after `<kr32/matrix.hpp>` no longer
      shift by the injected header size. Added regression tests for C++ array
      debug records, source-line mapping, and scalar formatting.
- **2026-11-05 02:58 -08:00** — Standardized C++ `new`/`delete`: null-safe
      delete, placement new, `std::nothrow`, `std::bad_alloc` on allocation
      failure, array value/initializer-list initialization, sized array headers,
      pointee-aligned allocation, scalar constructor-failure cleanup, implicit
      base/member construction and vptr setup, and virtual destructor dispatch.
      Fixed qualified pointer declarations (`kr32::mat4*`). Added regression
      tests for allocation failure, placement, nothrow, initialization,
      alignment, implicit construction, and virtual deletion.
- **2026-11-05 08:56 -08:00** — Added `examples/balanced-tree.cpp`, an AVL
      tree example with virtual comparison/printing hooks. `EngineerTree` adds
      30 engineers by last name and `IntegerTree` adds 20 random integers.
      Fixed implicit virtual override detection (no `override` keyword needed),
      default construction of abstract base subobjects, and vptr setup for
      derived classes without user constructors. Added the example to CTest and
      the README command list.
- **2026-11-05 09:04 -08:00** — Console output now auto-scrolls only when the
      view is already at the bottom. Scrolling upward preserves the user's
      position as new text arrives; returning to the bottom resumes following.
      Added a pure scroll-policy helper and regression tests.
- **2026-11-05 10:18 -08:00** — Variable arrays now start collapsed on image
      load and whenever a new array key first appears. Clicking the array row
      expands or collapses it, and the explicit state is retained per key.
- **2026-11-05 10:29 -08:00** — Fixed debugger syscall handling: the standalone
      `Session` now handles vectors 34..41 (`getchar`, file-error returns,
      `time`, `random`) instead of faulting at `sys_random`. Added Host and
      debugger tests covering every advertised `32..43` vector. KR32OS now
      fills unused software-vector slots with a `BRK` stub before installing
      vectors 64..69, so undefined syscalls trap instead of jumping to zero.
- **2026-11-05 15:59 -08:00** — Added native open/save file dialogs. Platform
      API supports Windows common dialogs and Linux zenity/kdialog; host syscall
      51 returns a UTF-8 path into a guest buffer with cancel, unavailable,
      error, fault, invalid-argument, and buffer-too-small statuses. Selected
      paths are authorized through the host sandbox for subsequent guest
      `open`. Added C/C++ runtime wrappers, CLI/debugger wiring, and fake-dialog
      tests. The debugger now hosts file I/O through the same `vm::Host`, so
      selected files can be opened immediately.
- **2026-11-06 07:37 -08:00** — Added the unbuffered C `FILE` layer
      (`fopen`/`fread`/`fwrite`/`fprintf`/`fscanf`/seeking/status), formatted
      string output, and console/file separation. Added C compiler support for
      `extern` global declarations and missing control-character escapes, and
      C++ typedef registration. A CLI-level exception handler now reports
      embedded-libc compile failures instead of terminating. C++ can use the
      C `FILE` API.
- **2026-11-06 07:37 -08:00** — Implemented member binary operator
      overloading (`<<`, `>>`, arithmetic/comparison operators), operator
      mangling, class-reference returns, and chained reference results. Added
      the compact `<iostream>`/`<fstream>` subset over `FILE` with
      `std::ifstream`, `std::ofstream`, `std::fstream`, `std::cin`,
      `std::cout`, `std::cerr`, `std::endl`, `operator<<`, and `operator>>`.
- **2026-11-06 08:10 -08:00** — Added `errno`/`strerror`/`perror` and error
      propagation through FILE operations; `sscanf`/`scanf` via an in-memory
      FILE, `getline`/`getdelim`, and `%[...]` scansets; host `unlink`,
      `rename`, and `stat` syscalls (52–54) with libc `remove`/`rename`/`stat`/
      `fstat`; and `freopen`, `fgetpos`/`fsetpos`, `setvbuf`/`setbuf` (initially
      no-op buffering).
- **2026-11-06 08:27 -08:00** — Added full and line output buffering through
      `setvbuf`/`setbuf`, with flush on `fflush`, `fclose`, `fseek`, `ftell`,
      and stream reads; added `access`/`isatty` and `<unistd.h>`.
- **2026-11-06 08:35 -08:00** — Extended C++ operator support to member
      `operator[]`, compound assignment (`+=` and peers), and simple unary
      operators (`!`, `-`, `~`, prefix `++`/`--`). Added runtime tests for
      operator indexing, compound assignment, and unary negation.
- **2026-11-06 08:49 -08:00** — Added member `operator()` calls with zero
      or one argument, including overload selection and class result types;
      added runtime functor tests.
- **2026-11-06 09:05 -08:00** — Added `tmpfile` and `%p` parsing in
      `fscanf`/`sscanf`; added a temporary-file runtime test.
- **2026-11-06 09:13 -08:00** — Added host `truncate` (syscall 55) plus
      `fsync`/`fdatasync`, with `<unistd.h>` declarations and a runtime test.
- **2026-11-06 09:22 -08:00** — Added free binary operator overloads,
      including class-aware parameter matching and stream insertion for
      custom types; added arithmetic and iostream runtime tests.
- **2026-11-06 09:30 -08:00** — Extended member `operator()` to arbitrary
      argument counts using the normal register argument convention; added a
      two-argument functor test.
- **2026-11-06 10:15 -08:00** — Added member postfix `operator++`/`operator--`
      (`operator++(int)` and peers), with prefix and postfix runtime coverage.
- **2026-11-06 10:23 -08:00** — Added `%n` position capture to
      `fscanf`/`sscanf` and made `fflush(NULL)` flush the standard streams;
      added a `%n` runtime test.
- **2026-11-06 10:32 -08:00** — Added block input buffering through
      `setvbuf`/`setbuf`, with `fgetc`/`fread` refill, seek resets, and buffer
      cleanup; added an input-buffering runtime test.
- **2026-11-06 10:43 -08:00** — Populated `st_mtime` from the host file
      timestamp and added a non-zero timestamp assertion to the stat test.
- **2026-11-06 10:52 -08:00** — Added host `getcwd`/`chdir` (syscalls 56/57),
      a sandbox-relative working directory in path resolution, libc wrappers,
      and a chdir/getcwd runtime test.
- **2026-11-06 11:02 -08:00** — Added `dup`/`dup2` (syscalls 58/59) with
      shared descriptor state, refcounted close, libc wrappers, and a
      shared-offset runtime test.
- **2026-11-06 11:10 -08:00** — Added class-by-value member operator
      parameters for register-sized aggregates; added a runtime test.
- **2026-11-06 11:19 -08:00** — Added iostream base (`hex`/`dec`/`oct`),
      width, and precision state with formatting tests.
- **2026-11-06 19:35 -08:00** — Fixed the C++ parser treating string and
      character literals as punctuators when their text matched (`"!"`, `"+"`,
      etc.); added a regression test for punctuator-looking literals.
- **2026-11-06 19:42 -08:00** — Fixed scalar-by-value `operator[]` results
      being loaded as if they were lvalues, and added a compact `<string>` /
      `std::string` subset with tests.
- **2026-11-06 19:51 -08:00** — User-defined `operator=` is now selected for
      class assignment instead of always emitting a bytewise copy; extended
      the `std::string` subset with assignment and `operator+=`.
- **2026-11-06 20:09 -08:00** — Added functional class temporaries (`A(7)`)
      in expression and return contexts, enabled class-by-value returns from
      those expressions, and added matching `std::string` equality.
- **2026-11-06 20:20 -08:00** — Gave const member functions distinct symbols
      and taught overload resolution to prefer non-const methods on mutable
      objects and const methods on const objects.
- **2026-11-06 20:29 -08:00** — Added class-returning free operator calls
      (sret/register results), class-valued initializer addressing, and
      `std::string operator+`.
- **2026-11-06 20:42 -08:00** — Added C multiple-declarator local
      declarations (`int a = 1, b = 2;`) with a compiler runtime test.
- **2026-11-06 20:52 -08:00** — Extended C multiple declarators to
      pointer declarators (`int *a = ..., *b = ...`).
- **2026-11-06 21:01 -08:00** — Added multiple declarators at global
      scope (`int a = 1, b = 2;`) with a compiler runtime test.
- **2026-11-06 21:09 -08:00** — Added const `std::string::operator[]` and
      extended the string runtime test to const objects.
- **2026-11-06 21:32 -08:00** — Added C global pointer initializers using
      `&global` (`.word` relocations) and covered multi-declarator pointers.
- **2026-11-06 21:41 -08:00** — Added C global pointer initializers from
      string literals (`const char *message = "..."`).
- **2026-11-06 21:51 -08:00** — Added C `typedef` declarator lists
      (`typedef int A, B;`) including pointer/array declarators.
- **2026-11-06 22:01 -08:00** — Stopped using sret/aggregate result
      handling for reference returns, enabling `std::hex`/`std::dec`/`std::oct`
      manipulators and ref-returning free operators.
- **2026-11-06 22:10 -08:00** — Added `std::to_string` overloads and
      `std::string` equality/inequality against C strings.
- **2026-11-06 22:19 -08:00** — Added C `restrict`, `inline`, and `_Atomic`
      qualifier acceptance, including pointer-qualified parameters.
- **2026-11-06 22:28 -08:00** — Loaded scalar reference call results in value
      contexts and added `std::string` `find`/`substr`/`front`/`back`.
- **2026-11-06 22:38 -08:00** — Added `std::string::resize` with growth
      zero-filling and runtime coverage.
- **2026-11-06 22:49 -08:00** — Added C global arrays of string pointers and
      loaded intermediate pointer values for nested subscripts.
- **2026-11-06 23:05 -08:00** — Generalized C global pointer-array
      initializers to `&global` entries.
- **2026-11-07 09:08 -08:00** — Added `std::string::rfind` and
      `std::string::compare`.
- **2026-11-07 09:18 -08:00** — Added `std::string::replace`.
- **2026-11-07 09:27 -08:00** — Added C `<stdint.h>`, `<stdbool.h>`, and
      `<limits.h>` synthetic headers.
- **2026-11-07 09:36 -08:00** — Added `<ctype.h>` plus `isdigit`, `isalpha`,
      `isspace`, `tolower`, `toupper`, and related character classification.
- **2026-11-07 09:46 -08:00** — Added `<assert.h>` with the `assert` macro
      and runtime assertion-failure reporting.
- **2026-11-07 09:56 -08:00** — Added `atoi`, `atol`, `atof`, `strtol`, and
      `strtoul` to `<stdlib.h>`.
- **2026-11-07 10:06 -08:00** — Added `<time.h>` with `time_t`, `time`, and
      `difftime`.
- **2026-11-07 10:17 -08:00** — Added `strspn`, `strcspn`, and `strpbrk`.
- **2026-11-07 10:27 -08:00** — Added `abs`, `labs`, `srand`, and `rand`.
- **2026-11-07 10:38 -08:00** — Added `std::string::push_back`/`pop_back`
      and character `find`/`rfind` overloads.
- **2026-11-07 10:48 -08:00** — Declared `vprintf`, `vsnprintf`, `vsprintf`,
      and `vfprintf` in `<stdio.h>`.
- **2026-11-07 10:59 -08:00** — Fixed the thread test's counter-symbol
      lookup to avoid matching `tmpfile_counter`, then added function-scope C
      `static` locals with persistent data/BSS storage.
- **2026-11-07 11:09 -08:00** — Added `strcoll` and `strxfrm`.
- **2026-11-07 11:19 -08:00** — Extended C static locals to initialized
      arrays and structs.
- **2026-11-07 11:30 -08:00** — Added `memccpy`.
- **2026-11-07 11:41 -08:00** — Added reentrant `strtok_r`.
- **2026-11-07 11:52 -08:00** — Added C function-pointer typedefs, indirect
      calls, pointer parameters, and function-pointer returns.
- **2026-11-07 12:07 -08:00** — Supported global function-pointer
      initializers from function names and `&function`.
- **2026-11-07 12:18 -08:00** — Supported direct function-pointer
      declarators such as `int (*fn)(int, int)` at local and global scope.
- **2026-11-07 12:28 -08:00** — Added callback-based `qsort` to `<stdlib.h>`.
- **2026-11-07 12:40 -08:00** — Added stack-passed fixed arguments beyond the
      fourth and restored `bsearch`.
- **2026-11-07 12:50 -08:00** — Allowed aggregate local initializers from
      struct-returning calls and string literals.
- **2026-11-07 13:01 -08:00** — Supported bytewise struct/union assignment
      and copy initialization.
- **2026-11-07 13:12 -08:00** — Supported calls through function-pointer
      array elements and other postfix expressions.
- **2026-11-07 13:22 -08:00** — Added `std::string::operator+=(const
      std::string&)`; qualifying the self parameter avoids an unqualified-name
      resolution mismatch inside `namespace std`.
- **2026-11-07 13:34 -08:00** — Fixed unqualified self-type member parameters
      inside namespaces by qualifying collected member signatures and matching
      operator overloads through resolved class identity.
- **2026-11-07 13:45 -08:00** — Added `std::string` self overloads for
      `find`, `rfind`, and `compare`.
- **2026-11-07 13:56 -08:00** — Added relational `std::string` operators for
      string and C-string operands.
- **2026-11-07 14:09 -08:00** — Fixed parenthesized relational expressions
      being mistaken for template type-ids after a unary `!`.
- **2026-11-07 14:19 -08:00** — Added `std::string` `operator+` overloads for
      C-string operands in both orders.
- **2026-11-07 14:30 -08:00** — Added `std::string::insert` and `erase`.
- **2026-11-07 14:41 -08:00** — Added `std::string` `starts_with`, `ends_with`,
      `find_first_of`, and `find_last_of`.
- **2026-11-07 14:51 -08:00** — Added C `strnlen` and `strndup`.
- **2026-11-07 15:02 -08:00** — Added `std::string::at` and one-argument
      `substr`.
- **2026-11-07 15:13 -08:00** — Added `std::string::append(const
      std::string&)` and `swap`.
- **2026-11-07 15:23 -08:00** — Treated unary `*` expressions as lvalues in
      `gen_address`, enabling `(*this) += value`.
- **2026-11-07 15:34 -08:00** — Supported overloaded `operator->` for member
      access through class wrappers.
- **2026-11-07 15:45 -08:00** — Added C `__func__`.
- **2026-11-07 15:56 -08:00** — Added C `__LINE__` and `__FILE__`.
- **2026-11-07 16:07 -08:00** — Added `__STDC__`, `__STDC_HOSTED__`,
      `__STDC_VERSION__`, `__DATE__`, and `__TIME__`.
- **2026-11-07 16:23 -08:00** — Added C compound literals and fixed struct/union
      parameter passing; parameters are copied into callee locals to preserve
      by-value semantics.
- **2026-11-07 16:45 -08:00** — Promoted anonymous struct/union members into the
      enclosing type for direct member access.
- **2026-11-07 16:57 -08:00** — Added variadic function-like macros and
      `__VA_ARGS__` expansion.
- **2026-11-07 16:59 -08:00** — Added parenthesized C comma expressions.
- **2026-11-07 17:10 -08:00** — Added C bit-fields with packed layout, masked
      stores, sign extension, and zero-width alignment.
- **2026-11-07 17:21 -08:00** — Added `__builtin_expect`,
      `__builtin_constant_p`, `__builtin_unreachable`, and `__extension__`.
- **2026-11-07 17:32 -08:00** — Added C++ conversion operators, including
      `operator bool` in conditions and explicit scalar casts.
- **2026-11-07 17:44 -08:00** — Extended conversion operators to copy
      initialization and scalar function arguments.
- **2026-11-07 18:52 -08:00** — Extended conversion operators to scalar
      returns and equality comparisons.
- **2026-11-07 19:14 -08:00** — Extended conversion comparisons to relational
      operators and reversed scalar/class operands.
- **2026-11-07 19:27 -08:00** — Extended conversion operators to integer
      arithmetic with class operands on either side.
- **2026-11-07 19:34 -08:00** — Stopped treating unknown `identifier *`/`&`
      inside parentheses as pointer cast types, fixing `(value & mask)` and
      `(value * factor)` expressions.
- **2026-11-07 19:48 -08:00** — Added converting-constructor implicit
      conversions for copy initialization and scalar function arguments.
- **2026-11-07 20:09 -08:00** — Added converting-constructor initialization for
      global and TLS objects.
- **2026-11-07 20:22 -08:00** — Added C++ default arguments for functions and
      member functions.
- **2026-11-07 20:47 -08:00** — Extended default arguments to constructors,
      including global objects and copy-initialized temporaries.
- **2026-11-07 20:50 -08:00** — Added C `#error` and `#warning` directives and
      surfaced diagnostics on successful compiles.
- **2026-11-07 21:03 -08:00** — Added C `__attribute__`/`__declspec`
      skipping in declarations, types, and function declarators.
- **2026-11-07 21:16 -08:00** — Added `std::string::find` overloads with a
      start position for C strings, characters, and strings.
- **2026-11-07 21:29 -08:00** — Added C11 `_Generic` selection by expression
      type, using an unevaluated control expression.
- **2026-11-07 21:40 -08:00** — Added C `typeof`/`__typeof__` for type names,
      arrays, and unevaluated expressions.
- **2026-11-07 21:53 -08:00** — Enforced `explicit` constructors for implicit
      copy initialization and argument conversions.
- **2026-11-07 22:05 -08:00** — Added C-style casts that construct class
      temporaries, including explicit constructors and same-class copies.
- **2026-11-07 22:18 -08:00** — Added `std::string::replace` and `insert`
      overloads accepting another string.
- **2026-11-07 22:30 -08:00** — Extended range-for to classes with pointer
      `begin()`/`end()` methods.
- **2026-11-07 22:43 -08:00** — Range-for now supports reference loop
      variables, permitting in-place element updates.
- **2026-11-07 22:56 -08:00** — Added `std::string` concatenation with
      characters on either side.
- **2026-11-07 23:09 -08:00** — Added start-position overloads for string
      `rfind`, `find_first_of`, and `find_last_of`.
- **2026-11-07 23:21 -08:00** — Added `std::string::npos`.
- **2026-11-07 23:33 -08:00** — Added `std::string::begin`/`end`, enabling
      range-for iteration and reference mutation.
- **2026-11-07 23:46 -08:00** — Added `__attribute__((packed))` struct
      layout with byte alignment and packed member offsets.
- **2026-11-07 23:58 -08:00** — Applied trailing `__attribute__((packed))`
      after a struct body by recomputing member offsets.
- **2026-11-08 00:11 -08:00** — Added `__attribute__((aligned(N)))` struct
      alignment, including combined packed/aligned layouts.
- **2026-11-08 00:23 -08:00** — Added mutable and const
      `std::string::data()`.
- **2026-11-08 00:38 -08:00** — Added C++ `alignas` class alignment and made
      `alignof` report the actual type/class alignment.
- **2026-11-08 00:53 -08:00** — Added `std::string::insert(position, char)`
      and one-argument `erase(position)`.
- **2026-11-08 01:05 -08:00** — Added `std::string::compare` overloads taking
      a substring position and count.
- **2026-11-08 01:18 -08:00** — Added character assignment to
      `std::string`.
- **2026-11-08 01:31 -08:00** — Added `std::string::append(count, char)` and
      `insert(position, count, char)`.
- **2026-11-08 01:45 -08:00** — Added public `std::string::assign` overloads
      for C strings and prefix lengths.
- **2026-11-08 01:58 -08:00** — Added `std::string::find_first_not_of` and
      `find_last_not_of` for sets and characters.
- **2026-11-08 02:11 -08:00** — Added C `_Alignas` declaration alignment,
      including aligned struct members.
- **2026-11-08 02:23 -08:00** — Added `std::string::resize(count, char)` for
      filling newly grown characters.
- **2026-11-08 02:35 -08:00** — Added `std::string::append(const char*, count)`
      for prefix-count appends.
- **2026-11-08 02:48 -08:00** — Added `std::string::insert(position, text,
      count)` for prefix-count inserts.
- **2026-11-08 03:02 -08:00** — Added `std::string::append(string, count)`
      for string prefix-count appends.
- **2026-11-08 03:16 -08:00** — Added stack-passed fixed integer arguments
      for C++ calls beyond four registers, plus four-argument
      `std::string::replace` overloads.
- **2026-11-08 03:28 -08:00** — Added four-argument `std::string::compare`
      overloads with source counts.
- **2026-11-08 03:41 -08:00** — Added deep-copy `std::string` copy assignment
      with self-assignment protection.
- **2026-11-08 03:54 -08:00** — Added five-argument `std::string::compare`
      overloads with source position and count.
- **2026-11-08 04:07 -08:00** — Added start-position overloads for
      `find_first_not_of` and `find_last_not_of`.
- **2026-11-08 04:21 -08:00** — Added C-style class-to-class casts through
      converting constructors.
- **2026-11-08 04:33 -08:00** — Added class copy initialization through
      converting constructors.
- **2026-11-08 04:47 -08:00** — Added converting-constructor class
      arguments in function calls.
- **2026-11-08 05:09 -08:00** — Fixed by-value register-class arguments to
      constructors and covered direct/copy initialization.
- **2026-11-08 05:20 -08:00** — Added `std::string(const char*, count)`
      prefix construction.
- **2026-11-08 05:31 -08:00** — Added class return conversion through
      converting constructors.
- **2026-11-08 05:42 -08:00** — Added class assignment conversion through
      converting constructors.
- **2026-11-08 05:53 -08:00** — Added `std::string::capacity` and a
      `reserve` capacity hint.
- **2026-11-08 06:04 -08:00** — Accepted `_Alignas` after the declaration
      type for globals, locals, and struct members.
- **2026-11-08 06:15 -08:00** — Accepted one-argument `_Static_assert`
      without a message.
- **2026-11-08 06:27 -08:00** — Added the `__COUNTER__` predefined macro
      with per-translation-unit counting.
- **2026-11-08 06:39 -08:00** — Added `__COUNTER__` to the C++ preprocessor.
- **2026-11-08 06:51 -08:00** — Added `__DATE__` and `__TIME__` to the C++
      preprocessor.
- **2026-11-08 07:03 -08:00** — Added `__func__` for C++ functions,
      members, and namespace functions.
- **2026-11-08 07:14 -08:00** — Accepted C++ `#warning` directives without
      stopping compilation.
- **2026-11-08 07:26 -08:00** — Made C `#line` update `__FILE__` as well as
      `__LINE__`.
- **2026-11-08 07:37 -08:00** — Made C++ `#line` update `__LINE__` and
      `__FILE__`.
- **2026-11-08 07:49 -08:00** — Added `std::string::copy` into a caller
      buffer.
- **2026-11-08 08:00 -08:00** — Added character overloads for
      `find_first_of`/`find_last_of`.
- **2026-11-08 08:12 -08:00** — Added `<utility>` with `std::swap` scalar
      overloads.
- **2026-11-08 08:23 -08:00** — Added `<algorithm>` with `std::min` and
      `std::max` scalar overloads.
- **2026-11-08 08:35 -08:00** — Added `<cmath>` with `std::abs`, `std::sqrt`,
      and `std::fabs` overloads.
- **2026-11-08 08:47 -08:00** — Added `<cstdlib>` with `std::atoi` and
      `std::atof`.
- **2026-11-08 08:58 -08:00** — Added `<cstring>` wrappers and stopped
      forcing namespace-scope functions with runtime names to C symbols.
- **2026-11-08 09:10 -08:00** — Added `<cctype>` character classification
      wrappers.
- **2026-11-08 09:22 -08:00** — Added `<cstddef>` with `std::size_t` and
      `std::ptrdiff_t`.
- **2026-11-08 09:34 -08:00** — Added C++ `short`/`unsigned short` with
      two-byte layout and `<cstdint>` fixed-width aliases.
- **2026-11-08 09:46 -08:00** — Added `<climits>` integer limit macros.
- **2026-11-08 09:58 -08:00** — Added `<cfloat>` floating limit macros.
- **2026-11-08 10:10 -08:00** — Extended `<cstring>` with `strncat`, `strrchr`,
      `strtok`, `strdup`, `memmove`, and `memchr`.
- **2026-11-08 10:22 -08:00** — Added `<cstdio>` for formatted output and
      made qualified `using` aliases resolve to function names.
- **2026-11-08 10:34 -08:00** — Added `<cstdlib>` allocation aliases and
      `exit`/`abort`.
- **2026-11-08 10:45 -08:00** — Added `<ctime>` with `std::time_t`, `time`,
      and `difftime`.
- **2026-11-08 10:56 -08:00** — Added `std::errno`, `strerror`, and
      `perror` aliases to `<cerrno>`.
- **2026-11-08 11:07 -08:00** — Added `<cassert>` with an `assert`
      function-like macro.
- **2026-11-08 11:31 -08:00** — Split CTest into per-source-file unit
      entries, split the C and C++ codegen suites into eight buckets each,
      serialized the OS build tests, used the detected CPU count for
      `ctest -j`, and documented the fast/full testing workflow. Full parallel
      suite dropped from ~290 s to ~49 s on Windows and ~12 s on Linux.
- **2026-11-08 11:42 -08:00** — Exposed `std::FILE`, `fopen`/`fclose`,
      `fread`/`fwrite`, `fgets`/`fputs`, and the rest of the C stdio surface
      from `<cstdio>`; the namespace `FILE` alias uses the underlying
      `kr32_file` tag so pointers stay type-compatible with global `FILE*`.
- **2026-11-08 11:49 -08:00** — Added `<cstdlib>` `std::strtol` and
      `std::strtoul` aliases.
- **2026-11-08 11:51 -08:00** — Added `<cstdlib>` `std::labs` alias;
      function-pointer declaration support is still needed for `qsort`/
      `bsearch` aliases.
- **2026-11-08 11:55 -08:00** — Added C++ function-pointer parameter
      declarators, function-to-pointer decay, and function-name typing, then
      exposed `<cstdlib>` `std::qsort`/`std::bsearch`.
- **2026-11-08 11:58 -08:00** — Added indirect calls through function-pointer
      variables, including call return-type inference and integer argument
      staging through `R0`-`R3`.
- **2026-11-08 12:06 -08:00** — Implemented C runtime `strtod` with end
      pointer and exposed it as `std::strtod`.
- **2026-11-08 12:07 -08:00** — Added decimal exponent parsing to
      `strtod`; `atof` now delegates to `strtod`.
- **2026-11-08 12:12 -08:00** — Added 64-bit `strtoll`/`strtoull`, fixed C
      64-bit unary negation (high-word borrow), and fixed C++ implicit
      `int`-to-`long long` return staging.
- **2026-11-08 12:13 -08:00** — Added 64-bit `llabs` runtime function and
      `std::llabs` alias.
- **2026-11-08 12:15 -08:00** — Added `strtof` and `strtold` runtime
      wrappers plus `std::strtof`/`std::strtold` aliases.
- **2026-11-08 12:18 -08:00** — Enabled C `long double` as a double-width
      type; `long double` values, casts, and comparisons now compile.
- **2026-11-08 12:20 -08:00** — Added `std::to_string(long long)` and
      `std::to_string(unsigned long long)`.
- **2026-11-08 12:23 -08:00** — Fixed C++ 64-bit unary minus high-word
      borrow; added a regression test for `-1234567890123LL`.
- **2026-11-08 12:25 -08:00** — Added `<string>` `std::stoi` and
      `std::stol` for C-string input.
- **2026-11-08 12:27 -08:00** — Fixed named-type matching for unqualified
      namespace class types, and added `std::string` overloads of
      `std::stoi`/`std::stol`.
- **2026-11-08 12:30 -08:00** — Added `<string>` `std::stod`, `std::stof`,
      and `std::stold` overloads for C strings and `std::string`.
- **2026-11-08 12:32 -08:00** — Initialized local `char[]` arrays from
      string literals instead of storing the literal pointer.
- **2026-11-08 12:34 -08:00** — Inferred the size of unsized local
      `char[]` arrays from their string-literal initializer.
- **2026-11-08 12:36 -08:00** — Inferred unsized array lengths from
      initializer-list element counts.
- **2026-11-08 12:38 -08:00** — Extended unsized global arrays to infer
      length from string literals and initializer lists, and emit constant
      data instead of runtime pointer stores.
- **2026-11-08 12:40 -08:00** — Allowed temporaries to bind to const
      lvalue-reference parameters; fixes `std::string` temporaries and similar
      API calls.
- **2026-11-08 12:43 -08:00** — Added C++ enum support: unscoped and
      scoped enumerators become integer constants, enum names alias `int`, and
      `Color::Red` works in expressions and switch labels.
- **2026-11-08 12:47 -08:00** — Extended enum constants to namespace and
      class scopes, including `Box::Kind::Value` expression parsing.
- **2026-11-08 12:49 -08:00** — Evaluated enum constants in array bounds;
      `int values[N::Two]` now sizes the array correctly.
- **2026-11-08 12:54 -08:00** — Added `std::string(int count, char)`
      constructor for repeated-character strings.
- **2026-11-08 13:01 -08:00** — Added `std::string::append` overloads that
      take a position and count for `std::string` and C-string sources.
- **2026-11-08 13:03 -08:00** — Added `std::string::assign` overloads for
      whole strings and substrings.
- **2026-11-08 13:04 -08:00** — Added `std::string::insert` overloads for
      inserting string and C-string substrings at a position.
- **2026-11-08 13:06 -08:00** — Added `std::string::replace` overloads that
      take a source position and count.
- **2026-11-08 13:08 -08:00** — Added `std::string::find(const char*,
      position, count)` for counted C-string ranges.
- **2026-11-08 13:10 -08:00** — Added the matching
      `std::string::rfind` counted-range overload.
- **2026-11-08 13:12 -08:00** — Added counted-range overloads for
      `find_first_of`, `find_last_of`, `find_first_not_of`, and
      `find_last_not_of`.
- **2026-11-08 13:15 -08:00** — Fixed initialized scalar function-local
      statics by emitting constant data directly instead of entering the
      guarded-construction path.
- **2026-11-08 13:19 -08:00** — Extended static-local constant data to
      all scalar widths, including floating-point values, and made local
      static symbols unique.
- **2026-11-08 13:21 -08:00** — Aligned function-local static storage and
      kept guarded construction for class/string statics.
- **2026-11-08 13:27 -08:00** — Replaced the local-static guard call with an
      inline LL/SC initialization sequence, fixing class/string statics in
      both the CLI and threaded VM harness.
- **2026-11-08 13:29 -08:00** — Made `std::string::append(const char*)`
      return `*this`, enabling standard append chaining.
- **2026-11-08 13:31 -08:00** — Added `std::string::shrink_to_fit`.
- **2026-11-08 13:33 -08:00** — Implemented real `std::string::reserve` and
      capacity tracking.
- **2026-11-08 13:36 -08:00** — Treated calls returning references as
      lvalues in codegen, enabling reference-returning helper functions in
      assignment expressions.
- **2026-11-08 13:39 -08:00** — Added the `std::string` substring
      constructor `(const string&, position, count)`.
- **2026-11-08 13:41 -08:00** — Added the C-string substring constructor
      `(const char*, position, count)`.
- **2026-11-08 13:47 -08:00** — Added the no-argument `std::string::erase()`
      overload that clears the string.
- **2026-11-08 13:50 -08:00** — Ranked constructor overloads with
      conversion costs instead of declaration order, fixing
      `string(count, char)` versus `string(const char*, int)`; added
      `string::replace(position, count, new_count, char)`.
- **2026-11-08 13:53 -08:00** — Added `std::string::assign(count, char)`.
- **2026-11-08 13:56 -08:00** — Recognized `_Atomic` as a C declaration
      specifier, allowing `_Atomic int` locals (atomic semantics still use
      plain scalar accesses).
- **2026-11-08 13:58 -08:00** — Added C `_Atomic(type)` type-specifier
      syntax.
- **2026-11-08 14:00 -08:00** — Accepted `_Noreturn` as a C function
      specifier.
- **2026-11-08 14:02 -08:00** — Accepted `_Thread_local` as a C storage
      specifier (currently single-thread storage semantics).
- **2026-11-08 14:06 -08:00** — Added const overloads of
      `std::string::front`/`back`.
- **2026-11-08 14:08 -08:00** — Accepted `_Complex` as a C type
      specifier (currently compiled as the corresponding real scalar).
- **2026-11-08 14:11 -08:00** — Accepted `_Imaginary` as a C type
      specifier (currently compiled as the corresponding real scalar).
- **2026-11-08 14:14 -08:00** — Unwrapped reference types in `store_value`,
      fixing character assignments that previously stored a full word and
      overwrote neighboring bytes.
- **2026-11-08 14:17 -08:00** — Added `std::string::contains` for C strings
      and strings.
- **2026-11-08 14:19 -08:00** — Added `std::string` overloads of
      `starts_with` and `ends_with`.
- **2026-11-08 14:21 -08:00** — Added `std::clamp` overloads to
      `<algorithm>`.
- **2026-11-08 14:24 -08:00** — Added `std::sort` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:26 -08:00** — Added `std::reverse` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:28 -08:00** — Added `std::fill` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:31 -08:00** — Added `std::find` for int, char, and double
      arrays.
- **2026-11-08 14:33 -08:00** — Added `std::copy` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:35 -08:00** — Added `std::count` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:37 -08:00** — Added `std::equal` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:40 -08:00** — Added `<numeric>` with `std::accumulate` and
      `std::iota`.
- **2026-11-08 14:43 -08:00** — Added `std::fill_n` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:45 -08:00** — Added `std::copy_n` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:47 -08:00** — Added `std::replace` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:50 -08:00** — Added `std::remove` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 14:52 -08:00** — Fixed pointer-to-pointer subtraction to divide
      the byte difference by the element size and yield `int`.
- **2026-11-08 14:55 -08:00** — Added `std::min_element` and
      `std::max_element`.
- **2026-11-08 14:57 -08:00** — Added `std::binary_search`,
      `std::lower_bound`, and `std::upper_bound`.
- **2026-11-08 15:00 -08:00** — Added `std::unique` for int, char, and double
      arrays.
- **2026-11-08 15:02 -08:00** — Added `std::swap_ranges` for int, char, and
      double arrays.
- **2026-11-08 15:05 -08:00** — Added `std::rotate` for int, char, and double
      arrays.
- **2026-11-08 15:07 -08:00** — Added `std::search` for int, char, and double
      arrays.
- **2026-11-08 15:10 -08:00** — Added `std::adjacent_find` for int, char, and
      double arrays.
- **2026-11-08 15:13 -08:00** — Added `std::search_n` for int, char, and double
      arrays.
- **2026-11-08 15:15 -08:00** — Added `std::find_end` for int, char, and double
      arrays.
- **2026-11-08 15:18 -08:00** — Added the four-argument `std::equal`
      overload with a bounded second range.
- **2026-11-08 15:20 -08:00** — Added `std::is_sorted` for int, char, and
      double arrays.
- **2026-11-08 15:23 -08:00** — Added `std::is_sorted_until` for int, char,
      and double arrays.
- **2026-11-08 15:26 -08:00** — Added `std::find_first_of` for int, char, and
      double arrays.
- **2026-11-08 15:28 -08:00** — Added `std::lexicographical_compare` for int,
      char, and double arrays.
- **2026-11-08 15:31 -08:00** — Added `std::includes` for int, char, and double
      arrays.
- **2026-11-08 15:34 -08:00** — Added `std::set_union` for int, char, and
      double arrays.
- **2026-11-08 15:37 -08:00** — Added `std::set_intersection` for int, char,
      and double arrays.
- **2026-11-08 15:40 -08:00** — Added `std::set_difference` for int, char, and
      double arrays.
- **2026-11-08 15:43 -08:00** — Added `std::set_symmetric_difference` for int,
      char, and double arrays.
- **2026-11-08 15:46 -08:00** — Added `std::merge` for int, char, and double
      arrays.
- **2026-11-08 15:49 -08:00** — Added `std::inplace_merge` for int, char, and
      double arrays.
- **2026-11-08 15:52 -08:00** — Added `std::stable_sort` for int, unsigned int,
      char, float, and double arrays.
- **2026-11-08 15:55 -08:00** — Added `std::nth_element` and
      `std::partial_sort` for int, char, and double arrays.
- **2026-11-08 15:57 -08:00** — Added `std::reverse_copy` for int, char, and
      double arrays.
- **2026-11-08 16:01 -08:00** — Added `std::rotate_copy` for int, char, and
      double arrays.
- **2026-11-08 16:04 -08:00** — Added `std::replace_copy` for int, char, and
      double arrays.
- **2026-11-08 16:07 -08:00** — Added `std::unique_copy` for int, char, and
      double arrays.
- **2026-11-08 16:10 -08:00** — Added `std::remove_copy` for int, char, and
      double arrays.
- **2026-11-08 16:13 -08:00** — Added `std::copy_backward` for int, char, and
      double arrays.
- **2026-11-08 16:16 -08:00** — Added `std::iter_swap` for int, char, and
      double pointers.
- **2026-11-08 16:20 -08:00** — Added `std::next_permutation` for int, char,
      and double arrays.
- **2026-11-08 16:23 -08:00** — Added `std::prev_permutation` for int, char,
      and double arrays.
- **2026-11-08 16:26 -08:00** — Added `std::is_permutation` for int, char, and
      double arrays.
- **2026-11-08 16:29 -08:00** — Added `std::is_heap` for int, char, and double
      arrays.
- **2026-11-08 16:33 -08:00** — Added `std::is_heap_until` for int, char, and
      double arrays.
- **2026-11-08 16:36 -08:00** — Added `std::make_heap` for int, char, and
      double arrays.
- **2026-11-08 16:40 -08:00** — Added `std::pop_heap` and `std::sort_heap` for
      int, char, and double arrays.
- **2026-11-08 16:43 -08:00** — Added `std::push_heap` for int, char, and
      double arrays.
- **2026-11-08 16:46 -08:00** — Added `std::min_element`/`max_element`
      overloads for unsigned int and float arrays.
- **2026-11-08 17:40 -08:00** — Added predicate algorithms
      (`all_of`/`any_of`/`none_of`, `find_if`/`find_if_not`, `count_if`,
      `for_each`, `transform`, `remove_if`, `replace_if`) and comparator
      `sort`; inferred lambda return types from return expressions; stopped
      the matrix `transform` intrinsic from hijacking `std::transform`.
- **2026-11-08 17:44 -08:00** — Propagated function-template type bindings
      through `resolve_type` during code generation, so body locals declared
      with `T*` use the substituted element size; generic sorts now work for
      `char` and `double`.
- **2026-11-08 17:49 -08:00** — Considered function-template candidates even
      when non-template overloads exist, preferring non-templates on ties;
      made comparator `sort` a generic template.
- **2026-11-08 17:58 -08:00** — Fixed `is_declaration_start` to index the
      token lookahead relative to the current position, enabling qualified
      two-argument class templates; propagated class-template bindings into
      member signatures; unwrapped reference types in assignments and FP
      conversions. Added `std::pair` and `<array>` (`std::array<T, N>`).
- **2026-11-08 18:12 -08:00** — Added `<atomic>` with `std::atomic<T>`
      loads, stores, exchange, fetch-add, CAS, assignment, and conversion
      using the existing LL/SC atomic intrinsics.
- **2026-11-08 18:19 -08:00** — Added C atomic builtins
      (`__atomic_load_n`, `__atomic_store_n`, `__atomic_fetch_add`,
      `__atomic_compare_exchange_n`) and `<stdatomic.h>`; guarded the heap
      allocator with an LL/SC spinlock.
- **2026-11-08 18:23 -08:00** — Added `<vector>` with a minimal
      `std::vector<T>` (growth, reserve, element access, iteration, and
      clear) including vectors of `std::pair`.
- **2026-11-08 18:30 -08:00** — Added `<optional>` and `<string_view>`;
      `std::optional<T>` supports `has_value`, `value`, `value_or`, `set`,
      and `reset`, and `std::string_view` supports construction, slicing,
      prefix/suffix removal, and comparison.
- **2026-11-08 18:35 -08:00** — Implemented class `operator*` in unary
      expressions and address generation, so dereferencing class objects
      loads through the returned reference and supports assignment.
- **2026-11-08 18:39 -08:00** — Added `std::vector::insert` and `erase`.
- **2026-11-08 18:49 -08:00** — Indirect calls now pass floating-point
      arguments in F registers; lambda return inference tracks parameter
      types and arithmetic, enabling double-returning transform lambdas and
      comparator sort on doubles.
- **2026-11-08 18:53 -08:00** — Added template value initialization `T()`
      for scalar and class bindings, including default construction and
      fallback zeroing.
- **2026-11-08 18:58 -08:00** — Implemented C `_Thread_local` globals in
      the `.tls` section with per-thread runtime copies and TLS-relative
      address generation.
- **2026-11-08 19:04 -08:00** — Implemented C `_Atomic` declarations for
      integer, character, and short objects with atomic loads, stores, and
      fences.
- **2026-11-08 19:08 -08:00** — Added LL/SC read-modify-write for atomic
      increments, decrements, and integer compound assignments.
- **2026-11-08 19:12 -08:00** — Added `std::vector::resize` using separate
      declaration and assignment, which works for class template elements
      including `std::pair`.
- **2026-11-08 19:17 -08:00** — Refreshed the standard-library documentation,
      added a C++ limits section, and named the callee in unknown or
      non-matching call diagnostics.
- **2026-11-08 19:21 -08:00** — Added `std::optional` assignment from `T`,
      `emplace`, and `operator->`.
- **2026-11-08 19:25 -08:00** — Added `std::string_view::starts_with` and
      `ends_with` for string-view and character arguments.
- **2026-11-08 19:30 -08:00** — Resolved injected class names in function
      parameter types, enabling `std::vector` copy construction and copy
      assignment for scalar and class elements.
- **2026-11-08 19:35 -08:00** — Added a minimal linear-search `std::map`;
      member operator calls now bind scalar temporaries to const references.
- **2026-11-08 19:40 -08:00** — General method calls now bind scalar
      temporaries, including floating-point values, to const references.
- **2026-11-08 19:44 -08:00** — Preserved const/volatile qualifiers when
      substituting template parameters, so `const T&` methods accept
      temporaries and `std::map::count` uses its standard signature.
- **2026-11-08 19:48 -08:00** — Allowed constructor conversions for class
      `const&` parameters and constructed class temporaries at call sites;
      methods can now accept string literals as `const std::string&`.
- **2026-11-08 20:17 -08:00** — Added shared `-O1` assembly peephole
      optimization: frame-slot load forwarding, immediate compare/ALU
      folding, dead/overwritten move removal, and redundant jump removal.
      The full C/C++ suite passes with optimization forced.
- **2026-11-08 20:32 -08:00** — Added ordinary-expression constant folding
      in both C and C++ plus C++ pruning of `if (0)`, `while (0)`, and
      `for (; 0;)`. Both unoptimized and optimized suites pass.
- **2026-11-08 20:44 -08:00** — Added immediate arithmetic/comparison
      codegen in both C and C++, C `if (0)`/`while (0)` pruning, and
      unreachable statement removal after jumps and returns.
- **2026-11-08 20:45 -08:00** — Added dead frame-store elimination to the
      `-O1` pass for slots that are never loaded or address-taken.
- **2026-11-08 20:49 -08:00** — Attempted repeated-load forwarding; reverted
      after a vector regression. Store-based frame forwarding remains.
- **2026-11-08 20:51 -08:00** — Added C++ immediate comparison codegen,
      matching C and avoiding operand spills for `value == constant`.
- **2026-11-08 20:58 -08:00** — Folded `LEA [R7+n]` plus `LOAD [reg]` into a
      direct frame load in `-O1`.
- **2026-11-08 21:05 -08:00** — C++ scalar local reads now emit direct frame
      loads at `-O0`; direct `STORE` frame addresses fold in `-O1`, but
      removing the `LEA` was reverted after a control-flow regression.
- **2026-11-08 21:17 -08:00** — C scalar local reads now emit direct frame
      loads as well, including char, short, long long, and floating types.
- **2026-11-08 21:18 -08:00** — C and C++ scalar local assignments now store
      directly to their frame slot instead of through a temporary address.
- **2026-11-08 21:25 -08:00** — Scalar declaration initializers now store
      directly to their frame slot in both C and C++.
- **2026-11-08 21:31 -08:00** — C++ scalar returns skip the return-value
      temporary when no destructors or active catch can clobber the registers.
- **2026-11-08 21:38 -08:00** — C++ `if` and loop comparisons now branch
      directly from `CMP`, removing boolean materialization and re-testing.
- **2026-11-08 21:44 -08:00** — Added `-O1` boolean-branch folding so C
      comparisons branch directly after materializing `0`/`1`.
- **2026-11-08 21:50 -08:00** — C `if` and `while` comparisons with immediate
      right operands branch directly at `-O0`.
- **2026-11-08 21:56 -08:00** — C comparisons with register right operands
      also branch directly at `-O0`.
- **2026-11-08 22:07 -08:00** — C and C++ `do`/`while` comparisons branch
      directly to the loop head.
- **2026-11-08 22:13 -08:00** — C and C++ ternary conditions branch directly
      on comparisons instead of materializing a boolean.
- **2026-11-08 22:19 -08:00** — C and C++ arithmetic on two simple locals
      keeps the left operand in `R1` instead of spilling it to a frame slot.
- **2026-11-08 22:28 -08:00** — C now uses the register-resident shortcut only
      when the right operand is a complete higher-precedence expression.
- **2026-11-08 22:34 -08:00** — Local `++`/`--` operations load and store the
      frame slot directly in both C and C++.
- **2026-11-08 22:40 -08:00** — Local compound assignments load, combine, and
      store the frame slot directly in both compilers.
- **2026-11-08 22:49 -08:00** — C++ calls with simple local or constant
      arguments load them directly into the argument registers.
- **2026-11-08 22:58 -08:00** — C calls with simple local or constant
      arguments do the same, skipping argument temporaries.
- **2026-11-08 23:06 -08:00** — C++ zero-argument member calls keep `this`
      in `R0` instead of spilling it to a temporary.
- **2026-11-08 23:12 -08:00** — C++ member calls with up to three simple
      arguments keep `this` in `R4` and load arguments directly.
- **2026-11-08 23:18 -08:00** — `-O1` folds frame-address `ADD`/`SUB`
      adjustments into the preceding `LEA` and supports offset consumers.
- **2026-11-08 23:25 -08:00** — `-O1` removes zero `ADJSP` adjustments from
      functions with no locals; an unsafe pointer-address fold was reverted.
- **2026-11-08 23:30 -08:00** — Restored pointer offset folding for loads only,
      guarded against flag-dependent conditional branches.
- **2026-11-08 23:36 -08:00** — Added safe pointer store-offset folding when
      the base register is immediately overwritten.
- **2026-11-08 23:43 -08:00** — C++ `-O1` inlines free functions whose body is
      a single return expression, including `heightOf` and `maximum`.
- **2026-11-08 23:49 -08:00** — Trivial member and static-member functions are
      inlined under `-O1` as well.
- **2026-11-08 23:55 -08:00** — C++ reads of constant scalar locals with
      constant initializers use immediates instead of reloading the slot.
- **2026-11-09 00:01 -08:00** — C reads of constant scalar locals propagate
      immediates as well.
- **2026-11-09 00:06 -08:00** — `-O1` removes instructions after unconditional
      transfers until the next label or directive.
- **2026-11-09 00:12 -08:00** — `-O1` removes repeated same-register frame
      loads within a store-free, call-free region.
- **2026-11-09 00:17 -08:00** — Repeated-load tracking carries across
      conditional branches on the fallthrough path.
- **2026-11-09 00:24 -08:00** — Ordered comparison values materialize with
      `SLT`/`SLTU` instead of branches and `0`/`1` moves.
- **2026-11-09 00:30 -08:00** — C and C++ codegen emit `SLT`/`SLTU` directly
      for ordered comparison values at `-O0`.
- **2026-11-09 00:36 -08:00** — Equality values use XOR plus `SLT #1` in both
      compilers, eliminating branch materialization.
- **2026-11-09 00:56 -08:00** — `-O1` folds `LEA symbol` plus indirect loads and
      qualifying stores into direct symbol memory operands.
- **2026-11-09 01:01 -08:00** — `-O1` combines adjacent immediate `ADD`/`SUB`
      chains guarded against flag-dependent branches.
- **2026-11-09 01:12 -08:00** — Local-vs-local comparison conditions load
      both operands into registers directly in C and C++.
- **2026-11-09 01:18 -08:00** — Logical `!` compiles to `SLTU #1`, including
      both halves of `long long`.
- **2026-11-09 01:25 -08:00** — C++ local-vs-local comparison values avoid
      temporary slots; equality against zero skips `XOR #0`.
- **2026-11-09 01:35 -08:00** — C local-vs-local comparison values use direct
      loads and `SLT`/`SLTU` without stack temporaries.
- **2026-11-09 01:42 -08:00** — `-O1` retargets loads across a following copy
      when the source register is immediately overwritten.
- **2026-11-09 05:56 -08:00** — Rebuilt KR32OS BIOS and kernel with the current
      compiler. The OS build now defaults to `-O1`; the rebuilt kernel is
      30,944 bytes and the scripted boot/init/fault/wx smoke test exits 0.
- **2026-11-09 06:03 -08:00** — Refreshed the checked-in embedded OS images from
      the rebuilt kernels and relinked `kr32.exe`; embedded build id is
      `bios-7d36f445-kernel-47a4f13a`.
- **2026-11-09 06:27 -08:00** — Started the P0-P7 optimization program. P0 adds
      CFG and register/frame-slot liveness; P1 adds liveness dead-store/move
      elimination, block-local copy coalescing, and leaf scalar promotion to
      `R12`-`R15`. Full unoptimized suites pass 80/80 on Windows and Linux.
- **2026-11-09 06:32 -08:00** — P2 folds `SLT`/equality materialization plus
      `CMP #0` and a conditional branch back into a direct `CMP`/branch,
      removing two instructions from common loop conditions.
- **2026-11-09 06:40 -08:00** — P3 extends C++ inlining to straight-line
      assignment/increment prefixes followed by `return`; multi-statement
      inline assembly and behavior tests pass.
- **2026-11-09 06:46 -08:00** — P3 also folds C calls to constant/global
      return-only functions. P4 folds adjacent C tail calls into a frame
      restore plus `JMP`.
- **2026-11-09 06:57 -08:00** — P5 removes unreachable local functions and
      their debug records. P6 strength-reduces immediate multiply, divide, and
      modulo by powers of two. Full suites remain 80/80 on all four configs.
- **2026-11-09 07:00 -08:00** — P5 also parses `noexcept` and omits call-site
      unwind labels, landing pads, and LSDA entries for direct noexcept calls.
      P7 benchmark/metrics documentation is in place.
- **2026-11-09 07:04 -08:00** — Completed the P0-P7 optimization program.
      Final validation is 80/80 on Windows and Linux, unoptimized and
      optimized; benchmark results are recorded in `docs/optimizations.md`.
- **2026-11-09 08:07 -08:00** — Rebuilt KR32OS from the P0-P7 compiler and
      refreshed both embedded image sets. Embedded Windows build id is
      `bios-7d36f445-kernel-64521ce0`; OS integration tests pass 8/8.
- **2026-11-09 08:36 -08:00** — Fixed redundant `JMP` removal to look through
      consecutive labels. This removes the jump-to-next-jump sequence reported
      in `shell.c`; rebuilt kernel images are 81,800 bytes (Windows) and
      81,836 bytes (Linux). Embedded id is
      `bios-7d36f445-kernel-51cdcb45`.
- **2026-11-09 08:58 -08:00** — Added CFG frame-load availability and
      PUSH/POP pair folding, and made frame slot zero parse as `[R7]`.
      Rebuilt kernels are 81,064 bytes (Windows) and 81,100 bytes (Linux);
      embedded id is `bios-7d36f445-kernel-e2891091`.
- **2026-11-09 09:19 -08:00** — Debugger SOURCE filename is now a picker for
      every source path in the symbols. Selecting a file pins it for
      breakpoint work; stepping, running, or restarting returns to the file
      containing the new PC.
- **2026-11-09 09:47 -08:00** — Added the BREAKPOINTS panel between Variables
      and Threads/Call Stack with address, basename, line, gutter toggle, and
      scrollbar. Breakpoints now have an enabled/disabled state (red/yellow),
      and RST preserves both addresses and state.
- **2026-11-09 12:00 -08:00** — Unified gutter behavior: clicking an empty
      gutter sets an enabled breakpoint, clicking a breakpoint toggles
      enabled/disabled, and the context menu sets or removes it. F9 follows
      the same toggle rule.
- **2026-11-09 12:31 -08:00** — Refactored KR32OS layout and MMIO access around
      `page.h`, `memory_map.h`, `user_memory.h`, `mmio.h`, `syscall.h`,
      `trap.h`, and `errno.h`. Serial/timer/IRQ registers now use volatile
      structs; BIOS and assembly constants use `.equ`; syscall/initrd/build
      helpers use named ABI constants. Rebuilt and re-embedded both OS images.
- **2026-11-09 12:54 -08:00** — Fixed assembly debug line label collisions:
      the object assembler now prefixes `.Lsource_` labels with the source
      hash so `traps-asm.S`, `user-asm.S`, and `boot/entry.S` line records
      resolve to their own addresses after linking. Rebuilt and re-embedded
      OS images; embedded id is `bios-f7f3930a-kernel-065dce79`.
- **2026-11-09 13:13 -08:00** — Recompiled all KR32 and KR32OS sources,
      regenerated symbols, and re-embedded both images. Fixed source-pane OUT:
      it now stops at the caller instruction following CALL instead of
      advancing an extra source line. Added a called-function step-out
      regression test.
- **2026-11-09 13:39 -08:00** — Scrollbar dragging now maps the mouse delta
      through the thumb travel to the scroll range, so the thumb tracks the
      cursor instead of moving in 14-pixel jumps. Track paging is clamped to
      the current range. Added a drag-mapping regression test.

- **2026-11-12 15:41 -08:00** — Display/graphics D0 review closed: open
      questions resolved in `docs/display.md` §12 and `docs/graphics.md` §13.
      Added M10 (framebuffer v2, host surface, console, debugger screen) and
      M11 (graphics API, blocked on guest header synthesis). Recorded CR-1: the
      image loader must zero-fill `.bss`, verified at D1. Implementation not
      started.

- **2026-11-12 15:54 -08:00** — Console cells are two bytes: a character plus a
      4-bit foreground and 4-bit background index into a 16-entry ANSI palette
      (`con_palette` in `console.c`). Per-character colour replaces the earlier
      global fg/bg decision; `con_set_colors` becomes a pen, and the ring costs
      51.2 KiB instead of 25.6 KiB. Recorded in `docs/display.md` §7.2 and
      `docs/graphics.md` §6; D3 and its test gate updated.

- **2026-11-12 18:50 -08:00** — CR-1 implemented. One shared loader,
      `container::load_into(core::Machine&, const Image&, LoadedImage&,
      std::string&)`, now loads allocated sections, captures `.tls` as a
      template, and bulk-zeroes `.bss` through `Machine::zero_memory`. The three
      duplicated loops in `cli.cpp`, `threads.cpp`, and `debug.cpp` are gone;
      `--flat` output is untouched. `unit_test_container` gained four cases
      (dirty then zero, reload, `.tls` captured not written, non-fitting `.bss`
      rejected). Windows gate: 80/80 passed, 632.64 s. Behaviour change to note:
      the debugger's `Session::load` now skips `.tls` instead of loading it as a
      data section, matching the CLI and cluster loaders; it never installed TLS
      anyway.

- **2026-11-12 19:29 -08:00** — M10 D1 complete. Framebuffer v2 is the device
      contract: pending registers with computed `MODE_OK`, sticky
      `FLIP_ERROR`, `FLIP` committing the surface that was `BACK`, `FRAME`, and
      the `ID` word compared as a 32-bit value. Control-page writes never fault;
      unknown offsets read zero. Added `KeyboardDevice` (64-event FIFO, packed
      words, IRQ, overrun W1C) on page 6 and reserved page 7 for the pointer.
      `unit_test_devices` grew from 3 to 29 cases, including an assembly guest
      that sets a mode, stores a pixel, and flips; it runs with paging enabled
      because the MMU rejects device addresses when paging is off. Windows gate:
      80/80 passed, 592.28 s.

- **2026-11-12 19:44 -08:00** — M10 D2 complete. `Window::present_surface`
      uploads an XRGB surface with integer zoom and the texture blend mode set
      to none, so the zero X byte does not make the frame transparent. Added
      `kr32 display` (image or `--os`), which presents on `FRAME` change, quits
      on `Esc`, cycles zoom on `F2`, keeps the window after a halt, and injects
      packed key words including key-up. `--screen-shot` writes the committed
      frame as a P6 PPM. Standalone display guests get a flat identity map,
      because a plainly loaded executable cannot reach MMIO with paging off.
      Added `examples/color-bars.asm` (320x200, eight bands) and the
      `display_screen_shot` CTest, which captures twice and requires identical
      bytes; the window was captured and shows the eight bands. Windows gate:
      81/81 passed, 564.89 s.

- **2026-11-12 20:17 -08:00** — M10 D3 implemented. The kernel now renders its
      own console: `tools/font8x16` extracts the embedded JetBrains Mono face,
      rasterizes an 8x16 table, and commits `kr32os/kernel/font8x16.c`;
      `fb.{h,c}` probes the ID word, sets 800x600 at offset 0, and flips;
      `console.c` keeps a 100x37 grid and a 256-line ring of two-byte cells
      (character plus 4-bit foreground/background palette indices), redraws
      dirty rectangles, draws an inverse cursor, scrolls, and mirrors serial
      output unconditionally. `console_getc` polls the keyboard FIFO first, then
      serial. The BIOS now maps eight MMIO pages so page 6 (keyboard) is
      reachable. The display command's OS path also writes `BootInfo`; without
      it the kernel rejected the framebuffer and never left 1920x1080.

      Boot cost is the open problem: a full-screen redraw is 473,600 pixel
      stores and the kernel front end emits about 14 instructions per store, so
      boot takes roughly 7M steps and a scroll costs about the same. OS test
      budgets were raised to 12M steps (24M for the fault test), and the
      fault test's 100-echo timer input was reduced to one `mem` query because
      boot now advances the timer on its own. Windows gate: 81/81 passed,
      604.86 s. The checked-in Linux OS images were not regenerated.

- **2026-11-12 20:32 -08:00** — M10 D4 implemented. The debugger's top-left
      region is now a 26-pixel tab strip plus a pane: `SOURCE` draws the source
      view, `SCREEN` caches the committed frame keyed on `frame().number` and
      blits it integer-scaled or nearest-neighbour fit, letterboxed, with a
      focus dot. Clicking the screen focuses it and points the memory panel at
      the pixel's guest address; hover reports `(x, y)`, the address, and the
      colour. `Esc` releases focus, and `app.cpp` routes keys to the keyboard
      device while focused except Escape and F5/F6/F9-F11. OS mode selects the
      tab by default, and `ui_headless_os` renders it headlessly. One existing
      `test_ui` case moved two click rows down by the strip; its assertions are
      unchanged. Windows gate: 82/82 passed, 611.91 s. The console redraw cost
      from D3 is still the milestone's open performance item.

- **2026-11-12 20:47 -08:00** — Documentation migrated to the shipped contract.
      `devices.md` now carries the v2 framebuffer register table, the flip
      semantics and reset state, and the keyboard page; its "being revised"
      banner is gone and the stale "no host presentation path" limits are
      replaced. `display.md` is marked implemented, its title loses "(proposal)",
      and §5 defers to `devices.md` as the normative copy. `ui.md` lists the tab
      strip and SCREEN pane; `testing.md` documents `display_screen_shot`,
      `ui_headless_os`, and the raised OS step budgets.

      Added `os_console_screen`: it boots the embedded OS under `kr32 display`,
      requires an 800x600 P6 frame, and asserts two runs are byte-identical.
      Windows gate: 83/83 passed, 768.42 s. Still open on the milestone: the WSL
      run, Linux OS images, cell-level colour assertions, the redraw
      optimisation, and the `m10` tag.

- **2026-11-12 21:07 -08:00** — WSL gate run headless. Cloned the tree to
      `~/kr32-m10` (ext4, not `/mnt/c`), configured with Unix Makefiles and
      `-DKR32_BUILD_UI=OFF`, built, and passed **76/76** Linux tests. WSL has no
      Ninja, `pkg-config`, or SDL2 and `sudo` needs an interactive password, so
      the seven UI tests did not run there.

      The Linux build caught a portability bug the MSVC build hid: unused
      helpers in `tests/cxx/cxx_test_support.h` and
      `tests/unit/cc_test_support.h` trip GCC's `-Wunused-function` under
      `-Werror`. Marked them `[[maybe_unused]]`. Regenerated the Linux OS images
      with `scripts/update-os.sh` (`kernel.k32` 131,924 bytes) and copied them
      into the tree. Windows gate re-run after the fix: 83/83 passed, 714.02 s.

- **2026-11-12 21:33 -08:00** — M10 gate closed. With `ninja`, `pkg-config`, and
      `libsdl2-dev` installed in WSL, the Linux tree configured with
      `linux-gcc`, built with the UI, and passed **83/83** (`SDL_VIDEODRIVER=dummy`,
      371.43 s). GCC also caught an unparenthesised arithmetic-or in
      `app.cpp`'s flat page map; parenthesised, and the Windows gate re-ran
      **83/83** (902.92 s). Both platforms are green with the UI.

      Deferred with reasons: kernel console cell assertions for a mid-line
      colour change and scroll colours (no host harness; the screenshot test
      covers steady-state colour), and the console redraw optimisation (boot is
      ~7M steps; budgets absorb it and the gate does not depend on it).
      Milestone `m10` tagged and pushed.

- **2026-11-12 21:55 -08:00** — Fixed the variables panel dropping inner array
      dimensions: `grid_chars` (`char[37][100]`) rendered as `grid_chars[37]`
      and only revealed `[100]` on expansion. Both front ends record array
      types innermost-first (`int[3][2]` for `int m[2][3]`), and the single
      `[count]` suffix used the last bracket. `array_dimensions_display`
      reverses the bracket groups for the row label and the hover tooltip;
      `unit_test_ui` covers 2-D, 1-D, 3-D, and non-array types.

- **2026-11-12 22:39 -08:00** — Fixed source-line step-over stopping inside a
      long callee. `Session::step_over` skips a CALL by running until the
      return address, but its guard was one million instructions; the console's
      `fill_surface` runs about seven million, so stepping over
      `fill_surface(...)` stopped mid-loop at `fill_surface+0x50` instead of the
      next line. The guard is now `kStepGuard` (20M) at all four step sites, and
      `unit_test_debug` covers a call that runs 1.6M instructions. Reload the
      image in the debugger after rebuilding the kernel so its addresses match.

- **2026-11-12 23:00 -08:00** — Folded constant narrowing conversions in the C
      compiler. `convert_value` always emitted the `AND`/`SHL`/`SAR` sequence for
      int-to-char (and short/bool) conversions, so `console_putc('\n')` compiled
      to `MOV R0, #10; AND #255; SHL #24; SAR #24; CALL`. When the value being
      converted is the literal currently in `R0`, `take_literal_in_r0` consumes
      it and the conversion becomes one `MOV` with the truncated, sign-extended
      value: `MOV R0, #10; CALL`. Non-constant conversions keep the mask because
      the callee relies on the normalised argument, and char lvalues already used
      `LOAD8S`/`LOAD8U`/`STORE8`. The kernel image shrank 131,072 → 130,840 bytes;
      the embedded images were regenerated.

- **2026-11-12 23:19 -08:00** — Simplified the console fill loop (author's
      change). The four-way unrolled body expanded to about 44 instructions per
      pixel through the kernel front end; the plain pointer loop emits about 19,
      measured from the generated `console.s`. The embedded kernel image was
      already built from this source, so it is unchanged at 130,840 bytes.
      Booting the embedded image and running `exit` takes 8,933,156 ticks.

- **2026-11-12 23:37 -08:00** — Added `kr32os/kernel/fast.{h,S}`: hand-written
      `fast_WordFill` and `fast_ByteFill` for paths where the instruction count
      matters. Both take the C ABI's first three arguments in R0-R2, are leaves,
      and use only R0-R4. `fast_ByteFill` replicates the byte into a word, writes
      `cnt/4` words, then finishes the remaining 0-3 bytes with `STORE8`.
      `console.c`'s `fill_surface` now calls `fast_WordFill`, and both build
      scripts assemble and link `fast.o`. Boot plus `exit` dropped from 8,933,156
      to 3,173,163 ticks; the embedded images were regenerated (kernel.k32
      132,736 bytes). `unit_test_fast` assembles `fast.S`, links it with a C
      driver, runs it, and checks word order, byte replication, the 0-3 byte tail,
      and zero-length calls.

- **2026-11-12 23:51 -08:00** — Console init now clears both grids with
      `fast_ByteFill` instead of the nested loop (author's change), and the count
      was corrected from the uninitialised `row * columns` to
      `rows * columns`: the generated code had been loading a stack slot nothing
      wrote, so the fill ranged from a no-op to an out-of-bounds write. The
      unused `column` local, a trailing space, and an array-address cast were
      removed at the same time, and `fast.h` states the 4-byte alignment
      precondition. Boot plus `exit` is 3,032,182 ticks; the embedded image is
      regenerated.

- **2026-11-13 00:21 -08:00** — `fast_ByteFill` now accepts an unaligned
      buffer: it emits byte stores for the 0-3 bytes up to the next word
      boundary, then the word loop, then the tail. `fast.h` drops the alignment
      precondition for the byte fill (`fast_WordFill` still requires it), and
      `unit_test_fast` covers `buffer + 1` (6 bytes) and `buffer + 3` (9 bytes),
      checking the bytes on both sides stay untouched. The embedded image is
      regenerated (132,752 bytes); the OS and integration tests pass against it.

- **2026-11-13 05:36 -08:00** — C++ `unsigned char` implemented end to end.
      Added `BuiltinKind::UnsignedChar`; the parser accepts `unsigned char` in
      either order; `uint8_t` aliases it; `is_unsigned_integer`, `promote`,
      size, type name, and mangling (`h` from the Itanium table) know it. Byte
      loads go through `byte_load_mnemonic` (`LOAD8U` for unsigned, `LOAD8S`
      otherwise) and stores through `STORE8`; casts mask to 255 for unsigned and
      sign-extend for plain char. `test_cxx_codegen_1` covers the value,
      comparisons, overflow, `++`, parameters, arrays, casts, `sizeof`, and
      globals. The unit-bucket watchdog is now 600 s: `test_cxx_codegen_2`
      takes about 214 s alone and crossed the old 300 s limit under `-j4`.

- **2026-11-13 07:53 -08:00** — Recorded that `signed char` mangles as `c` on
      purpose: the parser ignores `signed`, so `char` and `signed char` are one
      type here, unlike the standard where they are distinct and Itanium uses
      `a`. Left as is; the note sits with the `UnsignedChar` entry so the next
      reader does not treat it as an oversight.

- **2026-11-13 09:55 -08:00** — Approved the host terminal service and recorded
      the decisions in [docs/terminal.md](docs/terminal.md) and a new M10.5
      milestone: 10x20 cells (80x30 grid), one host renderer shared by the
      terminal and a graphics command device, the terminal as a host-side client
      drawing last with an explicit hide/show, immediate-register graphics
      commands first, no escape sequences in v1, scrollback internal. M11 is now
      also blocked on M10.5.

      Prototyped the font sizes with a `--sample` mode added to
      `tools/font8x16`: three 800x600 renders at 8x16 (100x37), 10x20 (80x30),
      and 12x24 (66x25). The default table output is byte-identical to the
      committed `font8x16.c`, so the generator change is safe. The host console
      itself remains unchanged; the earlier view-panning prototype was reverted.

- **2026-11-13 16:50 -08:00** — **M10.5 complete, tagged `m10.5`.** The kernel
      console is replaced by the host terminal device (page 8), and host-executed
      graphics commands landed on page 9 (`SET_CLIP`, `SET_COLOR`, `FILL_RECT`,
      `COPY_RECT`, `DRAW_LINE`, `DRAW_TEXT`, `FLIP`). The host owns the 80x30
      grid, the 10x20 JetBrains Mono atlas, the palette, and a 256-line
      scrollback ring; `console.c` is a thin driver and `font8x16.c` is deleted.
      The BIOS maps MMIO pages 8 and 9, and both build scripts dropped the font
      object.

      Two defects surfaced while bringing it up: `COMMAND` reset did not
      re-derive the grid from the active mode, so the terminal stayed 192x54 and
      rows past 30 were written below the 600-pixel surface (the screen appeared
      frozen at 30 lines); and hiding the terminal left the cursor block painted.
      The graphics device also defaulted to an empty clip, so drawing before
      `SET_CLIP` did nothing; an unset clip now means the full surface.

      Tests: `tests/unit/test_terminal.cpp` (6 cases) and
      `tests/unit/test_graphics.cpp` (8 cases); `unit_test_devices` moved its
      unclaimed-page probe from page 9 to page 10. Gates: Windows 86/86, WSL
      86/86. `kr32os/user/init.c` prints 32 lines during boot to exercise the
      scroll path.

      Deviation: there is no separate `Renderer` class. With two clients (the
      terminal and the graphics device) the primitives and clip live in
      `GraphicsDevice`, and the terminal owns the text/palette path.

- **2026-11-13 20:17 -08:00** — M11 in progress. The five guest headers are
      injected by both front ends from one shared list, with a warning (C) or
      the existing error (C++) on an unknown header. The graphics API runs on
      the KR32OS syscall path: `sys_gfx` (70) validates a 60-byte command block
      and submits it, 71 allocates from the VRAM arena, 72/73 poll the keyboard,
      and 74 controls the console. The user image links a freestanding
      `libc/gfx.c` with `gfx_*`, `con_*`, and `input_*`.

      Bring-up defects: the flat user image is mapped RX, so the library's
      `.bss` pool was read-only (the last image page is now RWX, documented as a
      limitation); the kernel skipped `SET_SURFACE` for offset zero, which is
      the screen, so `gfx_present` copied rows back into the demo surface; and
      presenting per row cost about 600 syscalls, replaced by one
      `COPY_RECT_STRIDED` plus a flip.

      `kr32os/user/gfxdemo.c` hides the console, draws a scene, and presents it;
      `os_gfx_demo` boots the OS, runs it through the shell, and checks three
      pixels of the screenshot; `gfxcheck` adds a clip-confined fill and a line
      with five pixel checks. Windows and WSL gates pass 89/89 each and the
      milestone is tagged `m11`.

- **2026-11-13 21:35 -08:00** — Code-size work started in both compilers. Two
      optimizer passes now fold addresses: a `LEA` plus a constant `ADD` becomes
      one `LEA` with a displacement, and a `MOV` that copies an address is
      retargeted into the `LEA`/`ADD` that produced it when the source register
      is dead afterwards. The C++ front end now pushes the lvalue address of a
      simple scalar assignment (as the C front end does) instead of spilling it
      to a frame slot, so the same passes apply.

      Measured on the `__kr32_stdio_init` shape: 91 instructions at `-O0`, 50 at
      `-O1` (was about 74), with each member store down from five instructions to
      `LEA R1, [object + n]` / `MOV R0, #v` / `STORE R0, [R1]`. Both front ends
      produce the same form. All 42 unit suites and the 47 integration/OS tests
      pass.

      Next: reuse one base pointer across consecutive stores to the same object
      (`STORE R0, [R1 + n]`) and look at the remaining `MOV`/`NEG` constant
      pairs.

- **2026-11-13 21:50 -08:00** — The address work continues. A `LEA` plus a
      memory operation through that register now becomes an absolute-address
      operation (`MOV R0, #0 ; STORE R0, [stdin_object + 4]`), so the register
      is not needed at all; the pass skips a register that is still live at a
      `RET` or a branch target. Repeated base loads also collapse to one `LEA`
      plus displacements when the absolute form does not apply, and a
      `MOV`/`NEG` pair folds to a negative immediate.

      Measured on the real `__kr32_stdio_init` in `libc.c`: 91 instructions at
      `-O0`, 36 at `-O1` (74 before this work). The whole libc object drops from
      451,376 to 425,212 bytes. Both front ends emit the same shape. All 89
      tests pass on Windows (42 unit, 47 integration/OS).

- **2026-11-13 22:18 -08:00** — 32-bit expression temporaries now take one stack
      slot instead of an eight-byte long long slot. A narrow value is widened
      only when the other operand is wider, so `p->x + p->y` and `s += array[i]`
      no longer emit the sign-extend and double store/load pair. The libc object
      drops to 422,532 bytes (451,376 at `-O0`); the pattern sample from 127
      instructions at `-O0` to 96.

      Also corrected: `docs/abi.md` claimed the C compiler uses only `R0-R7`,
      but the 64-bit multiply and shift expansions and helpers such as
      `__kr32_ulldiv` use `R12-R15` as caller-saved scratch. The gfx and console
      integration tests now take the `kr32os_build` resource lock, because they
      rebuild the shared OS tree and collided under `ctest -j`.

- **2026-11-13 22:26 -08:00** — Compound assignment on a plain local no longer
      materialises the lvalue address: it reads the frame slot, combines, and
      writes the slot back (`s += array[i]` is now `LOAD R0,[R7+4]` ...
      `STORE R0,[R7+4]` with no `LEA`/`PUSH`/`POP` address). The generic path
      pushes the address instead of spilling it to a slot, so the optimizer can
      fold it. `sum_array` in the pattern sample drops from 40 to 32
      instructions. All 89 Windows tests pass.

- **2026-11-13 22:37 -08:00** — A one-slot stack temporary now stays in a
      register when the span between its push and pop touches neither SP nor a
      free scratch register and that register is dead afterwards. `sum_array`
      drops from 32 to 30 instructions, the libc object from 422,532 to 420,708
      bytes, and the kernel binary at `-O1` from 72,876 to 70,176 bytes (85,032
      at `-O0`, 17.5% saved). The check uses the CFG liveness analysis, not just
      a linear scan. All 89 Windows tests pass.

- **2026-11-13 22:49 -08:00** — `RET` no longer counts as reading `R1`. The
      compilers emit `RETW` for a 64-bit return while optimizing, and the
      optimizer maps it back to `RET` before assembling, so only functions that
      actually return a wide value keep `R1` live. This unlocks the register
      folds at the end of every other function. The libc object drops from
      420,708 to 420,500 bytes and the kernel at `-O1` from 70,176 to 69,920.
      All 89 Windows tests pass.

      A copy-into-consumer pass was prototyped and dropped: with the wide-return
      change in place it changed no instruction in the sample.

- **2026-11-13 23:23 -08:00** — `srand` was eight instructions and a stack frame;
      it is now `STORE R0, [rand_state] ; MOV R0, #0 ; RET`. Three changes
      together: the address folds run again after the push/pop and address
      folds expose their final shape; the absolute-address pass uses the CFG
      liveness instead of stopping at a return label; and the frame prologue
      pass now recognises the real `PUSH R7 ; ADJSP #-N ; LEA R7, [SP]` order
      and removes the released frame too. A dead-frame-load pass drops loads
      whose register is overwritten first.

      A `double` expression no longer pays two spurious `FCVT.W.D` truncations:
      the logical-operator parser only converts an operand when `&&` or `||`
      actually follows.

      libc object 420,240 -> 418,920 bytes; kernel at `-O1` 69,740 -> 69,252.
      All 89 Windows tests pass.

- **2026-11-13 23:43 -08:00** — C++ compound assignment no longer keeps two
      frame temporaries. It pushes the lvalue address and the old value, so the
      optimizer folds the pushes into moves: `counter += v` is now
      `LEA R0, [counter] ; PUSH R0 ; LOAD R0, [R0] ; LOAD R1, [R7+0] ; ADD ... ;
      POP R1 ; STORE R0, [R1]` instead of six frame accesses. Verified by
      executing a C++ program that uses `+=`, `*=`, `-=`, `<<=` and `|=`.
      All 89 Windows tests pass.

      A pass that tried to keep the pushed lvalue address in a scratch register
      was prototyped and dropped: the address register is consumed by its own
      load and the value operand occupies `R1`, so the rewrite cannot beat the
      push/pop without real register allocation.

- **2026-11-13 23:56 -08:00** — Code-generation campaign closed. Both front ends
      now fold member offsets and lvalue addresses into the addressing mode and
      emit addresses directly in the store register (`LOAD R0, [R0 + 4]`,
      `STORE R0, [rand_state]`, `STORE R0, [stdin_object + 4]`). Supporting
      passes: absolute-address stores and loads, base reuse with displacements,
      one 32-bit stack slot per temporary, stack temporaries forwarded to
      `R2-R6`, dead frame loads and stores, unused-frame removal (including the
      `ADJSP` pair), `RETW` so only wide returns keep `R1` live, and pushed
      addresses and old values for compound assignment in both compilers.

      Measured against `-O0` of the same tree: libc object 448,096 -> 418,920
      bytes; kernel binary 85,032 -> 69,252 bytes (-18.5%); the pattern sample
      123 -> 86 instructions (-30%). Windows 89/89 and WSL 89/89 tests pass.

- **2026-11-14 07:42 -08:00** — Two code-quality fixes found by reading `memchr`
      at `-O1`. A truncation to `char` or `short` is no longer emitted before a
      narrowing store (`AND #255 ; SHL #24 ; SAR #24 ; STORE8` is now just
      `STORE8`, because the store keeps only those bits). And a `MOV` is folded
      into its first reader, so `MOV R0, R13 ; MOV R2, R0 ; MOV R0, R12 ;
      ADD R0, R2, R0` becomes `ADD R0, R13, R12`. The copy fold stops when the
      source register is clobbered before the reader.

      libc object 418,920 -> 417,596 bytes; kernel at `-O1` 69,252 -> 68,696.
      Windows 89/89 and WSL 89/89 tests pass.

- **2026-11-14 08:14 -08:00** — `promote_leaf_scalars` now promotes a frame slot
      that is loaded only once, provided it is loaded at least as often as it is
      stored, and it no longer requires the load to fold into its consumer. With
      the frame left unused, the prologue pass removes it. `add_fields` is five
      instructions with no frame; `set_fields` keeps its three parameters in
      `R12-R14` and drops the frame. libc object 417,596 -> 417,132 bytes;
      kernel at `-O1` 68,696 -> 68,280. Windows 89/89 and WSL 89/89 pass.

- **2026-11-14 08:30 -08:00** — A void function no longer clears `R0` on the way
      out, in either compiler. The value is not part of the calling convention,
      so `void f(void) { }` is now a single `RET` and every void function loses
      one instruction. libc object 417,132 -> 417,060 bytes; kernel at `-O1`
      68,280 -> 67,560. Windows 89/89 and WSL 89/89 pass.

- **2026-11-14 08:47 -08:00** — Two address passes closed the last visible copy
      in a global increment. When a `MOV` copies an address that the very next
      instruction reads and clobbers, the copy moves into the producer and the
      consumer, so the move disappears. And a `LEA Rd, [symbol]` whose register
      is read only as a memory address is replaced by naming the symbol at every
      use, so the register and the `LEA` go away. `++counter` is now
      `LOAD R0, [counter] ; ADD R0, R0, #1 ; STORE R0, [counter]`.
      libc object 417,060 -> 417,056 bytes; kernel at `-O1` 67,560 -> 67,528.
      Windows 89/89 and WSL 89/89 pass.

- **2026-11-14 09:35 -08:00** — The gfx pixel tests now run for 25,000,000
      steps instead of 6,000,000, and their programs stay on the final frame
      with the console hidden instead of exiting into the shell prompt, which
      repaints the top-left of the screen. The old timing depended on how fast
      the program ran, so an optimization that shortened it could turn the
      capture into a picture of the terminal. Each test takes about 54 seconds
      now; the integration suite about four minutes. Windows 89/89 and WSL 89/89
      pass.

      The dead `MOV R0` removal that exposed this is still reverted: it is
      correct on every site inspected, but with it the two gfx tests still lose
      the frame after the capture change, and the cause is not yet isolated.

- **2026-11-14 10:13 -08:00** — A `sys_halt` syscall (75) halts the machine with
      the current frame on screen. The gfx fixtures call it after presenting, so
      the pixel tests capture a frozen frame instead of racing the shell: they
      pass in about 16 seconds each and no longer depend on how fast the program
      runs. With that, dead `MOV R0` instructions are removed by the optimizer
      again (destinations other than R7 and R11), which drops 125 instructions
      from the libc. libc object 417,056 -> 416,556 bytes; kernel at `-O1`
      67,528 -> 67,372. Windows 89/89 pass.

- **2026-11-14 11:44 -08:00** — Call arguments are loaded straight into their
      argument registers. The simple-argument path used to stage every value
      through `R0` and then move it into place, so `heap_align(total, 8)` cost
      `MOV R0, #8 ; MOV R1, R0 ; LOAD R0, [R7+4]`; it is now
      `MOV R1, #8 ; LOAD R0, [R7+4]`. libc drops to 11,783 instructions and
      416,500 bytes. Windows 89/89 pass.

      The machine crashed during this run and left corrupted build artifacts;
      a clean rebuild restored the suite (an MSVC internal compiler error on the
      first attempt did not reproduce).

- **2026-11-14 12:25 -08:00** — Calls now carry their integer argument count
      (`CALL f, #2`, emitted only when optimizing), so the optimizer knows a
      one-argument call reads `R0` alone instead of assuming `R0-R3`. That makes
      the liveness analysis precise at calls and lets the copy folds work through
      them: `MOV R1, R4` in the libc drops from 88 to 3, and the libc loses 22
      instructions (11,783 -> 11,761; object 416,500 -> 416,412). The count is
      stripped before assembly and the tail-call fold accepts it.

      The kernel binary reading of 67,432 bytes was taken from a stale artifact;
      compiling the kernel sources with and without the counts shows the change
      removes 17 instructions (4,509 -> 4,492), so the kernel shrinks too.
      Windows 89/89 and WSL 89/89 pass.

- **2026-11-14 12:41 -08:00** — The C++ front end now passes an argument count
      for its `memcpy` and `memset` calls (three arguments), so the optimizer
      stops treating `R3` as live across them; the other C++ call sites remain
      conservative. A call-count fold test was added. All 89 Windows tests pass.

- **2026-11-14 13:37 -08:00** — Liveness was over-approximated: a block's use
      set was the union of every read in the block, including reads that happen
      *after* a definition in the same block. The block equations need the
      upward-exposed reads only, so `live_in` claimed registers that the block
      itself defines. Fixed, the libc drops 419 instructions (11,761 -> 11,342;
      object 416,412 -> 414,948) and the kernel 588 bytes (67,432 -> 66,844);
      `MOV R1, R2` disappears entirely.

      That exposed a real miscompile, caught by the gfx pixel tests: once the
      frame-base `LEA` was folded away, `remove_dead_frame_stores` no longer saw
      the block as escaped and deleted every field store of a command block. The
      pass now keeps a store to any slot at or after an escaped slot, and its
      coarse per-group pre-pass (which ignored escapes) is gone; the CFG pass
      handles the same stores with escape information. Windows 89/89 and WSL
      89/89 pass.

- **2026-11-14 13:57 -08:00** — A third copy-fold pass runs after the frame
      clean-up. The copy of a promoted parameter (`MOV R2, R0`) is created early
      but only becomes foldable once the frame accesses around it are gone, so
      the two earlier calls never saw it. libc 11,342 -> 11,213 instructions
      (object 414,948 -> 414,432 bytes), kernel 66,844 -> 66,588. `MOV R2, R0`
      drops from 180 to 62. Windows 89/89 and WSL 89/89 pass.

- **2026-11-14 15:15 -08:00** — A batch of five functional changes, committed
      together under the new cadence rule in `AGENTS.local.md` (five changes or
      180 lines of code, focused tests in the loop, full regression before the
      commit). The C++ simple-call path places literal and plain-integer
      arguments directly in their argument registers and accepts `nullptr` as a
      literal zero; `fold_address_moves` retargets a `LOAD` into a copy's
      destination; and a new `fold_literal_copies` turns
      `MOV Rd, #imm ; MOV Re, Rd` into `MOV Re, #imm`. libc 11,213 -> 11,059
      instructions (object 414,432 -> 413,816 bytes); the C++ pattern sample
      249 -> 245.

      Tried and reverted: loading a *pointer* local directly into its argument
      register. A pointer local can live in a register, so reading its frame
      slot returns stale data; eight `<cstring>` and algorithm tests caught it.

- **2026-11-14 16:36 -08:00** — First batch against `examples/libcpp.cpp` as
      the C++ benchmark. Five changes: `if`/`while` conditions with `&&`/`||`
      branch directly instead of building a 0/1 value and re-testing it;
      `long long` constants carry their known high word (`MOV R1, #0` instead of
      `MOV R1, R0 ; SAR R1, R1, #31`); an array base waits on the stack so the
      index can be evaluated without spilling it to a frame slot; and two
      optimizer fixes — `remove_forwarded_loads` and `remove_repeated_frame_loads`
      both failed to invalidate a copy when a plain `LOAD` clobbered the
      register, which the array change exposed as a miscompile in
      `std::vector::back`.

      libcpp 13,046 -> 11,851 instructions (-9.2%); `.Llogicend` sites 60 -> 6,
      `.Landfalse` 39 -> 5. libc unchanged at 11,059. Windows unit suites (run
      one file at a time), 47/47 integration, and 89/89 WSL pass.

- **2026-11-14 17:59 -08:00** — Second C++ batch. `long long` literals carry
      their real high word (`value >> 32`, not `0`/`-1`), and a comparison
      against a signed literal compares the two 32-bit halves directly, with a
      single high-word test for zero. Calls to a function whose body holds no
      throw, try, new, or call skip the landing pads (143 -> 94 sites) and the
      exception-pending checks after constructors and destructors (36 -> 17).
      The flag iterates to a fixpoint and a constructor also checks its bases
      and class-type members, so an implicit member construction cannot hide a
      throw.

      libcpp 11,825 -> 11,579 instructions. libc unchanged. All unit suites run
      one file at a time, 47/47 integration, and 89/89 WSL pass.

- **2026-11-14 18:28 -08:00** — Third C++ batch, committed at three changes
      rather than five to keep the tree green before a long pause. The throw
      scan now resolves direct calls by name (namespace lookup, then the owning
      class), so a function that only calls non-throwing functions is non-throwing
      too. Integer and `long long` binary operations keep the left operand on the
      stack instead of a frame slot, which lets the optimizer forward it.

      libcpp 11,579 -> 11,442 instructions (-1.2%); frame loads 1,336 -> 1,148,
      frame stores 971 -> 794. All unit suites, 47/47 integration, and 89/89 WSL
      pass.

- **2026-11-14 18:56 -08:00** — Floating-point expression values stop
      round-tripping through frame slots. A binary operation keeps the right
      value in `F0` and reloads only the left into `F1`, and the compound
      assignment does the same with its address on the stack. `FLOAD` 674 -> 521,
      `FSTORE` 467 -> 314, libcpp 11,442 -> 11,114 instructions (-2.9%). All
      unit suites, 47/47 integration, and 89/89 WSL pass.

- **2026-11-14 19:32 -08:00** — `STORE Rx, [R7 + slot]` immediately followed by
      `LOAD Rx, [R7 + slot]` is a memory round trip that leaves `Rx` unchanged.
      The CFG forwarding pass skips functions with exception paths, so these
      survived in most C++ output; a local adjacency pass removes them safely
      (labels may sit between). libcpp 11,114 -> 11,057 instructions. All unit
      suites, 47/47 integration, and 89/89 WSL pass.

- **2026-11-14 20:17 -08:00** — Two changes. The push/pop fold runs once more in
      the clean-up round, because earlier folds turn nested pairs into
      `PUSH Rx ; POP Rx` after its first pass; libcpp 11,057 -> 10,902. Long long
      comparisons in `if`/`while` conditions branch directly instead of building
      0/1 and testing it; 10,902 -> 10,867. The new branch table was wrong twice
      and a new regression test caught both: the ISA treats carry as *no borrow*,
      so `JCS` means unsigned `>=` and `JNC` means `<` - the round-4 literal
      comparison table had the same pair swapped, and is fixed here too.

      The test covers all six operators, negative values, equal high words, and
      literal boundaries. All unit suites, 47/47 integration, and 89/89 WSL pass.

- **2026-11-14 20:50 -08:00** — A `long long` declaration initialised from an
      integer literal now writes the literal's high word instead of sign
      extending through `SAR`, and the high word is stored from `R1` rather than
      copied to `R2` first. libcpp 10,867 -> 10,865 instructions; the gains are
      small because the remaining copies are live data movement. All unit suites,
      47/47 integration, and 89/89 WSL pass.

- **2026-11-14 21:42 -08:00** — The clean-up round repeats more of the fold
      sequence after the frame passes: move coalescing, a forwarded-load pass,
      symbol address folding, address moves, dead moves, and the frame
      load/store removers. Folding exposes work the earlier passes could not
      see. libcpp 10,865 -> 10,834 instructions and libc 11,059 -> 10,966. All
      unit suites, 47/47 integration, and 89/89 WSL pass.

- **2026-11-14 22:11 -08:00** — The CFG forwarding pass now serves a frame load
      from the register a preceding store in the same block left there, instead
      of only deleting loads whose destination already matched. The store then
      becomes dead and the dead-store pass removes it as well. libc 10,966 ->
      10,826 instructions (frame loads 1,618 -> 1,512, stores 943 -> 905) and
      libcpp 10,834 -> 10,751 (1,081 -> 1,037, 774 -> 753). All unit suites,
      47/47 integration, and 89/89 WSL pass.

- **2026-11-15 03:09 -08:00** — Optimization campaign closed for both compilers.
      Final `-O1` numbers against the `-O0` baseline: C library 16,332 -> 10,826
      instructions and 447,752 -> 412,884 bytes (33.7% and 7.8% smaller); the OS
      kernel 85,032 -> 64,960 bytes (23.6%); the C++ `examples/libcpp.cpp`
      13,046 -> 10,751 instructions (17.6%). The shared optimizer carries the
      bulk of the wins: upward-exposed liveness, absolute addressing, symbol
      bases, frame promotion and forwarding, copy and literal folding, identity
      and round-trip removal, stack-based operands, and a throw analysis that
      drops exception bookkeeping for bodies that cannot throw.

      What remains is a different class of work, recorded here so it is not
      re-discovered: expression values still go through a frame slot or stack
      word (about 2,400 accesses in libc, 1,790 in libcpp), the promoted
      registers are limited to `R12`-`R15` and require a call-free body, and
      the last 91 exception landing sites need type inference in the throw
      scan. A block-local register allocator with deferred stores is the next
      project; it should pay more in C, which carries 23.4% frame traffic
      against C++ at 17.1%.

- **2026-11-15 03:58 -08:00** — The suite is split into eleven labelled parts,
      each measured under three minutes (`part01` 131 s down to `part09` 16 s).
      `test_cxx_codegen_2.cpp` was the blocker at 329 s, so it is split into
      three files of 51, 50, and 50 cases (80 s, 110 s, 104 s); ctest registers
      one entry per file, so nothing else changed. The assignment lives in
      `KR32_TEST_PART_ROWS` at the end of `tests/CMakeLists.txt` and is
      documented in `docs/testing.md`. All eleven parts pass, and WSL passes
      91/91 (the two new files included).

- **2026-11-15 04:30 -08:00** — Callee-saved promotion prototype, not landed.
      Promoting frequently loaded slots to `R8`-`R10` in functions that call
      out (91% of the remaining frame loads live in those functions) gave libc
      10,826 -> 10,445 and libcpp 10,751 -> 10,684 with frame loads 1,512 ->
      786, and the save/restore was structurally sound: every promoted function
      saved in a 16-byte aligned area and restored on every exit.

      It was reverted because `os_gfx_check` and `os_gfx_demo` fail
      deterministically with it: the console stays visible, which means
      `gfx_create` returned no surface before `con_hide`. Every subset tried
      passes on its own - user-side gfx functions only, kernel-side gfx only,
      both together, only `gfx_create`, and all non-gfx functions - while the
      full set fails, so the trigger is not a single miscompiled function and
      is not yet explained. Understanding it is the first task next round; the
      approach is sound and the win is real, but it is not shippable until the
      gfx failure is traced.

- **2026-11-15 04:53 -08:00** — Root cause of the callee-saved promotion failure,
      found by tracing the syscalls of the failing image against a passing one:
      the promoted `sys_dispatch` loses its `MOV R9, R0` / `MOV R0, R9` pair and
      the tail-called handler receives a garbage first argument, so `gfx_create`
      returns no surface. The pair is deleted by `remove_dead_moves`, which
      treats a copy into an argument register before a **tail-call
      `JMP <symbol>`** as dead. That is a pre-existing optimizer miscompile, not
      a fault of the promotion: the jump target reads `R0`-`R3`, but the block
      has no successor so they are not live. A standalone snippet reproduces it.

      The intended fix is to mark `R0`-`R3` live at a named-target `JMP` in
      `instruction_uses_defs`; it was attempted but misplaced twice and
      reverted, so the tree is at `9a850b3` with the bug still present and the
      promotion still unlanded. Fix the liveness bug first, add the snippet as
      a regression test, then retry the promotion - it measured libc
      10,826 -> 10,445 and libcpp 10,751 -> 10,684 before the regression.

- **2026-11-15 05:11 -08:00** — Fixed the tail-call liveness bug. A `JMP` to a
      named target leaves the function and the target reads `R0`-`R3`, but the
      block has no successor, so those registers were not live-out and
      `remove_dead_moves` deleted a copy into them as dead. `instruction_uses_defs`
      now marks `R0`-`R3` used at a named-target `JMP`. This is a correctness
      fix, not a size win: libc grows 10,826 -> 10,843 instructions because the
      seventeen argument copies it was deleting are needed. A regression test
      covers the shape, and all eleven parts plus 91/91 WSL pass.

- **2026-11-15 05:29 -08:00** — Callee-saved promotion landed. Slots loaded at
      least three times in a function that calls out are cached in `R8`-`R10`,
      saved in a 16-byte area that keeps SP aligned, and restored before the
      first ADJSP of every frame release (including the multi-step tail-call
      epilogue that broke the first attempt). libc 10,843 -> 10,463 instructions
      and libcpp 10,751 -> 10,684; libc frame loads fall to 786. All eleven
      parts (91 tests) and 91/91 WSL pass, including both gfx tests that failed
      before the tail-call liveness fix.

- **2026-11-15 05:48 -08:00** — `CMP Rd, #0` before `JZ`/`JNZ` is redundant when
      the preceding instruction is an ALU operation that wrote the same register:
      both set `Z` from the result. Only Z branches are handled because `CMP`
      sets carry differently from the ALU operations. libc 10,463 -> 10,364
      instructions and libcpp 10,684 -> 10,664. All eleven parts (91 tests) and
      91/91 WSL pass.

- **2026-11-15 07:09 -08:00** — The C front end stages 64-bit values through the
      stack as `ADJSP #-8 ; STORE Ra, [SP] ; STORE Rb, [SP + 4]` and reads them
      back with a matching pair. Rewriting that shape as `PUSH`/`POP` keeps the
      discipline and lets the forwarding passes collapse it to moves. Only spans
      with no intervening stack adjustment are rewritten. libc 10,364 -> 10,205
      instructions (159 saved); libcpp has no such sites. All eleven parts
      (91 tests) and 91/91 WSL pass.

- **2026-11-15 07:09 -08:00** — Remaining-opportunity analysis after the
      allocator landed. Current state: libc 10,205 instructions with 1,492 frame
      accesses, 687 register moves, 510 ADJSP; libcpp 10,664 with 1,559 frame
      accesses, 998 moves, 835 floating-point memory operations.

      The deferred-store half of the block cache is count-neutral: a store that
      dies in its block is already removed by the dead-store pass, and a store
      that is live-out has to be written anyway, so the cache trades memory
      traffic for register pressure without changing the instruction count. The
      two remaining levers are not optimizer changes:

      1. **Floating-point register move.** `F8`-`F15` are callee-saved, but with
         no FP move instruction a cached FP value cannot reach an operand
         register without a memory round trip. Adding `FMOV Fd, Fs` would let
         argument staging become one move and let hot FP slots be cached. This
         is an ISA decision.
      2. **Global register allocation.** The 998 `libcpp` moves and 1,559 frame
         accesses are values live across clobbers and calls; the call-crossing
         cache already takes all three callee-saved registers. Reducing them
         further needs function-wide allocation with live-range splitting, not a
         block-local pass.

- **2026-11-15 07:34 -08:00** — A leaf function may clobber the caller-saved
      scratch registers, so the leaf promotion now draws from `R2`-`R6` as well
      as `R12`-`R15`, skipping any the function already uses. More slots fit,
      and the freed frame accesses fold into the operations. libc 10,205 ->
      10,142 instructions and libcpp 10,664 -> 10,576. All eleven parts
      (91 tests) and 91/91 WSL pass.

- **2026-11-15 08:22 -08:00** — `FMOV Fd, Fs` added to ISA v1 (FP escape
      function `0x032`), an FPR-to-FPR copy of all 64 bits: assembler and object
      assembler, disassembler, emulator, unit tests for encoding and execution,
      and `docs/isa-v1.md` with the instruction row, the semantics note, a golden
      vector (`FMOV F3, F5` = `0xC06A0032`), and a new revision-history section
      reporting draft 0.2 -> 0.3. Goldens for `FMUL`/`FMV` were wrong in my first
      draft of the test: the encoder places `fd` at bit 21 and `fs1` at bit 17, so
      the assembler's `0xC06A0032` is authoritative. All eleven parts (91 tests)
      and 91/91 WSL pass.

- **2026-11-15 09:04 -08:00** — First use of `FMOV`: FP arguments whose target
      register is `F2`-`F7` are placed with one move instead of a frame slot,
      when no later argument calls anything that could clobber them. It does not
      fire on `examples/libcpp.cpp` - the measured FP traffic there is `F0`/`F1`
      local-variable and temporary traffic, with only two loads reaching `F2` and
      none beyond - so `libcpp` stays at 10,576.

      Caching hot FP slots in `F8`-`F15` through the promotion machinery was
      implemented and measured: 16 `FMOV` replaced 6 `FLOAD`s but the save and
      restore added more, for libcpp 10,576 -> 10,594 and libc 10,142 -> 10,147.
      It was reverted. The cache only pays once an `FMOV Fd, Fk` can fold into
      its consumer (`FADD F0, F1, F2` becoming `FADD F0, F8, F2`), which needs an
      FP-aware copy fold that does not exist yet. All eleven parts (91 tests) and
      91/91 WSL pass.

- **2026-11-15 09:33 -08:00** — Call-free live-range allocation measured and
      reverted. The pass allocates a frame slot to an otherwise unused register
      for the span between its first and last access when that span contains no
      call, and lets two non-overlapping spans share a register. It is a large
      win: libc 10,142 -> 9,742 and libcpp 10,576 -> 10,340 with the span free
      of branches and calls; restricting spans to straight-line code gives
      libc 10,002 and libcpp 10,496 and makes `run_os_user` pass, but
      `os_gfx_check` still fails.

      What is ruled out: the multi-term frame offsets (`[R7 + 152 + 8]`) are
      rejected by `parse_frame_slot`; R0 and R1 are not in the pool; a slot's
      first access being a store while another path reaches a load without it is
      excluded by the straight-line rule; and the frame removal in the failing
      `main` is consistent (save area `ADJSP #-16` paired with
      `LOAD R8, [SP + 0]; ADJSP #16; RET`). The remaining fault is a specific
      transform in the gfx path and needs a per-function bisect.

- **2026-11-15 10:08 -08:00** — Call-free live-range allocation landed with two
      restrictions found by bisecting `os_gfx_check`. Per-function whitelists
      showed that no single gfx module breaks: `main`, both halves of the user
      gfx library, and both halves of the kernel gfx code each pass alone, so the
      fault is not a bad transform in one function. Restricting the register pool
      to `R2`-`R6` makes both `os_gfx_check` and `run_os_user` pass, which
      implicates `R12`-`R15`; why the OS depends on them is not yet known and is
      the next thing to establish. Spans are also restricted to straight-line
      code, and a function whose entire frame would move to registers is left
      alone. libc 10,142 -> 10,061 instructions and libcpp 10,576 -> 10,515;
      kernel 64,248 -> 62,704 bytes. All eleven parts (91 tests) and 91/91 WSL
      pass.

- **2026-11-15 10:36 -08:00** — Floating-point slots join the call-free
      allocation, in `F2`-`F7`: those are caller-saved, so a call-free span
      needs no save and restore, and the front end's expression temporaries use
      `F0`/`F1`. A new fold substitutes the cached register into the consumer of
      an `FMOV` whose destination is named nowhere else, which is what makes the
      cache pay. libcpp 10,515 -> 10,443 instructions with 303 `FMOV` and FP
      loads 521 -> 369; libc 10,061 -> 10,059. All eleven parts (91 tests) and
      91/91 WSL pass. The first `FMOV` in the assembly is now emitted by the
      optimizer rather than the front end.

- **2026-11-15 10:46 -08:00** — Narrowed the `R12`-`R15` question. Giving the
      call-free allocation the full pool (`R2`-`R6` plus `R12`-`R15`) breaks
      `os_gfx_check`; restricting the extra registers to functions with **no
      call at all** passes both OS gfx tests and `run_os_user`. So the fault is
      specific to `R12`-`R15` in a function that calls, not to those registers or
      to the pass. The extension gains nothing, though: leaf functions already
      have their slots promoted into `R12`-`R15` by the leaf pass, so libc and
      libcpp come out identical (10,059 / 10,443). The pool therefore stays
      `R2`-`R6`, and the source comment now says what is actually known.
      Lowering the call-crossing threshold from three loads to two was also
      measured and is worse (10,071 / 10,463), so it stays at three.

- **2026-11-15 11:44 -08:00** — Removed the whole-frame guards from the call-free
      allocation. They were added on the theory that the frame removal could not
      cope with a function whose frame vanished entirely; that theory was wrong,
      and the frame removal handles the save area correctly. Dropping the guards
      promotes functions of which every slot moves to a register: libc 10,059 ->
      10,029 instructions and libcpp 10,443 -> 10,438; the object is 409,696
      bytes and the kernel 62,696.

      Two optimizer unit tests had to change and it is worth being precise about
      why: they asserted that a frame load survives after its register is
      clobbered, which was the right assertion before the slot was promoted. The
      promoted form is `MOV R2, R0 ; LOAD R0, [R0] ; MOV R0, R2`, which restores
      the *stored* value rather than the clobbered one, so the tests now assert
      the memory form is gone and the value comes back from R2. Both gates pass
      after rebuilding *both* targets — an earlier Windows pass in this round
      was against a stale `kr32_tests.exe` because only `kr32` had been rebuilt.

- **2026-11-15 11:44 -08:00** — Cross-call splitting measured before building it.
      Of the frame accesses left after the passes, 60 slots in libc and 49 in
      libcpp have four or more accesses inside one call-free interval and
      therefore cross a call. Splitting one costs a store and a reload per
      crossing, so it pays only from the third access in an interval, and those
      intervals typically hold four or five: the whole-corpus gain is a few
      dozen instructions at best, against a change that would touch every
      function's epilogue. Not built.

      Final state of the two levers: libc 16,332 -> 10,029 instructions
      (-38.6%) and 447,752 -> 409,696 bytes; libcpp 13,046 -> 10,438 (-20.0%);
      OS kernel 85,032 -> 62,696 bytes (-26.3%). Floating-point memory
      operations in libcpp fall 835 -> 527, with 307 FMOV emitted, and
      `FMOV Fd, Fs` is ISA v1 function 0x032 documented in `docs/isa-v1.md` §6.2
      with its revision history at §14.

- **2026-11-15 12:58 -08:00** — Both front ends keep a floating-point binary
      expression's left operand in a scratch register instead of staging it
      through the stack. `expr + expr` used to cost six instructions
      (`ADJSP`, `FSTORE`, evaluate right, `FLOAD`, `ADJSP`, op); the left now
      goes to `F2`-`F7` with `FMOV` and the operation reads it from there, with
      nested expressions taking the next register up. This is what `FMOV` was
      added for. `examples/floats.c`'s `fpCrazyTown` drops from 98 to 71
      instructions; libc 9,999 -> 9,954 and libcpp 10,399 -> 10,251.

      The C++ front end checks the right operand for calls, which would clobber
      the caller-saved registers. The C front end is a one-pass stack machine
      and cannot inspect the operand it has not parsed yet, so it takes the
      register path only when the next token is a number, a character, or an
      identifier not followed by `(`, and only for `+ - * /`. All eleven parts
      (91 tests) and 91/91 WSL pass.

- **2026-11-15 13:02 -08:00** — The call-free allocation no longer stops at every
      label. The codegen emits a `.Lline_<function>_<n>` label per statement, and
      the span check treated any label as a branch boundary, which blocked the
      floating-point slots in straight-line code. A label now bounds a span only
      when a branch in the same function targets it. libc 10,029 -> 9,999 and
      libcpp 10,438 -> 10,399 before the front-end change below it in this log;
      all eleven parts (91 tests) and 91/91 WSL were run on the tree that
      contains both.

- **2026-11-15 13:14 -08:00** — `docs/optimizations.md` brought up to date with
      the current `-O1` pipeline: leaf promotion now names its full register
      range, and five passes are documented that were missing (64-bit stack
      staging as push/pop, hot-slot caching in `R8`-`R10`, call-free live-range
      allocation for integers and floating point, `FMOV` consumer folding, and
      redundant zero compares). The safety model gained the call-free rules. The
      benchmark table was stale by every change of the last day; it now carries
      instructions and bytes for `floats.c`, `libcpp.cpp`, `libc` at `-O0` and
      `-O1`, and the kernel, with the old tick rows kept.

- **2026-11-15 13:41 -08:00** — Floating-point literals are their own token kind
      (`TokenKind::Float`), not `Number`, so the C front end's register path for
      a binary operand never matched a literal right operand and those
      expressions still staged through the stack. Accepting `Float` removes the
      last staging from `examples/floats.c`: `fpCrazyTown` 71 -> 62 instructions
      with zero `ADJSP` staging, the file 289 -> 280. libc 9,954 -> 9,900 and
      409,396 -> 409,180 bytes; libcpp unchanged at 10,251. All eleven parts
      (91 tests) and 91/91 WSL pass.

- **2026-11-15 14:09 -08:00** — Golden vectors recomputed with an independent
      encoder. `tests/unit/test_golden_vectors.cpp` rebuilds every §12 row from
      its fields using the §2.1 layout rather than the implementation's
      constants, encodes all 72 opcodes in §4 with both widths, and checks that
      `kr32::isa::encode`/`decode` agree. It found one defect in the document:
      §4.4 claimed 71 of 128 top-level slots in use and 57 reserved; the map uses
      72 and reserves 56. It also caught a bug in the test itself first — the
      inline field is signed 11-bit, and `ADJSP #-16` failed until bit 10 was
      sign-extended. The document is now draft 0.4, the §13 golden-vector item is
      closed, and §12 states that the table is a sample rather than the coverage.
      All eleven parts (92 tests) and 92/92 WSL pass.

- **2026-11-15 14:10 -08:00** — Opcode-map review closed and the Q9/Q10 decision
      brief written (`docs/isa-v1-freeze.md`). The review used the disassembler
      as the oracle over all 128 top-level slots: 72 are recognised, exactly the
      72 the document lists, no undocumented slot, no missing one, every mnemonic
      matches (`0x39` renders `JNC` through the documented condition alias), and
      a reserved slot executed for the purpose raises `#UD` with `PC` unchanged
      and no instruction retired. The brief records that the ISA's FP operation
      set is correctly rounded and has no FMA or transcendental functions, so a
      soft-float library buys nothing for bit-exactness; the hazards are host
      configuration (FTZ/DAZ, extended precision), not arithmetic. The document
      is draft 0.5 with the review recorded in §14, and Q9/Q10 remain the two
      decisions for the operator.

- **2026-11-15 14:51 -08:00** — `examples/floats.c` modernised to use `printf`.
      The example predates the console printer's support for floating point: it
      walked strings and formatted integers by hand, and its one `printf` call
      had no prototype. Without a prototype the C front end compiles a call with
      the fixed-parameter convention, so a `double` variadic argument went to
      `F0` and `va_arg` read an empty stack slot - which is why the new line
      printed 0.000000. All of `main`'s output now goes through `printf`, the
      hand-written helpers are gone, and the file declares the prototype.
      `run_c_floats_variadic` asserts the printed value and was checked to fail
      without the declaration. The example is 223 instructions at `-O1` (was
      280) and 6,127 bytes. All eleven parts (93 tests) and 93/93 WSL pass.

- **2026-11-15 16:27 -08:00** — `printf` format coverage and a rounding fix. The
      new `examples/printf-format.c` formats 42 cases with `snprintf` and
      compares them, and it found four defects: `%.0f` of 2.5, 0.5 and -2.5 and
      `%.1f` of 2.25 all rounded half away from zero, where C and IEEE-754 round
      ties to even. Both float formatters now scale without the `+ 0.5` bias and
      test the parity of the last retained digit - the whole part when no
      fraction is printed, otherwise the last fraction digit. The first attempt
      tested `fraction` unconditionally and left `%.0f` of 3.5 at 3.

      The "space flag prints 0" defect I reported earlier is not real: `% d` is
      an integer conversion, and passing a double to it reads the low word, which
      is zero. The float conversions with a space flag (`% f`, `% e`, `% g`) were
      correct all along. Two of the suite's own expectations were wrong as well
      (`%o` of 42 is 52 without `#`, and the `%%` check compared literals instead
      of formatting), and were corrected before the fix landed. `run_c_printf_format`
      asserts "0 failures" in `part09`. All eleven parts (94 tests) and 94/94 WSL
      pass.

- **2026-11-15 16:52 -08:00** — The C front end now diagnoses a call to an
      undeclared function: `warning: implicit declaration of function 'name'`.
      It remains a warning rather than an error because C89 declared it
      implicitly, but no existing example or test triggers it, so the corpus was
      already clean. The warning goes through the same `Diagnostic` channel as
      `#warning`, so the CLI prints it with the source position and compilation
      continues; the column points at the argument list rather than the name.
      docs/abi.md records why it matters: without a prototype the front end
      cannot know the callee is variadic, which is how a `printf` double reached
      `F0` instead of the stack. A unit test covers the warning, a declared
      prototype, and a declared variadic prototype. All eleven parts (94 tests)
      and 94/94 WSL pass.

## 10. Updating this file

- Update checklists after every session; mark tasks `[x]` as they finish.
- Add a running-log entry for every session, decision, dependency change, or
  blocker. Use `local-datetime` for the timestamp.
- When a milestone gate passes: check every box, run the full gate, update the
  docs, commit, tag the milestone, and log it.
- Keep the environment and dependency sections current; record exact versions
  when they change.
- Do not delete history. Move abandoned tasks to **Deferred** with a reason.
- Keep `Last updated` at the top current.
