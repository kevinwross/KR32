# Release note — KR32 0.3.0

**Status:** release 0.3.0, tagged `v0.3.0` on `adopt-krc`.
**Applies to:** KR32 0.3.0, krc-only.
**Previous release:** 0.2.0 was the last release that shipped the built-in
compiler, assembler, linker and runtime.

## The breaking change

0.3.0 removes the built-in toolchain. Guest C and C++ is compiled by the
installed **krc** toolchain (KRCompiler 4.12 or later, built with
`KRC_BUILD_TOOLS=ON`), and the `kr32` executable runs, disassembles, debugs and
boots prebuilt `.k32` images. The built-in compiler path, its commands and its
runtime are gone.

This is a breaking release, which is why the version is 0.3.0 rather than a patch.

## What disappeared

- The in-tree C and C++ front ends, assembler and linker: `src/cc`, `src/cxx`,
  `src/asm`, `src/link`.
- KR32's freestanding libc/runtime and the injected guest-header strings.
- The `KR32_TOOLCHAIN` configure option and the `builtin` value. krc is the only
  toolchain and needs no selection.
- The CLI commands `kr32 as`, `kr32 ld`, `kr32 build`, `kr32 cc` and `kr32 cxx`,
  and the standalone `kr32-cxx` driver.
- The window's built-in-compiler branch; its **Build** calls krc.

## `cons.h`

`<kr32/cons.h>` replaces `<kr32/con.h>`. That rename already shipped in 0.2.0
because Windows reserves `CON`; 0.3.0 keeps the new name. Programs that already
moved to `cons.h` need no change for this release.

## How an existing user migrates

1. **Install krc.** Use KRCompiler 4.12+ built with `KRC_BUILD_TOOLS=ON`. It
   exports `KRC_SYSROOT` and `KRC_KR32_RUNTIME_DIR`.
2. **Configure with its prefix.** There is no toolchain option:
   `cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=<krc install>` (Windows) or
   `cmake --preset linux-gcc -DCMAKE_PREFIX_PATH=<krc install>` (WSL/Linux).
3. **Build guest programs with krc, not `kr32`.** Replace `kr32 build x.c` with
   the krc toolchain and run the resulting `.k32` with `kr32 vm <image>`; the
   window's **Build** already does this.
4. **Update scripts.** Any script calling `kr32 as`, `kr32 ld`, `kr32 build`,
   `kr32 cc` or `kr32 cxx` must call the krc toolchain instead. Prebuilt `.k32`
   images continue to run unchanged through `kr32 vm`, `kr32 debug`, `kr32 dap`
   and the window.
5. **Check the C++ corpus gaps.** The differences accepted as permanent are in
   `docs/cxx-gap-signoff.md`: scalar `throw`, runtime-bound `new[]`, `T()`, the
   plain-aggregate `S()` correction, `std::thread::id`, `<fstream>`, and the
   debug-information display differences.

The command surface that remains:

```text
kr32 vm <file.k32>          run an image
kr32 dis <image>            disassemble a raw image
kr32 size <file.k32>        section sizes
kr32 debug <file.k32>       dump debug information
kr32 gui ...                windowed debugger
kr32 os ...                 boot KR32OS with a kernel and BIOS
kr32 dap ...                Debug Adapter Protocol server
kr32 shell                  interactive shell
```

## What did not change

The ISA, the `.k32` container, the emulator, the debugger model, DAP, the
windowed debugger and the shipped kernel/BIOS images are unchanged. OS mode
still boots the shipped pair or an explicit `--os <kernel> --bios <bios>`.

## Licenses

KR32 is MIT; the root `LICENSE` ships in each archive. krc-debug is MIT with the
same notice. The shipped OS images embed OFL-licensed font data (JetBrains Mono
for the console, Inter for the UI), and both OFL texts ship in each archive.
SDL2 remains a separately installed host dependency under its own zlib license.

## Stage B closure

This release follows stage B, which landed at `91165ae` after 0.2.0. The
clean-checkout gate passed 40/40 on Windows and WSL with the four directories,
the runtime and the injection gone. Decision 9's three conditions are satisfied.
