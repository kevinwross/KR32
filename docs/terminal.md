# Host terminal service

**Status:** implemented 2026-11-13, tagged `m10.5`. The terminal device (page 8),
the graphics command device (page 9), the kernel driver, and the font atlas are
in; [implementation.md](implementation.md) records the deviation that the
renderer primitives live in `GraphicsDevice` rather than a separate class.

**Decisions taken 2026-11-13:** 10x20 cells (80x30 grid); one host renderer
shared with a graphics command device; console is a host-side client of that
renderer and draws last, with an explicit hide/show; immediate-register graphics
commands first, a FIFO only if measured; no escape sequences in v1; scrollback
stays internal; the work is its own milestone before M11.

**Owner:** display/hardware stream. The device contract here is normative once
implemented; [display.md](display.md) §11 describes the console this replaces,
and [graphics.md](graphics.md) §7 defines the `con_*` API whose implementation
changes.

## 1. Why

The kernel console moves framebuffer-sized data on the emulated CPU. A scroll
repaints all 3,700 cells (473,600 pixels); a clear fills 480,000 pixels. At the
measured emulator speed of roughly 1M ticks per human second, and with a floor
of about four instructions per pixel even in hand-written assembly, a scroll
cannot cost less than about 1.9M ticks — two seconds — before any text is
drawn.

Measured on this machine, with the console implemented in the kernel:

| Workload | Ticks | Wall clock at 1M ticks/s |
| --- | --- | --- |
| 8 echoed commands, no scroll | 4.52M | ~4.5 s |
| 41 lines (18 echoed commands) | >60M (did not finish) | >60 s |
| 41 lines, with view panning prototyped | 10.0M | ~10 s |
| 85 lines, with view panning prototyped | 45.0M | ~45 s |

The host does about 4.5G ticks per second. Moving the same work there makes it
imperceptible. No guest-side optimisation closes a gap of that size: the panning
prototype already removed every whole-screen redraw and still cost about 0.9M
ticks per scroll.

**Decision:** the console becomes a **terminal device**. The guest writes
characters to a device page; the host owns the grid, the font, the palette, the
scrollback ring, and the rendering into the framebuffer. The guest never touches
a pixel.

## 2. Device contract

New MMIO page 8 at `0xFFF0_8000`:

| Offset | Name | Access | Reset | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | `DATA` | WO | 0 | Write one character; the low 8 bits are used |
| `0x04` | `STATUS` | RO | | Bit 0 `READY` (always set), bits 8..15 columns, bits 16..23 rows |
| `0x08` | `CONTROL` | RW | 0 | Bit 0 cursor visible, bit 1 console hidden |
| `0x0C` | `COMMAND` | WO | 0 | 1 clear screen, 2 full reset |
| `0x10` | `COLOR` | RW | `0x07` | Bits 0..3 foreground palette index, bits 4..7 background |
| `0x14` | `ID` | RO | `0x5445_524D` | Magic, "TERM" |

- Data writes render immediately in the host and never fault; values above
  `0xFF` are truncated. Unknown offsets read zero and ignore writes.
- `COMMAND` 1 clears the grid and homes the cursor; 2 also resets the palette
  and `CONTROL`.
- Control characters follow the current console: `0x0A` newline (scrolling when
  on the last row), `0x0D` carriage return, `0x08` backspace, `0x09` tab to the
  next multiple of eight, `0x0C` clear. Other values below `0x20` and `0x7F`
  are ignored. No escape sequences in v1.
- The terminal derives `columns` and `rows` from the active framebuffer mode
  and the chosen cell size: 800x600 with **10x20 cells gives 80x30**. A later
  mode change resets the grid. With no framebuffer, writes are still accepted
  and the grid still runs; nothing is rendered.
- While `CONTROL` bit 1 is set the terminal keeps its grid up to date but does
  not draw; graphics owns the screen. Clearing the bit redraws the whole grid.

## 3. Rendering model

- **One renderer** in the host (`Renderer`: surface, clip, palette, fill, copy,
  line, text blit) serves both the terminal and the graphics command device.
  The terminal is a client of it: it keeps the grid and, per line or scroll,
  emits a handful of renderer calls — it does not call the guest-facing graphics
  device.
- The terminal keeps a 256-line scrollback ring, the 16-entry palette (the
  values currently in `console.c`), and the **10x20 font table**. The font is
  the same JetBrains Mono face, rasterised by `tools/font8x16` at a larger cell
  box; the current 8x16 table remains available as an optional small mode.
- After each write the renderer repaints the affected cells into the framebuffer
  device's back surface; a scroll repaints the window. That work is native C++
  and invisible to the guest.
- The guest still controls presentation: `console_present()` flips the
  framebuffer as today, so `kr32 display`, the debugger's `SCREEN` tab, and
  `--screen-shot` see ordinary `FLIP`s and committed frames.
- **Compositing:** graphics and text share the back buffer. The console draws
  last, on top; a full-screen graphics program hides it through `CONTROL` bit 1.

### 3.1 Graphics command device (companion)

A second MMIO page (page 9) exposes host drawing so programs never rasterise:

| Command | Parameters |
| --- | --- |
| `SET_CLIP` | x, y, width, height |
| `SET_COLOR` | foreground, background indices |
| `FILL_RECT` | x, y, width, height, colour |
| `COPY_RECT` | source offset, destination x/y/width/height |
| `DRAW_LINE` | x0, y0, x1, y1, colour |
| `DRAW_TEXT` | x, y, pointer, length, foreground, background |
| `FLIP` | — |
| `SET_SURFACE` | offset, width, height, stride |
| `COPY_RECT_STRIDED` | source, source stride, x, y, width, height |

v1 is **immediate registers**: write the opcode and its parameters, then ring a
doorbell; the host executes and validates. A command FIFO in VRAM replaces this
only if a program needs thousands of primitives per frame. Every command clamps
to the surface and clip; the host is the trust boundary.

## 4. What the guest keeps

| Guest side | Change |
| --- | --- |
| `console_init` | Probe `ID`, write 2 to `COMMAND`, write `COLOR`; still calls `fb_mode(800, 600)` |
| `console_putc` | Write the character to `DATA`; the serial mirror stays |
| `console_write`, `console_write_uint` | Unchanged wrappers over `console_putc` |
| `console_clear` | Write 1 to `COMMAND` |
| `console_present` | `fb_present()`; no drawing |
| `console_getc`, `con_readline` | Unchanged; input stays in the guest |
| `font8x16.c`, grid arrays, dirty tracking, `draw_cell`, palette and scroll code | **Deleted** |
| `con_set_colors` | Write `COLOR` |

`console.c` drops from about 430 lines to a thin driver; the kernel image
shrinks by the font table and the console code.

## 5. Alternatives considered

| Option | Guest cost per scroll | Verdict |
| --- | --- | --- |
| Kernel rendering (today) | ~9-15M ticks | Unusable; measured |
| View panning in a taller backing store (prototyped) | ~0.9M ticks | Better, still about one second; large kernel complexity |
| Device-side scroll, guest redraws the new row | ~0.25M ticks | Acceptable for scrolling, but every full repaint still crosses the bus |
| **Host terminal + host renderer** | **tens of ticks per character; no pixel work** | Chosen |

The price is pedagogical: the console implementation leaves the OS. The
counter-argument is that a terminal is what a teaching machine should talk to;
the framebuffer stays available for graphics programs.

## 6. Interaction with the rest of the system

- **Framebuffer:** unchanged. The renderer writes into its VRAM through the
  device's own surface, and `FLIP` semantics are untouched.
- **Keyboard:** unchanged in v1. Cooked line input could come later.
- **Debugger:** the `SCREEN` tab shows the same committed frames; hover and
  focus are unaffected.
- **Serial:** the mirror in `console_putc` stays, so transcripts and the
  `run_os_*` tests are unchanged.

## 7. Migration

Landed as its own milestone before M11 (`implementation.md` records it as
M10.5).

1. Host renderer and terminal device with grid, 10x20 font, palette, scrollback,
   and rendering; host unit tests.
2. Graphics command device (immediate registers) reusing the renderer.
3. `kr32os/kernel/console.c` reduced to the device driver; `font8x16.c` dropped
   from both build scripts and the link.
4. Embedded OS images regenerated; `os_console_screen` and `run_os_*` stay
   green; the boot screen was checked visually at 80x30.
5. Documentation: `devices.md` gained pages 8 and 9, `display.md` §7 points
   here, and `graphics.md` records the host-execution model.

## 8. Open questions

- Palette register granularity: one `COLOR` register covers `con_set_colors`;
  a per-entry palette register can come with SGR.
- FIFO trigger: adopt the command FIFO only when a real program's primitive
  count justifies it.
- Scrollback: the host keeps the ring; exposing it is deferred.
- Escape sequences: no v1; ANSI SGR is the natural v2.
- Whether to also ship the 8x16 mode, and how a mode change interacts with the
  graphics device's surface.

## 9. Test plan

Implemented in `tests/unit/test_terminal.cpp` and
`tests/unit/test_graphics.cpp`:

- Host unit tests: putc and wrapping at 80x30, scroll, clear, control
  characters, colours, hide/show, geometry, no-framebuffer behaviour, and the
  `ID` word.
- Graphics device tests per primitive: fill, copy, line, text, clip, validation.
- `os_console_screen`: the committed frame is 800x600, contains both palette
  colours, and is identical across two runs.
- `run_os_*`: serial transcripts unchanged; the shell and user programs still
  run.
- A screenshot after more than 30 lines of output, to confirm scrolling.
