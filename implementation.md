# KR32 Implementation Plan

> **Status:** active. This file is the working checklist, milestone gate, dependency
> record, and running log for KR32.
>
> **Last updated:** 2026-11-02 01:19 -08:00
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

**Tasks**

- [ ] FPR file, `FLOAD`/`FSTORE`, FP escape, `.S`/`.D` arithmetic, compares,
      conversions, moves.
- [ ] RNE, canonical NaN, denormals, no traps, saturating conversions.
- [ ] Tile file, `MMUL`/`MMAC`/`MVEC`/`MTRANS`/`VDOT`.
- [ ] `MLOAD`/`MSTORE`, `VLOAD`/`VSTORE`, strided loads, `MPUSH`/`MPOP`/`MDROP`.
- [ ] FP and matrix demos: transform a vector, multiply two matrices, dot
      product, push/pop a transform.
- [ ] Unit tests for FP flags/NaN/conversions and matrix results.

**Test plan and gate**

- [ ] `FADD.S`/`FADD.D`, `FEQ`/`FLT`/`FLE`, conversions match the spec.
- [ ] `MMUL` matches a reference 4x4 multiply; `MVEC` matches a reference.
- [ ] Strided `MLOAD` reads a 4x4 block from a larger matrix.
- [ ] Transform stack push/pop restores the parent matrix.
- [ ] Windows and WSL tests pass.
- [ ] Commit and tag `m2b`; log entry added.

### M3 — Container, linker, build pipeline

**Goal:** relocatable objects, a linker, a `.k32` image, and `kr32 build`.

**Tasks**

- [ ] Write `docs/container.md`: header, sections, symbols, relocations, debug
      sections, CRC.
- [ ] Container reader/writer with round-trip tests.
- [ ] Assembler emits relocatable objects with `.text`, `.rodata`, `.data`,
      `.bss`, symbols, and relocations.
- [ ] Linker: layout, symbol resolution, relocation, map, strip; define
      `__heap_start`, `__stack_top`, `__stack_limit`.
- [ ] CLI: `kr32 ld`, `kr32 size`, `kr32 build` for `.s` inputs.
- [ ] Golden object and executable fixtures.
- [ ] `build` pipeline: assemble → link → run.
- [ ] C++ readiness: reserve weak/COMDAT flags, relocation types, section
      merging, and extensible debug sections.

**Test plan and gate**

- [ ] Container round-trip for every section and relocation type.
- [ ] Two-object link resolves symbols and relocations correctly.
- [ ] Linker map lists sections and symbols.
- [ ] `kr32 build examples/hello.s --run` succeeds.
- [ ] Windows and WSL tests pass.
- [ ] Commit and tag `m3`; log entry added.

### M4 — Simple C compiler and minimal runtime

**Goal:** a C subset that compiles, assembles, links, and runs C programs.

**Tasks**

- [ ] Freeze the C subset: `int`, `char`, `_Bool`, pointers, arrays, structs,
      unions, enums, typedefs, qualifiers, casts, control flow, functions,
      prototypes, string/character literals.
- [ ] Preprocessor: object/function macros, `#include`, conditionals; built-in
      headers.
- [ ] Lexer, parser, type checker.
- [ ] Code generation to KR32 assembly; ILP32 ABI with frames and callee-saves.
- [ ] `crt0`, `putchar`, `exit`, `memcpy`/`memset`/`strlen`; bump allocator.
- [ ] Minimal `SYSCALL` console/exit path (full services in M7).
- [ ] `kr32 cc`, `kr32 build file.c`; link the runtime automatically.
- [ ] Diagnostics `file:line:column: message`.
- [ ] C++ readiness: separate IR/back end, shared ABI document, debug-record
      hooks, no C-only assumptions in the object model.

**Test plan and gate**

- [ ] `kr32 build examples/hello.c --run` prints the expected text.
- [ ] Arithmetic, pointers, arrays, structs, recursion, `switch`, `goto`.
- [ ] Compiler diagnostics tests for common errors.
- [ ] Runtime and ABI tests.
- [ ] Windows and WSL tests pass.
- [ ] Commit and tag `m4`; log entry added.

### M5 — Debug information, `DebugSession`, CLI debugger

**Goal:** the shared debug model that the GUI, DAP, CLI, and tests will use.

**Tasks**

- [ ] Debug sections: lines, sources, variables, frames, types, globals,
      strings.
- [ ] Compiler emits `.loc` rows, variable/frame/type/global records; assembler
      and linker relocate them.
- [ ] `DebugSession` API: load, breakpoints, step in/over/out, registers,
      memory, variables, call stack, console.
- [ ] CLI `kr32 debug` dumps debug records; `vm --trace` remains.
- [ ] Tests: line mapping, variable lookup, frames, breakpoints, stepping.

**Test plan and gate**

- [ ] Source line lookup matches the compiled program.
- [ ] Variables and frames are correct at a breakpoint.
- [ ] Step over/in/out behave on a small C program.
- [ ] Debug dump identifies files, functions, and types.
- [ ] Windows and WSL tests pass.
- [ ] Commit and tag `m5`; log entry added.

### M6 — Windowed debugger

**Goal:** the KR16-style GUI debugger, adapted to KR32 and SDL2.

**Tasks**

- [ ] Port KR16 `src/ui` layout, painting, widgets, hit testing, and font
      atlas; carry the license notices.
- [ ] Port the SDL3 backend to SDL2; software RGBA canvas presented as an SDL
      texture.
- [ ] Panels: source, disassembly, registers, variables, watches, memory,
      call stack, console.
- [ ] KR32 panels: FPRs, tiles as 4x4 grids, MMU/page tables, transform stack,
      core selector.
- [ ] Toolbar: Load/Build, Run, Break, Restart, counters, settings, guest
      arguments.
- [ ] Breakpoints, step over/in/out, error dialog.
- [ ] Offscreen UI tests and golden screenshots; dummy-driver smoke test.
- [ ] `KR32_BUILD_UI=OFF` continues to build and test.

**Test plan and gate**

- [ ] Debug a C program: source and disassembly follow the PC.
- [ ] Breakpoints, stepping, registers, memory, variables, call stack,
      console.
- [ ] Matrix/tile and MMU panels render correct values.
- [ ] UI works on Windows and WSL (WSLg); headless build remains green.
- [ ] Commit and tag `m6`; log entry added.

### M7 — Host services, sandbox, shell, DAP

**Goal:** a usable hosted runtime and the secondary debug interface.

**Tasks**

- [ ] `SYSCALL` ABI block: console, file I/O, time, random, exit.
- [ ] Sandbox: `--fs-root`, quota, read-only, path rules.
- [ ] Runtime wrappers for file I/O and stdio subset.
- [ ] `kr32 shell`: commands, history, piped input.
- [ ] DAP server over TCP sharing `DebugSession`; VS Code smoke test.
- [ ] Guest arguments, traces, stats.

**Test plan and gate**

- [ ] C program reads and writes inside the sandbox; escapes are rejected.
- [ ] Shell runs the tool commands and a scripted session.
- [ ] DAP launch, breakpoints, stepping, memory, and evaluate work in VS Code.
- [ ] Windows and WSL tests pass.
- [ ] Commit and tag `m7`; log entry added.

### M8 — Devices, time, full multicore

**Goal:** deterministic devices and the complete multicore model.

**Tasks**

- [ ] Timer, block, and console devices; IRQ route table; MMIO.
- [ ] Device timing on the tick clock; DMA.
- [ ] Multicore scheduler, IPIs, `LL`/`SC` stress test.
- [ ] GUI core selector and per-core panels.
- [ ] Determinism tests for interrupts and devices.

**Test plan and gate**

- [ ] Timer interrupt arrives at the expected tick.
- [ ] Block DMA completes and raises the routed interrupt.
- [ ] Two-core spinlock increments correctly; runs reproduce.
- [ ] Windows and WSL tests pass.
- [ ] Commit and tag `m8`; log entry added.

### M9 — C++ toolchain

**Goal:** a C++ compiler module linked into `kr32`, with C++ source debugging.

**Tasks**

- [ ] Choose and pin the C++ front end (EDG or a custom front end); record the
      license and build steps.
- [ ] `kr32_cxx` module: front end, lowering, code generation, weak/COMDAT
      merge.
- [ ] C++ ABI and runtime: classes, virtual dispatch, templates, exceptions,
      RTTI, `new`/`delete`, static initialization.
- [ ] Debug info for C++ names, types, and lines; GUI and DAP support.
- [ ] `kr32 cxx`, `kr32 build file.cpp`.
- [ ] Tests: classes, templates, virtual calls, exceptions, standard-library
      subset.

**Test plan and gate**

- [ ] C++ hello world and a class/template demo run.
- [ ] Exceptions unwind correctly.
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
