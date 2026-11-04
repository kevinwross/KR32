# KR32OS Design Notes

Status: draft for the interactive design session.

## Confirmed decisions

- KR32OS targets KR32 only; host compilers never build its sources.
- Boot path: a BIOS-compatible kernel image at fixed physical entry `0x0002_0000`.
- Language split: minimal assembly boot/trap stubs, C kernel and services.
- First runnable milestone: console output, keyboard input, and a small kernel shell.
- Build environment: folder-local scripts plus a CMake orchestrator that invokes
  the KR32 toolchain; `kr32os` is not added to the root CMake build.
- Input: the GUI keyboard feeds a guest console input queue.
- Initial shell: kernel built-ins first; user mode follows once boot is stable.
- Build coverage: Windows PowerShell and WSL Bash wrappers around the same
  orchestration.
- Flat image: `kr32 ld --flat --base 0x00020000` emits the raw kernel image.
- BIOS: track `kr32os/bios.S` as the development BIOS source.

## Boot contract (initial assumption)

The current reference BIOS enters the kernel at `0x0002_0000` with:

- paging enabled and an identity map for RAM, ROM, and MMIO;
- `PTBR = 0x0004_0000`, `VTBR = 0x0005_0000`;
- `SP = KSP = 0x0010_0000`;
- kernel mode, interrupt-enable state as supplied by the BIOS;
- console output at MMIO `0xFFF0_2000`.

The kernel installs its own vector table entries before relying on faults or
interrupts. The first milestone keeps the BIOS identity map; a later milestone
owns page-table construction.

## Proposed layers

```text
boot/entry.S        reset-to-C handoff, stack, kernel entry
kernel/main.c       banner, subsystem init, shell loop
kernel/console.c    polled console output/input
kernel/traps.c      vector table, exception reports
kernel/shell.c      built-in commands
include/            shared headers and MMIO definitions
build/              generated artifacts (ignored)
```

## Proposed milestones

1. **M0 — scaffold:** build scripts, linker layout, boot stub, halt on entry.
2. **M1 — boot banner:** kernel entry at `0x0002_0000`, console output, clean halt.
3. **M2 — console + shell:** line input, prompt, `help`, `echo`, `clear`, `mem`.
4. **M3 — faults + timer:** vector table, exception diagnostics, timer IRQ.
5. **M4 — memory + processes:** page allocator, kernel heap, user-mode image.

## M1 implementation sketch

- `entry.S` sets a kernel stack, clears `.bss`, and calls `kmain`.
- `kmain` prints a banner through the console abstraction and halts cleanly.
- Console output initially uses the existing host `putchar` path (syscall 33)
  behind `console_putc`; direct MMIO `0xFFF0_2000` is the next fallback.
- Console input starts as a guest input queue in the debug session. A `getchar`
  request with no queued byte pauses the run loop and waits for a GUI key, so
  the shell does not busy-spin.
- Shell built-ins run in kernel mode for M2.

## Build pipeline (proposal)

```text
kr32os/build.ps1          Windows entry point
kr32os/build.sh           WSL entry point
kr32os/CMakeLists.txt     project(KR32OS NONE), custom commands
kr32os/boot/entry.S       assembly
kr32os/kernel/*.c         compiled with kr32 cc -S
```

The orchestrator:

1. Compiles C sources to KR32 assembly with `kr32 cc -S`.
2. Assembles each generated `.s` and `entry.S` with `kr32 as --object`.
3. Links the objects with `kr32 ld --flat --base 0x00020000`.
4. Assembles `bios.S` at `0xFFFF0000`.
5. Runs with `kr32 vm kernel.bin --base 0x00020000 --bios bios.bin`.

## Open design questions

- Kernel heap: bump allocator first, then free-list?
- Kernel/user ABI: syscall numbers and pointer validation rules.
- Single core for M0–M2; when should multicore enter?
