# KR32OS

KR32OS is a small operating system for the KR32 virtual machine.

## Scope

- All sources in this directory target the KR32 ISA and KR32 toolchain.
- Host compilers (MSVC, GCC, Clang) must not compile or include these sources.
- The root CMake build intentionally does not add this directory.

## Status

M0-M4 boot path is implemented: BIOS boot and paging, serial shell, exception
reporting, timer IRQ, initrd loading, process table, and a first user-mode init
process. Remaining M4 infrastructure work is listed in
[kr32os-impl.md](kr32os-impl.md). Architecture is recorded in
[DESIGN.md](DESIGN.md).

## Build

Windows:

```text
powershell -ExecutionPolicy Bypass -File kr32os/build.ps1
```

WSL/Linux:

```text
bash kr32os/build.sh
```

Both scripts accept `KR32` through the environment; `build.ps1` also accepts
`-Kr32 <path>`. The default is the host Debug build.

Run:

```text
kr32 os kr32os/build/kernel.bin --bios kr32os/build/bios.bin
```

For a scripted session:

```text
kr32 os kr32os/build/kernel.bin --bios kr32os/build/bios.bin \
    --serial-input "ls
run init
exit
"
```

## Planned layout

```text
kr32os/
  bios/       reset vector, ROM bootstrap, page tables, firmware stubs
  boot/       kernel entry and BIOS handoff
  kernel/     traps, memory, console, syscalls
  libc/       OS-specific crt0 and syscall wrappers
  user/       shell and test programs
  include/    shared KR32OS headers
  build/      host-side build scripts and generated artifacts (ignored)
```
