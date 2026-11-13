# Devices, time, and multicore (M8)

> **Framebuffer v2 and the keyboard are implemented.** This document is
> normative for the shipped device contract; [display.md](display.md) keeps the
> host-presentation design that sits on top of it, and [terminal.md](terminal.md)
> specifies the host terminal that replaces the kernel console. See
> [README.md](README.md) for the active work streams.

## MMIO map

Devices live in the 960 KiB window at `0xFFF0_0000`:

| Base | Device | Page |
| --- | --- | --- |
| `0xFFF0_0000` | timer | 0 |
| `0xFFF0_1000` | block | 1 |
| `0xFFF0_2000` | console | 2 |
| `0xFFF0_3000` | IRQ router | 3 |
| `0xFFF0_4000` | framebuffer control | 4 |
| `0xFFF0_5000` | serial tty | 5 |
| `0xFFF0_6000` | keyboard | 6 |
| `0xFFF0_7000` | pointer (reserved, no device) | 7 |
| `0xFFF0_8000` | terminal (host-rendered console, see [terminal.md](terminal.md)) | 8 |
| `0xFFF0_9000` | graphics commands (host-rendered drawing, see [terminal.md](terminal.md) §3.1) | 9 |

Accesses go through the normal load/store path. `Machine` checks the device
window before RAM/ROM and forwards aligned 32-bit accesses to the `DeviceBus`;
byte accesses use a read/modify/write of the containing word.

## Serial tty

The serial device provides a raw byte stream for KR32OS:

| Offset | Register |
| --- | --- |
| `0x00` | data: read consumes RX, write transmits TX |
| `0x04` | status: bit 0 RX-ready, bit 1 TX-ready, bit 2 overrun |
| `0x08` | peek: next RX byte without consuming it; `0xFFFF_FFFF` when empty |

The RX queue is 4 KiB. If it fills, new bytes are dropped and the overrun bit is
set. The shell performs line editing and echo. The existing console device
remains available for host/debug output.

## Framebuffer

The 32 MiB aperture at `0xFC00_0000..0xFDFF_FFFF` is VRAM, not one surface: a
surface is `height` rows of `stride` bytes starting at a 4-byte-aligned offset
the guest chooses. The default 1920x1080 surface is 8,294,400 bytes
(`0x7E_9000`), so two default surfaces fit with room to spare. Off-screen
regions are scratch memory, not an error.

A pixel is one little-endian 32-bit word `0x00RRGGBB` (XRGB8888), so its bytes
in memory are `B, G, R, 0`. The top byte is ignored and the host presents
opaque.

Control registers live at `0xFFF0_4000`:

| Offset | Name | Access | Reset | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | `CONTROL` | RW | 0 | bit 0 enable; other bits ignored |
| `0x04` | `WIDTH` | RW | 1920 | 1..1920; a successful write sets `STRIDE = WIDTH*4` |
| `0x08` | `HEIGHT` | RW | 1080 | 1..1080 |
| `0x0C` | `STRIDE` | RW | 7680 | `WIDTH*4..7680`, multiple of 4 |
| `0x10` | `FORMAT` | RW | 1 | 1 = XRGB8888; nothing else accepted |
| `0x14` | `FRONT` | RW | 0 | 4-byte-aligned byte offset in VRAM |
| `0x18` | `BACK` | RW | `0x7E_9000` | 4-byte-aligned byte offset in VRAM |
| `0x1C` | `STATUS` | RO/W1C | 0 | bit 0 committed enable, bit 1 `MODE_OK`, bit 2 sticky `FLIP_ERROR` (write 1 to clear) |
| `0x20` | `FLIP` | WO | 0 | write bit 0 to commit; reads 0 |
| `0x24` | `FRAME` | RO | 0 | committed-frame counter |
| `0x28` | `ID` | RO | `0x4642_5231` | magic; compare the 32-bit word, not a byte string |

Writes never fault: an out-of-range value leaves the register unchanged, and
unknown offsets read zero and ignore writes. Reads return the written values,
except `STATUS`, which is computed.

`FLIP` is the only transition. Writing bit 0 validates the bundle (`WIDTH`,
`HEIGHT`, `FORMAT`, `STRIDE` alignment, and `FRONT`/`BACK` plus
`HEIGHT*STRIDE` within 32 MiB, in 64-bit arithmetic). On success the committed
frame becomes `{CONTROL.bit0, WIDTH, HEIGHT, STRIDE, FORMAT, BACK}` — the
surface the guest just drew — then `FRONT` and `BACK` swap and `FRAME`
increments. On failure `FLIP_ERROR` is set and nothing else changes. A flip
while disabled commits `ENABLED = 0`.

Reset restores control 0, 1920x1080, stride 7680, format 1, front 0, back
`0x7E_9000`, frame counter 0, no error, and clears VRAM.

Hosts read the committed frame through `frame()` and `frame_pixels()` in
`include/kr32/core/devices.h`. There is no dirty-rectangle channel. The host
also *writes* this VRAM on behalf of the terminal and graphics-command devices:
their renderer paints into the same back surface the guest sees, so text, host
drawing, and guest pixel writes all target the same memory and compose.

## Keyboard

Page 6 at `0xFFF0_6000`:

| Offset | Name | Access | Reset | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | `DATA` | RO | 0 | pop one event word; 0 when empty |
| `0x04` | `STATUS` | RO/W1C | 0 | bit 0 available, bit 1 overrun (write 1 to clear), bits 8..15 count |
| `0x08` | `CONTROL` | RW | 0 | bit 0 IRQ enable |
| `0x0C` | `IRQ` | RW | 0 | IRQ line 0..15 |

An event word carries ASCII in bits 0..7 (0 when the key has no character),
modifiers in 8..15 (shift, control, alt, caps), a KR32 scancode in 16..23, and
the type in 24..31 (0 down, 1 up). The FIFO holds 64 events; the 65th is
dropped and sets overrun. The IRQ line is asserted while the FIFO is non-empty
and `CONTROL.bit0` is set. Page 7 (`0xFFF0_7000`) is reserved for the pointer
and has no device behind it.

## Timer

| Offset | Register |
| --- | --- |
| 0x00 | control: bit 0 enable, bit 1 periodic, bit 2 IRQ enable |
| 0x04 | reload (ticks) |
| 0x08 | remaining count |
| 0x0C | status bit 0 pending (write 1 clears) |
| 0x10 | IRQ line 0..15 |

`DeviceBus::tick()` decrements the timer once per tick. A `Machine` attached
with `tick_devices = true` ticks devices after every retired instruction, while
`Cluster` attaches with `false` and ticks once per cluster step. The two
periods are documented here because they differ; [display.md](display.md)
recommends stating one rule for all attach modes. When the count reaches zero
the timer sets pending and, if enabled, raises the configured line.

## Block

| Offset | Register |
| --- | --- |
| 0x00 | guest physical DMA address |
| 0x04 | byte count |
| 0x08 | command: bit 0 start, bit 1 direction (1 = guest to block), bit 2 IRQ enable |
| 0x0C | status: bit 0 done, bit 1 busy, bit 2 error |
| 0x10 | IRQ line |
| 0x14 | data window byte |

The backing store is 4 KiB. A start command copies through
`Machine::read/write_physical8`; on completion the configured IRQ line is
raised. The current model completes immediately, which keeps tests
deterministic.

## Debug console (page 2)

The host debug console, not the guest screen. Writing offset 0 emits the low
byte to `DeviceBus::console()`; reading offset 0 returns the last byte and
offset 4 reports readable status. The GUI and host console can consume the same
output. The guest screen is the terminal device on page 8.

## Terminal (page 8)

`0xFFF0_8000`. The host owns the character grid, font, palette, scrollback ring,
and rendering; the guest writes characters. The normative register table and
semantics are in [terminal.md](terminal.md) §2:

| Offset | Register | Access |
| --- | --- | --- |
| `0x00` | `DATA` (write one character) | WO |
| `0x04` | `STATUS` (ready, columns, rows) | RO |
| `0x08` | `CONTROL` (cursor visible, hidden) | RW |
| `0x0C` | `COMMAND` (1 clear, 2 reset) | WO |
| `0x10` | `COLOR` (foreground and background indices) | RW |
| `0x14` | `ID` = `0x5445_524D` | RO |

With the terminal present the guest console does no pixel work; writes render in
the host.

## Graphics commands (page 9)

`0xFFF0_9000`. Host-executed drawing so programs never rasterise: `SET_CLIP`,
`SET_COLOR`, `FILL_RECT`, `COPY_RECT`, `DRAW_LINE`, `DRAW_TEXT`, and `FLIP`,
submitted through immediate registers in v1. The command set, parameters, and
validation rules are in [terminal.md](terminal.md) §3.1.

## IRQ routing

The router has 16 route words at `0xFFF0_3000 + line * 4`; bit *n* delivers the
line to core *n*. `Cluster` installs a route handler that calls
`Machine::raise_interrupt` on every selected core. An unconfigured line routes
to core 0.

## Multicore

`Cluster` gives every core a distinct `COREID` and one shared RAM image. It
advances one core per `step()` in round-robin order. Writes increment a
per-core generation; when a write happens, other cores' LL/SC reservations are
invalidated. `LL`/`SC` loops therefore retry across cores, and repeated runs
produce the same counter value.

`send_ipi(target, line)` is the minimal inter-processor interrupt hook.

## Current limits

The machine tab displays the current core id and control state; a full
multi-core selector/per-core panel layout is a UI follow-up. DMA is
synchronous rather than tick-scheduled.

The framebuffer is presented by the `kr32 display` window and the debugger's
`SCREEN` tab; the committed frame is the only host-visible state. The 32 MiB
aperture is reached through the device bus one word at a time, so bulk pixel
stores are slow; the terminal (page 8) and graphics-command (page 9) devices are
the fast path because the host does the pixel work. Direct guest pixel writes
remain available for programs that want them. The pointer page is reserved and
has no device behind it.
