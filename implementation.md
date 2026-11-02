# KR32 Implementation Plan

> **Status:** active. This file is the working checklist, milestone gate, dependency
> record, and running log for KR32.
>
> **Last updated:** 2026-11-02 14:03 -08:00
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
- [x] Preprocessor: object/function macros, `#include`, conditionals; built-in
      headers.
- [x] Lexer, parser, type checker for the M4 subset.
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
cleanup) are complete; M9.10 is in progress with stage-1 host thread/futex
services over `core::Cluster` and stage-2 TCB/TLS/context done. C++ ABI revision 1 is frozen in
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
- [ ] Compiler `thread_local`/atomics and mutex/condvar/thread headers
      (M9.10 remaining).
- [ ] Debug info for C++ names, types, and lines; GUI and DAP support.
- [ ] `kr32 cxx`, `kr32 build file.cpp`.
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
- [ ] C++ hello world and a class/template demo run.
- [x] Host thread/futex services over `core::Cluster` pass (M9.10 stage 1:
      183 cases / 4841 assertions).
- [x] Guest TCB layout, context save/restore, and guest join pass (M9.10
      stage 2: 186 cases / 4885 assertions).
- [ ] Guest threads run on multiple KR32 cores with mutex/atomic/TLS tests.
- [ ] GUI steps through a `.cpp` file with correct lines and variables.
- [ ] Windows, WSL, and ARM64 builds pass.
- [ ] Commit and tag `m9`; log entry added.

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
- **2026-11-02 10:50 -08:00** — Accepted all M9.1 ABI review recommendations
      (`this` in R0, void ctor/dtor returns, uniform 8-byte varargs, 8/16-byte
      aggregate rule, address-point vtable/RTTI, `.cpp_except` exceptions,
      direction-only TCB offsets deferred to M9.10, EBO disabled, `long double`
      alias, bare-pointer `va_list`, `KR32_BUILD_CXX_TOOL` default ON). No ABI
      contract change; [docs/abi.md](docs/abi.md) now records the approved review
      decisions and [cpp-impl.md](cpp-impl.md) remains M9.1 done. M9.2 is next.
- **2026-11-02 11:10 -08:00** — Completed M9.2. Added the C++ preprocessor,
      lexer, AST, and recursive-descent parser under `src/cxx`, wired
      `kr32::cxx::compile` through preprocess → lex → parse, and added host
      parser tests including a malformed-input corpus. Windows MSVC Debug and WSL
      GCC headless both pass 21/21 CTest tests; `kr32_tests` reports 131 test
      cases and 4617 assertions. M9.3 (semantic analysis, mangling, integer
      code generation) is next.
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
- **2026-11-02 12:36 -08:00** — Started M9.9. Added `new`/`delete` lowering to
      `malloc`/`free` for scalars, objects, and scalar arrays, including
      constructor and destructor calls. Windows/WSL both pass 21/21 CTest tests;
      `kr32_tests` reports 171 test cases and 4753 assertions. Exceptions,
      table-driven cleanup/full unwinding, class-array helpers, and static
      destruction remain; M9.9 stays in progress.
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
