# Devices, time, and multicore (M8)

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

The 32 MiB framebuffer aperture lives at `0xFC00_0000..0xFDFF_FFFF`, separate
from the MMIO window:

| Base | Contents |
| --- | --- |
| `0xFC00_0000` | front surface: 1920×1080, 32-bit RGBA, 7680-byte stride |
| `0xFC7E_9000` | back surface |

Pixel accesses use normal little-endian 32-bit storage. Control registers are
in the MMIO page at `0xFFF0_4000`:

| Offset | Register |
| --- | --- |
| `0x00` | enable |
| `0x04` | width |
| `0x08` | height |
| `0x0C` | stride |
| `0x10` | format (`1` = RGBA8888) |
| `0x14` | front-buffer offset |
| `0x18` | back-buffer offset |
| `0x1C` | status |

## Timer

| Offset | Register |
| --- | --- |
| 0x00 | control: bit 0 enable, bit 1 periodic, bit 2 IRQ enable |
| 0x04 | reload (ticks) |
| 0x08 | remaining count |
| 0x0C | status bit 0 pending (write 1 clears) |
| 0x10 | IRQ line 0..15 |

`DeviceBus::tick()` decrements the timer once per scheduler turn. When the
count reaches zero it sets pending and, if enabled, raises the configured line.

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

## Console

Writing offset 0 emits the low byte to `DeviceBus::console()`; reading offset 0
returns the last byte and offset 4 reports readable status. The GUI and host
console can consume the same output.

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
