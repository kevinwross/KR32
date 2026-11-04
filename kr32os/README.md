# KR32OS

KR32OS is a small operating system for the KR32 virtual machine.

## Scope

- All sources in this directory target the KR32 ISA and KR32 toolchain.
- Host compilers (MSVC, GCC, Clang) must not compile or include these sources.
- The root CMake build intentionally does not add this directory.

## Status

Scaffold only. The boot ABI, build workflow, kernel structure, and initial
services will be defined in the design session before implementation.

## Planned layout (to be confirmed)

```text
kr32os/
  bios/       reset vector, ROM bootstrap, page tables, firmware stubs
  boot/       kernel entry and BIOS handoff
  kernel/     traps, scheduler, memory, console, syscalls
  user/       shell and test programs
  include/    shared KR32OS headers
  build/      host-side build scripts and generated artifacts (ignored)
```
