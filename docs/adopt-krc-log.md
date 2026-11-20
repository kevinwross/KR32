# adopt-krc log — stage A of the krc toolchain adoption

Append-only. Every entry begins with the exact `iso` string from `local-datetime`,
called immediately before that entry was written. Entries are never rewritten; a
correction is a new entry. The plan is
`krdbg/docs/requests/kr32-adopt-krc-toolchain.md`; the brief is
`krdbg/docs/requests/brief-kr32.md`. The pinned dependency is KRCompiler
`f936155` on `adopt-krc`.

## 2026-11-20T09:35:32-08:00

**Changed:** nothing in the repository. Stage 3.1, the debug spike: a krc-built
`.k32` was opened in the current GUI and driven through a breakpoint, a local, a
call stack and line stepping. The image was built outside the tree with a
throwaway facade host in `build/spike/` (ignored), not by a KR32 code path.

**Pinned dependency:** KRCompiler `f936155b4423d86df21190024e1f0350a534a318`
on `adopt-krc`, built from a detached worktree at
`C:\Users\kevin\source\repos\.krc-pin-f936155` and installed to
`C:\Users\kevin\source\repos\.krc-install-f936155` with
`cmake --install ... --config Debug`. The facade host linked `krc::toolchain`
through `find_package(krc-toolchain CONFIG REQUIRED)`.

**Spike source (C, line 10 is the `printf`):**

```c
#include <stdio.h>

int add(int a, int b) {
    int sum = a + b;
    return sum;
}

int main(void) {
    int total = add(20, 22);
    printf("total=%d\n", total);
    return total;
}
```

**Commands and real results (Windows, Debug):**

- Facade host: `compile` (`debug=true`, `optimize=1`, `base=0x10000`,
  `entry=_start`) → 3301 bytes of assembly and 527 bytes of `.debug_info`;
  `assemble`; `link` with krc's staged KR32 runtime → `wrote 1279213 bytes,
  entry 0x2f564 base 0x10000`.
- `kr32 vm spike-krc.k32` → `total=42`, `HALTED exit=42`.
- `kr32 debug spike-krc.k32` → `source 0 build\spike\spike.c`, `function add
  0x10000-0x10060 frame=40`, `function main 0x1006c-0x100d0 frame=32`, followed
  by the libc functions. KR32's existing `debug::parse_info` reads the krc
  bridge unchanged.
- `kr32 gui spike-krc.k32 --headless --screenshot gui-krc.ppm` →
  `ui-render width=1440 height=900 hash=1DEAF1DFB37CBEA9`, exit 0. The rendered
  frame shows `SOURCE build\spike\spike.c` with lines 1–12, line 9 highlighted,
  `Locals (1) total = 0x00000000`, and `#0 main 00010078` under CALL STACK.
- `kr32 dap spike-krc.k32` with a scripted session
  (initialize, launch, `setBreakpoints` line 10, configurationDone, continue,
  stackTrace, scopes, variables, disconnect) →
  `"line":10,"verified":true`, stop `"reason":"breakpoint"`,
  `"stackFrames":[{"column":5,"instructionPointerReference":"65692","line":10,
  "name":"main","source":{"name":"source","path":"build\\spike\\spike.c"}}]`,
  `"name":"total","type":"i32","value":"0x0000002A"`.
- A driver on the real `DebuggerUI` (`load`, then `step_line_in`,
  `step_line_over`, `step_line_out`) printed:

```text
load   pc=00010078 line=9 function=main frames=1 total=0x00000000
in     pc=00010034 line=4 function=add frames=2
over   pc=00010058 line=5 function=add frames=2
out    pc=00010090 line=9 function=main frames=1 total=0x00000000
```

  A second run with two `step_line_over` calls moved 9 → 10 (total
  `0x0000002A`) → 11. Source lines, locals, call stack and line stepping all
  work in the current GUI. **3.1 is a GO.**

**Decision — the runtime linked by the krc path is krc's staged KR32 runtime, not
`cc::runtime_assembly()`.** The plan's risk table says to keep KR32's runtime and
link it with krc objects. That route does not exist:

1. krc's assembler rejects KR32's runtime assembly spelling: `unsupported
   directive .RODATA` (also `.bss`, `.space`, `.asciz`).
2. After those five spellings are rewritten to the krc set, it rejects an
   instruction: `unsupported instruction 'LL'` (KR32's libc atomics use load-
   linked), and more would follow.
3. KR32's object container and krc's KRO object are different formats; krc's
   linker rejects a KR32 object with `KRO magic mismatch`.
4. Compiling KR32's `src/cc/libc.c` with krc instead fails on
   `__atomic_compare_exchange_n`, `__atomic_store_n`, `__builtin_heap_start`,
   `__builtin_heap_end`, `__builtin_sqrt`, and a stricter `feof` redefinition.
5. Stage B deletes `src/asm` and `src/link`, so a final image assembled by KR32's
   assembler cannot be the adopted path.

The krc path therefore assembles and links the runtime the facade is documented
to need: `<sysroot>/kr32/lib/crt0.s` (staged copy), `<sysroot>/kr32/lib/libkrcrt.kro`,
and `fileio.s`/`eh.s` from `targets/kr32/runtime`, in that order after the user
object. Guest code built through krc uses krc's libc rather than KR32's
`cc::libc.c`; this is the deviation the plan's risk row did not anticipate, and
it is recorded here for the programme owner before stage B.

**Next step:** 3.2, the C++ corpus.

**Blocker:** none for 3.1. The runtime-engine choice above is a plan deviation
that needs a decision before stage B; it does not block stage A.

## 2026-11-20T09:35:50-08:00

**Changed:** nothing in the repository. Stage 3.2, the C++ corpus: KR32's six
guest C++ examples were compiled and linked with the pinned krc, then run under
`kr32 vm`, and the constructs in KRCompiler's `docs/cxx-front-end-gaps.md` were
reproduced separately to show the boundary of each open gap.

**Corpus and real results** (`krc --target=kr32 -c -O1 -I include --sysroot
<stage>`, then `krld <user.kro> libkrcrt.kro fileio.s eh.s crt0.s`):

| Example | Compile | Link | Run |
|---|---|---|---|
| `examples/balanced-tree.cpp` | ok | ok | sorted order (30) printed |
| `examples/cxx_libc.cpp` | ok | ok | `C++ via C libc: shared libc`, exit 0 |
| `examples/cxx_tls.cpp` | ok | ok | exit 42 |
| `examples/FileOpen.cpp` | ok | ok | not run: opens the host file dialog |
| `examples/hello.cpp` | ok | ok | exit 42 |
| `examples/matrix.cpp` | ok | ok | exit 42, so every tile-intrinsic check passed |

`matrix.cpp` is the interesting one: krc lowers the `kr32::matrix_mul`,
`transform`, `transpose` and tile calls in `include/kr32/matrix.hpp` the same way
KR32's C++ front end does, and the program's own value checks (translation,
scale-then-translate, aliasing, `new`/`delete`) all returned 42. No example was
rejected, so **no corpus failure maps to a documented gap**.

**Gap reproduction** (`build/spike/gaps/`, not committed):

| Construct | krc diagnostic |
|---|---|
| `new int[n]` with runtime `n` | `variable length arrays are not supported` (line 1, column 42) |
| `throw 1` | `throw requires a class type` (line 1, column 20) — deliberate |
| `int total = int();` | `expected expression` (line 1, column 26) plus `expected ';' after declaration` |
| control: `new int[4]` | compiles |
| control: throw/catch a one-field class | compiles |
| control: `S s = S();` for a plain struct | rejected at this pin: `constructor-initialized class has no constructor` |

The last row is a correction to `docs/cxx-front-end-gaps.md`, which records that
control as compiling; it is a documentation drift in the dependency, not a KR32
change, and it narrows gap 3 slightly rather than opening a new one.

**KR32 corpus gap not covered by KRCompiler's list:** KR32's guest headers
(`<kr32/fb.h>`, `<kr32/gfx.h>`, `<kr32/con.h>`, `<kr32/input.h>`, `<kr32/sys.h>`)
exist only as the injected string constants in
`include/kr32/cc/guest_headers.h`; they are not files, so krc's preprocessor
cannot find them. No `examples/*.cpp` uses them, so the stage-A corpus passes,
but a GUI-compiled program that includes one will fail under
`KR32_TOOLCHAIN=krc` until those headers have on-disk copies. Recorded as a named
stage-B gap.

**Next step:** 3.3–3.6, the CMake option and the two compile branches.

**Blocker:** none. 3.2 does not exercise any open krc gap; the documented subset
is not also KR32's subset, because KR32's examples are inside the supported set.

## 2026-11-20T09:48:20-08:00

**Changed (3.3–3.6):** the window's BUILD has a krc path behind
`KR32_TOOLCHAIN=krc`; the default stays `builtin`.

- CMake: `KR32_TOOLCHAIN` is a cache string with values `builtin` and `krc`.
  Only the krc branch calls `find_package(krc-toolchain CONFIG REQUIRED)`. It
  also requires `KR32_KRC_SYSROOT` (the staged sysroot) and
  `KR32_KRC_RUNTIME_DIR` (krc's `targets/kr32/runtime`), because the package
  installs neither; both are checked at configure time with a message naming the
  missing file.
- `compile_c_source` and `compile_cxx_source` take the krc branch when
  configured. It reads the source, sets `Target::Kr32`, `cxx`, `debug=true`,
  `optimize=1`, `base=0x10000`, `entry=_start`, and include paths
  `<sysroot>/include`, `<sysroot>/kr32/include`, and KR32's `include/` (for
  `<kr32/matrix.hpp>`); compiles, assembles with the debug bridge, links with
  krc's staged runtime (`libkrcrt.kro`, `fileio.s`, `eh.s`, `crt0.s`), and parses
  the returned image through `container::read`. `source_root` stays empty,
  matching the built-in path, which records the path it was given.
- Diagnostics render as `file:line:column: message`, from the first facade
  diagnostic; an empty diagnostic list falls back to the facade error.
- The listing is rendered from the `assemble` rows in the shape of
  `kr32 as --listing` (8-digit address, encoded words, statement); row offsets
  are translated to linked addresses with `LinkResult::sections`.
- Tests: `test_ui` gains a C source build/step/local assertion (the local only in
  the krc branch, because the built-in optimizer folds it away), a C++ source
  local, a rejected-source diagnostic-shape check, and a listing check; the
  listing-column assertion is krc-only through `KR32_TOOLCHAIN_KRC`. `tests`
  gains `ui_headless_os_files`, the explicit `--os <kernel> --bios <bios>` GUI
  path, next to the existing embedded one.
- Docs: `README.md` documents the option and its two inputs; `docs/ui.md` states
  that the default is builtin and what the krc configuration needs.

**Commands and real results:**

- Windows, builtin:
  `cmake --preset windows-ninja -DKR32_TOOLCHAIN=builtin`,
  `cmake --build --preset windows-ninja` (clean, `/W4 /WX`),
  `ctest --preset windows-ninja` → **100% tests passed, 0 tests failed out of
  98**, `Total Test time (real) = 104.32 sec`.
- Windows, krc: same three commands with
  `-DKR32_TOOLCHAIN=krc`,
  `-DCMAKE_PREFIX_PATH=C:/Users/kevin/source/repos/.krc-install-f936155`,
  `-DKR32_KRC_SYSROOT=.../stage/sysroot` and
  `-DKR32_KRC_RUNTIME_DIR=.../targets/kr32/runtime` → **100% tests passed, 0
  failed out of 98**, `Total Test time (real) = 113.58 sec`.
- WSL, builtin: `cmake --preset linux-gcc -DKR32_TOOLCHAIN=builtin` then
  `bash scripts/run-gate.sh` → **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 21.06 sec`.
- WSL, krc: `cmake --preset linux-gcc -DKR32_TOOLCHAIN=krc`
  `-DCMAKE_PREFIX_PATH=$HOME/krc-install-f936155`
  `-DKR32_KRC_SYSROOT=$HOME/krc-pin-f936155/build/linux-gcc-debug/stage/sysroot`
  `-DKR32_KRC_RUNTIME_DIR=$HOME/krc-pin-f936155/targets/kr32/runtime`, then
  `bash scripts/run-gate.sh` → **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 20.68 sec`.
- The first WSL krc build failed under `-Werror`: `write_listing` was defined but
  unused because both compile entry points take the krc branch. The fix wraps
  the built-in writer in `#if !defined(KR32_TOOLCHAIN_KRC)`; MSVC had not
  diagnosed it. Commit `3516460`; the four results above are from the tree
  including that fix.
- `kr32 gui examples/hello.c --headless --screenshot` and the same for
  `examples/hello.cpp` → `ui-render width=1440 height=900`, so the krc branch
  builds, loads and renders a C and a C++ source through the real `DebuggerUI`.
- OS mode: `ui_headless_os` (`--os --embedded`) and the new
  `ui_headless_os_files` (`--os kernel.bin --bios bios.bin`) both print
  `ui-render`; the booting pair is covered by `run_os_embedded` and
  `run_os_boot`. All four passed in both Windows suites and both WSL suites.
- A source with the listing setting on writes, for `examples/hello.c` under krc:

```text
00010000:                    main:
00010000: 0000e078           PUSH R7
00010004: f00f007c           ADJSP #-16
00010008: 0088e012           LEA R7, [SP]
0001000c: 08680e12           LEA R0, [R7 + 8]
```

**Commit:** `aa110da` (`Add the krc toolchain behind KR32_TOOLCHAIN`) and
`3516460` (`Keep the built-in listing writer out of krc builds`).

**Next step:** pull with rebase and push `adopt-krc`; the ARM64 tester's entry for
the tip is the remaining gate row, and it is non-blocking per the plan. Stage B
still needs the programme owner's decision on the runtime deviation logged at
`2026-11-20T09:35:32-08:00` and on the guest-header gap logged at
`2026-11-20T09:35:50-08:00`.

**Blocker:** none for stage A. The plan's "keep KR32's runtime" mitigation is
not implementable (see the 09:35:32 entry); stage A links krc's staged runtime
instead, and stage B cannot start until that is accepted or changed.

## 2026-11-20T09:51:16-08:00

**Correction to the 09:48:20 entry:** its builtin rows were measured on
`aa110da`, before the krc-only `write_listing` guard in `3516460`. Re-ran both
builtin gates on the pushed tip `adaef4f`:

- Windows: `cmake --preset windows-ninja -DKR32_TOOLCHAIN=builtin`,
  `cmake --build --preset windows-ninja`, `ctest --preset windows-ninja` →
  **100% tests passed, 0 tests failed out of 98**,
  `Total Test time (real) = 117.97 sec`.
- WSL: `cmake --preset linux-gcc -DKR32_TOOLCHAIN=builtin` then
  `bash scripts/run-gate.sh` → **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 21.34 sec`.

The krc rows in that entry stand: they were measured on `3516460`, and
`adaef4f` adds only this log file.

**Next step:** the ARM64 tester's entry for `adaef4f`; stage B awaits the runtime
decision.

**Blocker:** none for stage A.

## 2026-11-20T10:06:39-08:00

**Changed (3.8): the guest headers are on disk for both toolchains.**

- `include/kr32/fb.h`, `display.h`, `gfx.h`, `input.h` and `sys.h` are checked in
  with the declaration bodies the built-in front ends inject. `display.h` is
  included because it has the same injection-only defect and is in
  `docs/graphics.md`'s table; the item names the other five.
- `include/kr32/con.h` is **not** tracked, and cannot be. `CON` is a reserved DOS
  device name. The real behaviour on Windows:

```text
$ git add include/kr32/con.h                     # this repository, default config
error: open("include/kr32/con.h"): No such file or directory
error: unable to index file 'include/kr32/con.h'
fatal: adding files failed
$ git -c core.protectNTFS=false add include/kr32/con.h   # succeeds
$ git checkout -- con.h                          # scratch repository, default config
error: unable to create file con.h: Invalid argument
```

  CMake's `file(WRITE .../con.h)` also left no file behind, so a tracked file
  would break every Windows checkout.
- `tools/guest_headers` writes
  `<build>/generated/guest-include/kr32/con.h` from `kGuestConHeader` through the
  extended-length prefix (`\\?\`), the one spelling Windows accepts. The krc path
  puts that directory on `options.include_paths`.
- The built-in front ends stay filesystem-free and keep injecting the embedded
  bodies; the injection is not removed, because removing it would require giving
  `cc::compile` filesystem access.
- `tests/unit/test_guest_headers.cpp` now compares every carrier with the
  embedded declaration (con.h from the generated directory), so the two copies
  cannot drift. `tests/unit/test_ui.cpp` adds the acceptance source: it includes
  all five headers plus `display.h`, includes `gfx.h` twice, declares a value of
  each type, and goes through the real `DebuggerUI::load`.
- `docs/graphics.md` now describes the file and generated carriers.

**Commands and real results:**

- Focused, builtin and krc:
  `kr32_tests.exe --source-file=*test_guest_headers.cpp` →
  `test cases: 6 | 6 passed | 0 failed`, `assertions: 22 | 22 passed`.
  `kr32_tests.exe --source-file=*test_ui.cpp` →
  `test cases: 21 | 21 passed | 0 failed`, `assertions: 120 | 120 passed`
  (builtin) and `123 | 123 passed` (krc); the three extra krc assertions are the
  krc-only ones. The case `all five guest headers compile through the window`
  passed in both.
- Windows, builtin (`cmake --preset windows-ninja -DKR32_TOOLCHAIN=builtin`,
  build, `ctest --preset windows-ninja`) → **100% tests passed, 0 tests failed
  out of 98**, `Total Test time (real) = 118.77 sec`.
- Windows, krc (same preset with the krc package/sysroot/runtime arguments) →
  **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 110.96 sec`.
- WSL, builtin (`cmake --preset linux-gcc -DKR32_TOOLCHAIN=builtin`,
  `bash scripts/run-gate.sh`) → **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 22.75 sec`.
- WSL, krc (same with the package/sysroot/runtime arguments) → **100% tests
  passed, 0 failed out of 98**, `Total Test time (real) = 21.75 sec`.

**Commit:** `f2c19a4` (`Ship the guest headers as files`).

**Next step:** the ARM64 tester's entry for the tip; stage B (3.9) waits for its
second approval and the one-release window.

**Blocker:** none. The only deviation from a literal "five tracked files" is
`con.h`, which Windows cannot check out; it is generated from the same body at
build time. Renaming the public header would avoid the generator but is not
authorised by the plan.

## 2026-11-20T10:17:34-08:00

**Changed:** nothing functional. This entry is the inventory of what the krc
branch needs from outside the repository, and it adds configure-time checks where
a missing input used to reach the build or a later GUI BUILD. It is the list the
KRCompiler package has to install for a consumer to build the krc path from the
installed package alone.

**External inputs**

Everything below is consumed by `KR32_TOOLCHAIN=krc`; the repository supplies
the source text and `include/`, nothing else.

| Input | CMake variable | Consumed by | If missing today |
|---|---|---|---|
| `krc-toolchain-config.cmake` + `krc/krc-targets*.cmake` | `CMAKE_PREFIX_PATH` (`find_package`) | configure | `find_package(krc-toolchain CONFIG REQUIRED)` fails at configure and lists the prefixes searched |
| `include/krc/toolchain.h` (and the headers it includes: `krc/frontend/diagnostics.h`, `lexer.h`, and their includes) | `krc::toolchain` `INTERFACE_INCLUDE_DIRECTORIES` | build of `kr32_ui` | **new configure check**; message names the header and the include directories (see below) |
| `lib/krc-toolchain.lib` / `libkrc-toolchain.a` | `krc::toolchain` imported location | link of `kr32` and `kr32_tests` | link error naming the library path; the path is in the message, so no configure check |
| `lib/krc-debug.lib` / `libkrc-debug.a` | `krc::toolchain` → `$<LINK_ONLY:krc::debug>` | link | link error naming the library path |
| `<sysroot>/include/stdio.h`, `include/stdint.h` | `KR32_KRC_SYSROOT` | krc preprocessor at GUI BUILD | **configure check**, message names the file |
| `<sysroot>/include/` — all 30 common headers: `array, assert.h, complex.h, ctype.h, errno.h, exception, float.h, functional, limits.h, locale.h, math.h, memory, stdalign.h, stdarg.h, stdatomic.h, stdbool.h, stddef.h, stdint.h, stdio.h, stdlib.h, stdnoreturn.h, string.h, strings.h, threads.h, time.h, tuple, typeinfo, unistd.h, utility` | `KR32_KRC_SYSROOT` | krc preprocessor at GUI BUILD | `stdio.h` and `stdint.h` fail at configure; any other missing one fails the GUI BUILD with `header not found: '<name>'` at the `#include` line (real output below) |
| `<sysroot>/kr32/include/bits/stdint.h`, `bits/types.h` | `KR32_KRC_SYSROOT` | krc preprocessor (`<stdint.h>` includes the first) | **new configure check**, message names the file |
| `<sysroot>/kr32/lib/libkrcrt.kro` | `KR32_KRC_SYSROOT` | `krc_runtime_objects` reads it at GUI BUILD and passes it to `link` | **configure check**, message names the file; if removed after configure, GUI reports `cannot open <path>` |
| `<runtime>/crt0.s` | `KR32_KRC_RUNTIME_DIR` | assembled at GUI BUILD | **configure check**, message names the file |
| `<runtime>/fileio.s` | `KR32_KRC_RUNTIME_DIR` | assembled at GUI BUILD | **configure check**, message names the file |
| `<runtime>/eh.s` | `KR32_KRC_RUNTIME_DIR` | assembled at GUI BUILD | **configure check**, message names the file |

The staged sysroot also carries `kr32/lib/crt0.s`; the code does **not** read it
(it reads `<runtime>/crt0.s`), so that copy is redundant for KR32 and is not an
input.

Internal inputs, for contrast: `<build>/generated/guest-include/kr32/con.h` is
generated by `tools/guest_headers` from `kGuestConHeader`, and
`KR32_GUEST_INCLUDE_DIR` is this repository's `include/` (for
`<kr32/matrix.hpp>` and the five guest headers). Neither is external.

**New configure-time checks and their real output**

- Package public header — a fake prefix with a `krc::toolchain` target and no
  `krc/toolchain.h`:

```text
CMake Error at CMakeLists.txt:41 (message):
  krc-toolchain was found, but its public header is missing: krc/toolchain.h
  was not under its INTERFACE_INCLUDE_DIRECTORIES
  ('.../fake-krc/lib/cmake/krc-toolchain/../../../include')
```

- Missing `bits/stdint.h` — a sysroot with `include/stdio.h` and
  `include/stdint.h` but no `kr32/include/bits`:

```text
CMake Error at CMakeLists.txt:57 (message):
  KR32_TOOLCHAIN=krc: KR32_KRC_SYSROOT ('.../bad-sysroot') is missing
  'kr32/include/bits/stdint.h'
```

- Missing `eh.s` — a runtime directory with `crt0.s` and `fileio.s` only:

```text
CMake Error at CMakeLists.txt:69 (message):
  KR32_TOOLCHAIN=krc: KR32_KRC_RUNTIME_DIR ('.../bad-runtime') is missing
  'eh.s'
```

- An unchecked common header (`string.h`, removed from a copy of the staged
  sysroot) is not a host build failure; it is a GUI BUILD diagnostic that names
  the header and line:

```text
build/spike/no-string.c:1:2: error: header not found: 'string.h'
  #include <string.h>
   ^
```

**What KRCompiler must install (proposed item)**

Today `cmake --install` installs the CMake package, `include/krc/**` and the two
static libraries, and **no sysroot or runtime assembly at all**. For a consumer
to build KR32's krc path from the installed package alone, the install must also
carry:

1. `include/krc/**` — unchanged, already installed.
2. `lib/krc-toolchain.*` and `lib/krc-debug.*` — unchanged, already installed.
3. The CMake package under `lib/cmake/krc-toolchain/` and `lib/cmake/krc/` —
   unchanged, already installed, including `krc-targets-<config>.cmake`.
4. The staged target sysroot: `sysroot/include/**` (the 30 headers above),
   `sysroot/kr32/include/bits/stdint.h`, `sysroot/kr32/include/bits/types.h`,
   and `sysroot/kr32/lib/libkrcrt.kro`.
5. The runtime assembly: `crt0.s`, `fileio.s`, `eh.s` — today only in
   `targets/kr32/runtime/` (and `crt0.s` alone in the staged `kr32/lib/`).
6. A documented way to find (4) and (5), either by exporting the paths from the
   package (so `KR32_KRC_SYSROOT` and `KR32_KRC_RUNTIME_DIR` stop being hand-set)
   or by installing them at fixed relative locations the consumer can derive.
   Until then the two `-D` variables stay required.

**Gate:** unchanged from the `2026-11-20T10:06:39-08:00` entry — **98/98** on
Windows and WSL under both toolchains (Windows 118.77 s builtin, 110.96 s krc;
WSL 22.75 s builtin, 21.75 s krc). This entry changes configure checks and
documentation only; the normal krc configure was re-run against the real staged
sysroot and succeeded, and the three missing-input scenarios above were run.

**Commit:** the commit that carries this entry and the configure checks.

**Next step:** the KRCompiler package item above; stage B (3.9) still waits for
its second approval and the one-release window.

**Blocker:** none for KR32. The packaging gap is a KRCompiler item, not a
KR32 failure.

## 2026-11-20T11:11:36-08:00

**Changed (3.9): the krc branch consumes the installed package's exported
paths.** This entry also corrects the previous entry's "stage B (3.9)": stage B
is now 3.10, and 3.9 is this installed-package item.

- KRCompiler `47fa4e5` (install change `b680815`) is built and installed to a
  temp prefix. Its `krc-toolchain-config.cmake` exports
  `KRC_SYSROOT=<prefix>/share/krc/sysroot` and
  `KRC_KR32_RUNTIME_DIR=<prefix>/share/krc/sysroot/kr32/lib`.
- KR32's krc branch now uses those variables. `KR32_KRC_SYSROOT` and
  `KR32_KRC_RUNTIME_DIR` are removed from CMake and the cache; the only external
  input is `-DCMAKE_PREFIX_PATH=<krc install>`.
- Configure fails naming the variable when the package does not export it, and
  names the path when a required file is absent: `KRC_SYSROOT` needs
  `include/stdio.h`, `include/stdint.h`, `kr32/include/bits/stdint.h` and
  `kr32/include/bits/types.h`; `KRC_KR32_RUNTIME_DIR` needs `libkrcrt.kro`,
  `crt0.s`, `fileio.s` and `eh.s`.
- The code's compile definitions are `KR32_KRC_SYSROOT_PATH` and
  `KR32_KRC_RUNTIME_PATH`, filled from the exported values; the runtime reader
  takes `libkrcrt.kro` and the three `.s` files from the one runtime directory.
- `README.md` and `docs/ui.md` drop the two `-D` inputs.

**Installed-prefix acceptance:** KRCompiler's build tree was deleted on both
hosts before KR32 was configured:

```text
C:\Users\kevin\source\repos\.krc-pin-47fa4e5\build        -> deleted
/home/kevinro/krc-pin-47fa4e5/build                       -> deleted
```

The installed prefixes are
`C:\Users\kevin\source\repos\.krc-install-47fa4e5` and
`/home/kevinro/krc-install-47fa4e5`; each carries
`share/krc/sysroot/{include,kr32/include,kr32/lib}` including
`libkrcrt.kro`, `crt0.s`, `fileio.s` and `eh.s`.

**Exported values configured with:**

```text
Windows:
-- KR32: KRC_SYSROOT=C:/Users/kevin/source/repos/.krc-install-47fa4e5/share/krc/sysroot
-- KR32: KRC_KR32_RUNTIME_DIR=C:/Users/kevin/source/repos/.krc-install-47fa4e5/share/krc/sysroot/kr32/lib
WSL:
-- KR32: KRC_SYSROOT=/home/kevinro/krc-install-47fa4e5/share/krc/sysroot
-- KR32: KRC_KR32_RUNTIME_DIR=/home/kevinro/krc-install-47fa4e5/share/krc/sysroot/kr32/lib
```

`build/windows-ninja/CMakeCache.txt` contains no `KR32_KRC_*`, `f936155` or
`krc-pin` entry. The krc compile command carries
`-DKR32_KRC_SYSROOT_PATH=\".../.krc-install-47fa4e5/share/krc/sysroot\"`.

**Missing-export errors, real output:**

```text
CMake Error at CMakeLists.txt:30 (message):
  krc-toolchain did not export KRC_SYSROOT.  The package has no target
  sysroot; install a KRCompiler built with KRC_BUILD_TOOLS=ON.

CMake Error at CMakeLists.txt:35 (message):
  krc-toolchain did not export KRC_KR32_RUNTIME_DIR.  The package has no KR32
  runtime directory.
```

**Gate, fresh build trees on both hosts:**

- Windows, builtin: configure, build, `ctest --preset windows-ninja` →
  **100% tests passed, 0 tests failed out of 98**,
  `Total Test time (real) = 110.01 sec`.
- Windows, krc (`cmake --preset windows-ninja -DKR32_TOOLCHAIN=krc
  -DCMAKE_PREFIX_PATH=C:/Users/kevin/source/repos/.krc-install-47fa4e5`) →
  **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 122.44 sec`.
- WSL, builtin (`cmake --preset linux-gcc -DKR32_TOOLCHAIN=builtin`,
  `bash scripts/run-gate.sh`) → **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 21.28 sec`.
- WSL, krc (`cmake --preset linux-gcc -DKR32_TOOLCHAIN=krc
  -DCMAKE_PREFIX_PATH=$HOME/krc-install-47fa4e5`, `bash scripts/run-gate.sh`) →
  **100% tests passed, 0 failed out of 98**,
  `Total Test time (real) = 21.13 sec`.

**Commit:** `0dad664` (`Consume the installed krc sysroot and runtime`); this
entry follows in a documentation commit.

**Next step:** stage B (3.10) still needs its second approval and the one-release
window. The inventory item for KRCompiler is satisfied by `47fa4e5`; no further
KR32 work is pending.

**Blocker:** none.

## 2026-11-20T11:16:58-08:00

**Correction to the `11:11:36` entry:** the code commit for "Consume the
installed krc sysroot and runtime" is `fee51ea`, not `0dad664`. `0dad664` was
committed, then the pull-rebase onto the ARM64 tester's `b77018f` rewrote it;
only `fee51ea` is in the published branch. The earlier entry stands; this one
supersedes its SHA.

**SHA audit.** Every KR32 commit SHA the log names was checked against the
pushed tip (`aa120ad`) with `git merge-base --is-ancestor <sha> origin/adopt-krc`:

```text
aa110da -> ancestor
3516460 -> ancestor
adaef4f -> ancestor
f2c19a4 -> ancestor
0dad664 -> NOT an ancestor  (corrected above to fee51ea)
fee51ea -> ancestor
```

The KRCompiler SHAs the log also names (`f936155`, `47fa4e5`, `b680815`) are
dependency commits, not KR32 commits; they were verified in the KRCompiler
repository and are not expected to be ancestors here.

This is the **second rebase that has moved a recorded SHA**. The correction
habit matters more than the individual fix: record a SHA only after confirming
it is an ancestor of the pushed tip, and re-run this audit after every
pull-rebase before pushing.

**Stage B (3.10) readiness — for the approver, not a plan change.** Not started
and not authorised. It needs (a) the second approval and (b) a release in which
both toolchains have shipped. It then flips the default to `krc` and deletes
`src/cc`, `src/cxx`, `src/asm`, `src/link`, KR32's runtime (`runtime_assembly`
and its embedded libc), and the injected guest-header strings
(`include/kr32/cc/guest_headers.h` and the injection branches). Its acceptance is
a green suite on a clean checkout with no built-in toolchain present.

**Commit:** the commit that carries this entry.

**Blocker:** none; stage B waits for its approval and the release window.

## 2026-11-20T14:17:47-08:00

**Stage-B trial — evidence only, nothing merged or pushed.** A local scratch
branch `stage-b-evidence` was cut from `4063a86` and deleted after the run. The
default was flipped to `krc`; `src/cc` (3 files), `src/cxx` (13), `src/asm` (3),
`src/link` (1) and `include/kr32/cc/guest_headers.h` (1) were deleted, and the
four library targets plus the `kr32-cxx` tool were removed from CMake so
configuration could start. **23 files changed, 2 insertions, 42,562
deletions.**

**Result: RED.**

- Windows: `cmake --preset windows-ninja -DCMAKE_PREFIX_PATH=<installed 47fa4e5>`
  configured (94 tests registered). Build stopped at the first error:

```text
tools/guest_headers/main.cpp(15): fatal error C1083:
Cannot open include file: 'kr32/cc/guest_headers.h': No such file or directory
```

  `ctest --preset windows-ninja` → **2% tests passed, 92 tests failed out of 94**
  (only `boundary_bad` and `boundary_clean` passed; everything else was
  `Not Run` because no test executable was produced), 0.31 s.
- WSL: the same configure succeeded (94 tests), the same build error
  (`tools/guest_headers/main.cpp:15:10: fatal error: kr32/cc/guest_headers.h:
  No such file or directory`), and the same **2% / 92 of 94** result in 0.04 s.

**Why it is red — what a green stage B still needs.** The four-directory
deletion is not self-contained:

1. `tools/guest_headers` (the `con.h` generator) reads `kGuestConHeader` from the
   deleted `include/kr32/cc/guest_headers.h`. Stage B needs a new home for that
   body, or the generator must be removed and `con.h` obtained another way.
2. `src/ui/debugger_ui.cpp` (4 includes), `src/cli/cli.cpp` (5) and at least 12
   test sources include `kr32/asm`, `kr32/cc`, `kr32/cxx` or `kr32/link` headers.
   The CLI's `as`, `ld`, `build`, `cc` and `cxx` commands and the UI's built-in
   branches must be removed or reworked before the tree compiles.
3. The `kr32os_images` fixture and the assembler/C/C++ integration tests invoke
   `kr32 build`, so the OS image build and most integration tests disappear with
   the assembler and linker.
4. Unit coverage for the deleted components (`test_cc_*`, `test_cxx_*`,
   `test_assembler`, `test_object_assembler`, `test_linker`, `test_optimizer`)
   and tests that build images through them (`test_ui`, `test_debug`,
   `test_host`, `test_threads`, `test_fast`, `test_boot`, `test_devices`,
   `test_disassembler`, `test_guest_headers`) need replacement or deletion.

For the approver: 3.10 as written is a red deletion-only change. Either its scope
must include the UI/CLI/test/OS migration and a new carrier for the `con.h`
body, or it stays unauthorised. This trial did not implement or merge that
migration; the scratch branch is gone and the default is still `builtin`.

**Diff size:** 23 files changed, 2 insertions(+), 42,562 deletions(-); 21 files
deleted, plus `CMakeLists.txt` and `tests/CMakeLists.txt`.

**Test count:** 94 registered, 2 passed, 92 failed (Windows and WSL).

**Next step:** the second approval decision. The deletion list and the red gate
are the review material; no branch exists to merge.

**Blocker:** stage B is red as a deletion-only change; the migration work above
is required first.
