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
- BIOS: lives in `kr32os/bios/`; assembly reset/privileged stubs with optional
  freestanding C helpers for page tables, BootInfo, and diagnostics.
- Kernel heap: bump allocator first; introduce a free-list only when allocation
  patterns justify it.
- Kernel/user ABI: stable numbered syscall vectors with explicit user-pointer
  validation and copy-in/copy-out helpers.
- Multicore: single core through M4; bring up secondary cores after the
  scheduler and user-mode path are stable.
- Console: a new MMIO serial/tty device at `0xFFF05000`; the existing console
  remains a host/debug output path.
- UI input: hover the console panel to send text keys, Enter, Backspace, and
  Ctrl-V paste; function keys stay as debugger shortcuts.
- First user process: flat image at `0x00400000`, stack top `0x07F00000` with a
  guard page at `0x07E00000`; the first program is an embedded initrd blob.
- Physical memory: bitmap page allocator, one bit per 4 KiB page.
- Early exceptions: panic with vector/PC/FAR/error diagnostics, then halt.


## Boot contract (initial assumption)

The current reference BIOS enters the kernel at `0x0002_0000` with:

- paging enabled and an identity map for RAM, ROM, and MMIO;
- `PTBR = 0x0004_0000`, `VTBR = 0x0005_0000`;
- `SP = KSP = 0x0010_0000`;
- kernel mode, interrupt-enable state as supplied by the BIOS;
- console output at MMIO `0xFFF0_2000`.

Initial low-memory layout:

```text
0x0002_0000  kernel image
0x0003_F000  BootInfo page
0x0004_0000  page directory and page tables
0x0005_0000  vector table
0x0006_0000  stack guard page (not present)
0x0006_1000  usable stack
0x0010_0000  SP / KSP, stack grows down
```

The low 4 MiB is identity-mapped, so the kernel's linked addresses are valid
both before and after paging is enabled. The guard page protects the vector
table and page tables from ordinary stack growth.

Initial high-memory layout:

```text
0x0801_0000..0xFBFF_FFFF  unbacked gap
0xFC00_0000..0xFDFF_FFFF  framebuffer aperture (32 MiB)
0xFE00_0000..0xFFEF_FFFF  unbacked gap
0xFFF0_0000..0xFFFE_FFFF  MMIO window
0xFFFF_0000..0xFFFF_FFFF  ROM
```

The framebuffer uses a 1920x1080 RGBA surface with a 7680-byte stride. Front and
back buffers are at offsets `0x000000` and `0x7E9000`. Control registers live at
`0xFFF0_4000`. ROM and MMIO are mapped supervisor-only (`U = 0`); ROM is
read-only and executable, while MMIO is writable and non-executable.

The framebuffer aperture needs eight page tables (`PDE 0x3F0..0x3F7`) in the
low page-table region. Those mappings are identical for every process and can
share the same page tables. Framebuffer PTEs are writable, non-executable, and
supervisor-only initially.

The BootInfo structure carries:

```c
uint32_t framebuffer_base;   // 0xFC000000
uint32_t framebuffer_size;   // 0x02000000
uint32_t framebuffer_stride; // 7680
uint32_t framebuffer_format; // 1 = RGBA8888
```

For M1-M3 there is one kernel address space. When user processes arrive, the
kernel low-memory identity window must either be replicated in every process
page table with supervisor-only permissions, or the trap path must switch to a
separate kernel `PTBR`. Replicating the supervisor mappings is the simpler first
option.

The kernel installs its own vector table entries before relying on faults or
interrupts. The first milestone keeps the BIOS identity map; a later milestone
owns page-table construction.

## BIOS structure

The BIOS is a separate ROM component, versioned with the OS because it defines
the boot ABI:

```text
bios/entry.S    reset vector, privileged stubs (WRCR/IRET), low-stack setup
bios/bios.c     optional freestanding helpers: BootInfo, page tables, diagnostics
bios/README.md  build contract and ROM constraints
```

Assembly owns the reset path and anything that executes privileged instructions.
C is introduced only where it reduces complexity, starting with page-table and
BootInfo policy rather than the first boot stub.

BIOS constraints:

- The complete image must fit in the 64 KiB ROM at `0xFFFF_0000`.
- No host libc or standard `_start`; only freestanding C is allowed.
- Writable `.data`/`.bss` cannot live in ROM. BIOS state must be on the stack or
  in an explicitly reserved RAM block.
- `WRCR` and `IRET` remain assembly stubs even when the surrounding logic is C.
- The BIOS must move to a low mapped stack before enabling paging.

## Proposed layers

```text
bios/entry.S        reset vector, privileged stubs, final IRET
bios/bios.c         optional freestanding BootInfo/page-table helpers
boot/entry.S        kernel entry after the BIOS handoff
kernel/main.c       banner, subsystem init, shell loop
kernel/console.c    polled console output/input
kernel/traps.c      vector table, exception reports
kernel/shell.c      built-in commands
include/            shared headers and MMIO definitions
build/              generated artifacts (ignored)
```

## Kernel heap

The first allocator is a monotonic bump allocator over the RAM reported by
BootInfo:

```text
heap_start = first page after kernel .bss and reserved boot structures
heap_end   = RAM end minus the kernel stack guard region
```

`kmalloc` aligns the cursor and returns `null` when the region is exhausted.
`kfree` is a no-op initially. A free-list allocator with boundary tags and
coalescing replaces the implementation behind the same API once the kernel has
real allocation workloads.

## Syscall ABI

KR32 software vectors `32..255` are available through the vector table. The host
already uses `32..50` for its debug services, so the KR32OS ABI reserves a
separate range starting at vector `64` (`0x40`):

```text
vector 64..95   KR32OS kernel syscalls
R0..R3          arguments
R0              return value / negative error
```

The exact syscall numbers are assigned in a table and remain stable after the
first user program depends on them. Initial candidates are exit, console write,
console read, yield, and process identity.

Every user pointer is range-checked against the current user address space
before use. Kernel copy-in/copy-out helpers validate the entire range, handle
page boundaries, and never trust a user pointer directly.

The host's `32..50` vectors remain debug/host services and must not become part
of the OS ABI. The OS console path should use the MMIO console once devices are
attached to an OS boot; the host console is only a temporary bring-up aid.

## Serial console

The OS-facing tty is a separate MMIO device at `0xFFF0_5000`:

| Offset | Register |
| --- | --- |
| `0x00` | data: read consumes RX, write transmits TX |
| `0x04` | status: RX-ready, TX-ready, overrun |
| `0x08` | peek: next RX byte without consuming it; `0xFFFFFFFF` when empty |

The RX FIFO is 4 KiB. When full, new bytes are dropped and the overrun bit is
reported. The first shell polls status/peek; an RX interrupt comes later.

The device is raw: the shell performs line editing and echo. Ctrl-C, Ctrl-D, and
other control combinations are delivered as their byte values. Ctrl-V is the
one reserved UI shortcut and pastes clipboard text, normalized to LF.

The existing console at `0xFFF0_2000` remains a host/debug output path. The UI
console panel displays serial output and captures input while the pointer is
over the console panel. Printable keys, Enter, Backspace, and Ctrl-V are sent to
the guest; F-keys remain debugger shortcuts.

## User address space

```text
0x0000_0000..0x003F_FFFF  kernel low-memory window, user-inaccessible
0x0040_0000..            user image / heap
0x07E0_0000              user stack guard page
0x07E0_1000..0x07EF_FFFF  user stack (grows down from 0x07F0_0000)
0x07F0_0000              initial user stack top
```

The first user image is a flat binary copied from an embedded initrd blob in the
kernel image. It contains no relocations or dynamic loader. The user heap starts
after the image and must stop before the stack guard.

The physical page allocator is a bitmap with one bit per 4 KiB page. Kernel,
page-table, BootInfo, framebuffer, MMIO, ROM, and stack-guard pages are reserved
before any user page is allocated.

Early exceptions print the vector, PC, FAR, and error code to the serial console
and halt. Process termination and recovery are deferred until process state is
well-defined.

## Multicore policy

M0–M4 run on the boot core only. Secondary cores remain parked until the
scheduler, trap path, and one user process are stable. Multicore bring-up then
adds:

- per-core stacks and kernel state;
- a parked-core startup path;
- IPIs for scheduling and TLB/page-table coordination;
- per-core idle loops.

There is no core oversubscription in the first multicore milestone: one runnable
thread per core at a time.

## Proposed milestones

1. **M0 — scaffold:** build scripts, linker layout, boot stub, halt on entry.
2. **M1 — boot banner:** kernel entry at `0x0002_0000`, console output, clean halt.
3. **M2 — console + shell:** serial tty output, polled RX, prompt, `help`,
   `echo`, `clear`, `mem`.
4. **M3 — faults + timer:** vector table, exception diagnostics, timer IRQ.
5. **M4 — memory + processes:** bitmap page allocator, kernel heap, embedded
   initrd, first user-mode image.
6. **M5 — scheduler + multicore:** context switching, per-core state, IPIs, secondary cores.

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
kr32os/bios/entry.S       ROM reset path and privileged stubs
kr32os/bios/bios.c        optional freestanding BIOS helpers
kr32os/boot/entry.S       kernel entry
kr32os/kernel/*.c         compiled with kr32 cc -S
```

The orchestrator:

1. Compiles C sources to KR32 assembly with `kr32 cc -S`.
2. Assembles each generated `.s` and `entry.S` with `kr32 as --object`.
3. Links the kernel objects with `kr32 ld --flat --base 0x00020000`.
4. Builds `bios.bin` from `bios/entry.S` plus any `bios/*.s` at `0xFFFF0000`.
5. Runs with `kr32 os kernel.bin --bios bios.bin`.

## Booting from the CLI

`kr32 os` boots the default kernel and BIOS. Explicit images are also allowed:

```text
kr32 os
kr32 os path/to/kernel.bin --bios path/to/bios.bin
kr32 gui --os
```

The default kernel is resolved from `KR32OS_BIN`, then `kr32os.bin` next to the
executable, then `kr32os/build/kernel.bin` or `kr32os/build/kr32os.bin` in the
current directory, then an installed `share/kr32` directory. The BIOS defaults
to `KR32OS_BIOS`, the kernel's sibling `bios.bin`, or the same locations.

## Follow-up design questions

- What are the exact syscall numbers and initial call signatures?
- How large may the user image/heap grow before the stack guard?
- What block-device and filesystem model should follow the embedded initrd?
- What is the first scheduler quantum and preemption policy?
- When should the free-list allocator replace the bump allocator?
