# KR32 documentation index

Start here, then follow the topic.

| Document | Contents |
| --- | --- |
| [display.md](display.md) | **Proposal.** Display memory, framebuffer v2 registers, host presentation, the windowed system console, and the keyboard device. |
| [graphics.md](graphics.md) | **Proposal.** The graphics and console API presented to programmers writing KR32 programs. |
| [devices.md](devices.md) | Implemented v1 devices: MMIO map, timer, block, console, serial, framebuffer, IRQ routing, multicore. |
| [isa-v1.md](isa-v1.md) | Instruction encodings and the machine memory map. |
| [abi.md](abi.md) | Guest ABI: calling convention, thread control block, structures. |
| [ui.md](ui.md) | Windowed debugger layout, panels, controls, and headless rendering. |
| [host.md](host.md) | Host syscalls, file sandbox, shell, and DAP. |
| [container.md](container.md) | `.k32` object/executable format. |
| [libc.md](libc.md) | Freestanding C libc and runtime. |
| [templates.md](templates.md) | C++ template support and limits. |
| [optimizations.md](optimizations.md) | `-O1` peephole passes, safety model, and optimized test runs. |
| [roadmap.md](roadmap.md) | Compiler roadmap status. |
| [testing.md](testing.md) | Test layout, fast edit loop, and full-suite cadence. |
| [debug.md](debug.md) | Debug information format. |

## Active work streams

Two documentation work streams are active at the same time. They touch
different files so they can proceed independently.

| Stream | Scope | Files |
| --- | --- | --- |
| Display / hardware spec | Display memory, framebuffer contract, host presentation, system console, keyboard, graphics API | `docs/display.md`, `docs/graphics.md`, `docs/devices.md` |
| Compiler / libc | C and C++ front ends, code generation, guest headers, libc, C++ library wrappers | `src/cc/`, `src/cxx/`, `docs/libc.md`, `docs/templates.md`, `docs/roadmap.md`, `cpp-impl.md`, `cpp-handoff.md` |

**Coordination note for the compiler/libc stream.** The display stream is
working independently on the spec in `docs/display.md` and `docs/graphics.md`.
It does not edit compiler sources. One item in `docs/graphics.md` needs the
compiler stream eventually: guest headers are synthesized inside the C and C++
front ends (see the `#include` handling in `src/cc/cc.cpp` and
`src/cxx/preprocessor.cpp`), so a `<kr32/gfx.h>` header has to be added there.
That is explicitly deferred; `docs/graphics.md` records the exact declarations
so the work can be picked up without redesign.

If you need to change a display device contract, do not edit the specification
in place. Add a note under **Open questions** at the end of `docs/display.md`
(or `docs/graphics.md`) and mention it here. That keeps one owner per contract
and avoids two sessions editing the same normative table.
