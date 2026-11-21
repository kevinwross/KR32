# Release note — the both-toolchain release

**Status:** draft for the release that precedes stage B. It is not a claim that
this release has shipped.
**Applies to:** KR32 0.1.0 with `KR32_TOOLCHAIN` (default `builtin`) and the
published `krc::toolchain` package (KRCompiler `47fa4e5`, 4.12).

## What ships

KR32 ships with two compilers for the window's **Build** and its C/C++ example
images:

- `builtin` — the in-tree C and C++ front ends, assembler and linker. It remains
  the default in this release.
- `krc` — the published `krc::toolchain` package, selected at configure time
  with `-DKR32_TOOLCHAIN=krc`. The package exports `KRC_SYSROOT` and
  `KRC_KR32_RUNTIME_DIR`; the KR32 build takes no build-tree paths and no
  `KR32_KRC_*` inputs.

Both toolchains share the same guest headers, which are now files under
`include/kr32/`. The one API change in this release is `<kr32/con.h>` becoming
`<kr32/cons.h>`: Windows reserves `CON`, so `con.h` cannot be a tracked file.
External programs that include `<kr32/con.h>` must rename the include. No
in-tree program includes the old spelling.

The shipped kernel and BIOS images are unchanged. OS mode (`--os --embedded` and
an explicit `--os <kernel> --bios <bios>` pair) runs the shipped pair as
fixtures. The test suite (40 registered tests) builds its C/C++ images through
the installed krc package and no longer invokes `kr32 build`; it is green in
both toolchain configurations on Windows and WSL.

## Enabling krc

Windows:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows-ninja -DKR32_TOOLCHAIN=krc `
    -DCMAKE_PREFIX_PATH=<krc install prefix>
& $cmake --build --preset windows-ninja
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --preset windows-ninja
```

WSL:

```sh
cmake --preset linux-gcc -DKR32_TOOLCHAIN=krc -DCMAKE_PREFIX_PATH=<krc install prefix>
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

The install prefix must be a KRCompiler 4.12 or later built with
`KRC_BUILD_TOOLS=ON`; the configure step prints the exported `KRC_SYSROOT` and
`KRC_KR32_RUNTIME_DIR` and fails naming either variable or any missing sysroot
or runtime file.

## What stage B will remove

Stage B (3.10b) is conditionally approved but not authorised to land yet. When
its conditions hold it will:

- flip the default to `krc` and remove the `KR32_TOOLCHAIN` choice;
- remove the window's built-in compile branches and the CLI's `as`, `ld`,
  `build`, `cc` and `cxx` commands;
- delete `src/cc`, `src/cxx`, `src/asm` and `src/link`, together with KR32's
  runtime/libc and the injected guest-header strings.

After stage B, `krc` is the only compiler, the CLI can run, disassemble, debug
and boot prebuilt `.k32` images but cannot assemble, link or compile them, and
`<kr32/cons.h>` remains the guest console header. Scripts and users of
`kr32 as`, `kr32 ld`, `kr32 build`, `kr32 cc` or `kr32 cxx` must move to the krc
toolchain or the window's Build before that release.

## What must be true before stage B lands

1. A release has shipped with both toolchains available and
   `KR32_TOOLCHAIN=krc` documented.
2. A written sign-off names the C++ corpus differences accepted as permanent
   (`docs/cxx-gap-signoff.md`).
3. The clean-checkout gate is green with the four directories, the runtime and
   the injection gone.
