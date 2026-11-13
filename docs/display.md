# Display, console, and keyboard

**Status:** implemented. Framebuffer v2, the keyboard device, the host surface,
and the framebuffer console shipped on 2026-11-12. The device contract is now
normative in [devices.md](devices.md); this document keeps the design rationale
and specifies the host presentation model, the windowed console, and the
keyboard mapping that sit on top of it.

**Owner:** display/hardware stream. The C/C++ compiler and libc stream works
independently in `src/cc/`, `src/cxx/`, and its own docs; see
[README.md](README.md). Do not edit this specification in place from another
stream. Add a note under **Open questions** instead.

## 1. Summary

The framebuffer exists in the emulator but nothing displays it and no guest
draws into it. This proposal turns it into the primary output device and gives
the machine its first input device.

The first client is the **system console**: a text console that KR32OS renders
into the framebuffer with a bitmap font and that the host shows in a window.
The existing serial tty remains as a debug/log transport and as the headless
test transport, so shell tests keep working without a window.

Decisions at a glance:

| Decision | Choice | Reason |
| --- | --- | --- |
| Display memory | 32 MiB VRAM at `0xFC00_0000`; surfaces are byte offsets inside it | Off-screen surfaces for free; no per-surface allocation |
| Active mode | 320x200 through 1920x1080, default 800x600 | Fits a window 1:1; 100 console columns at 8 px, 37 rows at 16 px |
| Pixel format | XRGB8888, word `0x00RRGGBB` | Matches `ui::Canvas` (`0xAARRGGBB`) and SDL `ARGB8888`; one copy |
| Presentation | Commit on `FLIP`; host reads only the committed frame | No torn modes, cheap host update |
| Host update | Keyed on a committed-frame counter | No 8 MB scan per UI frame |
| Bad configuration | Ignored register writes; `FLIP` sets a sticky error | MMIO never faults; easy to probe and debug |
| Console | Kernel-rendered text grid in the framebuffer | Exercises the display; no host-side terminal emulation |
| Input | Keyboard device at `0xFFF0_6000`, packed 32-bit events | Extensible to pointer later; same device pattern |

## 2. What exists today

Implemented v1 (see [devices.md](devices.md)):

- 32 MiB aperture at `0xFC00_0000`, backed by a `std::vector<std::uint8_t>`.
- Control page at `0xFFF0_4000`: enable, width, height, stride, format,
  front, back, status.
- Front and back are ordinary offsets. Nothing swaps them, `STATUS` only
  reports enable, and there is no frame counter.
- Every pixel access is routed through `DeviceBus` and a virtual call; a byte
  store becomes a read/modify/write of the containing word.
- `BootInfo` advertises the aperture, and the BIOS maps its pages writable,
  non-executable, supervisor-only.
- No host code reads the pixels, so the aperture is currently invisible.

What this proposal changes:

1. The register contract (commit/flip/status/format/bounds).
2. The host side, which gains a screen surface and a display-only window.
3. The guest side, which gains a text console and a keyboard.
4. The memory path, later, so bulk pixel stores do not pay a device call.

## 3. Design rules

1. **The guest-visible contract is the product.** The register table below is
   normative. The emulator and KR32OS are two implementations of it and must
   agree on ordering, reset state, and error behavior.
2. **Aperture memory is not MMIO.** Pixel memory is bulk storage with no
   per-access side effects. It must behave like RAM (byte, halfword, word,
   unaligned, read-back) and may later get a direct fast path. Only the 4 KiB
   control page is MMIO.
3. **The core does not know the host exists.** No Canvas, SDL, or window types
   in `kr32_core`. The device exposes a committed frame; the host pulls it.
4. **The host presents only committed frames.** Register writes are the guest's
   pending configuration. A frame is coherent after `FLIP`.
5. **Time is instruction count.** No wall clock, no host callbacks. Async work
   is a state machine advanced from `tick()`, even when it completes at once.
6. **Guest values are hostile.** Range-check every write, ignore invalid ones,
   use 64-bit arithmetic for offset+bounds, and never let a guest crash the
   host.
7. **Presentation follows guest intent.** The host copies on a committed-frame
   change, not on a timer.
8. **Everything is testable headlessly.** If a feature cannot be asserted
   without a window, the split is wrong.

## 4. Display memory model

The 32 MiB aperture is **VRAM**, not a single surface:

```text
0xFC00_0000  +--------------------------------------------------+
             | VRAM, 32 MiB, byte addressable, little-endian     |
             |   surface 0 (front)  <- offset 0x0000_0000        |
             |   surface 1 (back)   <- offset 0x007E_9000        |
             |   free off-screen VRAM                            |
             +--------------------------------------------------+
0xFDFF_FFFF
```

- A **surface** is a rectangle of `height` rows, each `stride` bytes, starting
  at a 4-byte-aligned byte offset inside VRAM. Nothing is reserved: the guest
  chooses offsets and may overlap surfaces (its problem, not the device's).
- The default 1920x1080 surface is `1920 * 1080 * 4 = 8_294_400` bytes
  (`0x007E_9000`), so VRAM holds two default surfaces plus ~15.4 MiB free.
- `FRONT` and `BACK` name two surfaces. The guest draws into `BACK`, calls
  `FLIP`, and the back surface becomes the front.
- Pixel `(x, y)` of the front surface is at `base + FRONT + y*STRIDE + x*4`.
- Access anywhere inside the aperture is legal. Access outside the active
  surface but inside VRAM is **not** an error: it is scratch memory.
- Access outside the aperture faults (general protection), as it does today.

A mode is therefore `(width, height, stride, format, front, back)` plus the
enable bit. Different modes can use different offsets; a guest is free to keep
several surface pairs for different purposes.

## 5. Frame buffer control registers (v2)

The normative register table is in [devices.md](devices.md). This section keeps
the design rationale and the commit/validation reasoning; if the two disagree,
`devices.md` wins and this section is the one to fix.

Control page base `0xFFF0_4000`. Registers are 32-bit, little-endian.
Offsets not listed read as zero and ignore writes.

| Offset | Name | Access | Reset | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | `CONTROL` | RW | `0` | bit 0: display enable. Other bits reserved, write ignored. |
| `0x04` | `WIDTH` | RW | `1920` | Active pixels, `1..1920`. A successful write also sets `STRIDE = WIDTH*4`. |
| `0x08` | `HEIGHT` | RW | `1080` | Active lines, `1..1080`. |
| `0x0C` | `STRIDE` | RW | `7680` | Bytes per line, `WIDTH*4..7680`, multiple of 4. |
| `0x10` | `FORMAT` | RW | `1` | `1` = XRGB8888. Other values ignored. |
| `0x14` | `FRONT` | RW | `0` | Byte offset of the front surface, 4-byte aligned, within VRAM. |
| `0x18` | `BACK` | RW | `0x007E_9000` | Byte offset of the back surface, 4-byte aligned, within VRAM. |
| `0x1C` | `STATUS` | RO/W1C | `0` | bit 0: committed enable. bit 1: current registers form a valid mode. bit 2: sticky `FLIP_ERROR`, write 1 to clear. |
| `0x20` | `FLIP` | WO | `0` | Write bit 0 set to commit and swap. Reads as 0. |
| `0x24` | `FRAME` | RO | `0` | Committed-frame counter; increments on every successful flip. |
| `0x28` | `ID` | RO | `0x4642_5231` | Magic. Guests compare the 32-bit word; the bytes in memory are `31 52 42 46` (`1RBF`), so a byte compare against `FBR1` fails. |

`WIDTH` and `HEIGHT` maxima are the 1920x1080 surface capacity, not a
recommendation. The default mode for the console is 800x600 (section 7).

### 5.1 Register write rules

Writes never fault and never partially apply:

- `CONTROL`: bits other than 0 are ignored.
- `WIDTH`: accepted only for `1..1920`; on accept, `STRIDE` becomes `WIDTH*4`.
  Out of range: ignored, register keeps its value.
- `HEIGHT`: accepted only for `1..1080`; out of range ignored.
- `STRIDE`: accepted only for `WIDTH*4..7680` and `STRIDE % 4 == 0`; otherwise
  ignored. This is the only way to get a padded stride; write `WIDTH` first.
- `FORMAT`: only `1` is accepted.
- `FRONT`/`BACK`: accepted only if 4-byte aligned and `< 32 MiB`; otherwise
  ignored. Whole-surface fit is checked at `FLIP`, not per write.
- `STATUS`: only bit 2 is writable (write 1 to clear `FLIP_ERROR`).
- Unlisted offsets in the page: reads return 0, writes are ignored.

The general principle: a write either takes effect completely or not at all,
and a guest can always read back what it wrote. Reads return the written
(pending) values, not the committed mode.

### 5.2 `STATUS`

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `ENABLED` | Value of `CONTROL.bit0` in the committed frame. |
| 1 | `MODE_OK` | The current register set would be accepted by `FLIP`. Computed on read. |
| 2 | `FLIP_ERROR` | Sticky: the last `FLIP` was rejected. Write 1 to clear. |
| 3..31 | — | zero |

### 5.3 `FLIP` and commit

`FLIP` is the single transition point. Writing bit 0 set performs:

1. Validate the current registers as a bundle:
   - `WIDTH` in `1..1920`, `HEIGHT` in `1..1080`, `FORMAT == 1`;
   - `STRIDE >= WIDTH*4`, `STRIDE <= 7680`, `STRIDE % 4 == 0`;
   - `FRONT % 4 == 0`, `BACK % 4 == 0`;
   - `FRONT + HEIGHT*STRIDE <= 32 MiB`, `BACK + HEIGHT*STRIDE <= 32 MiB`
     (64-bit arithmetic; the 32-bit fields cannot overflow the sum).
2. On failure: set `STATUS.FLIP_ERROR`, leave the committed frame, the
   registers, and `FRAME` untouched. The display keeps showing the previous
   committed frame.
3. On success: the committed frame becomes
   `{CONTROL.bit0, WIDTH, HEIGHT, STRIDE, FORMAT, BACK}` — the surface the
   guest just drew; then swap the `FRONT` and `BACK` registers; then increment
   `FRAME`; clear `FLIP_ERROR`.

After a successful flip, reading `FRONT` returns the offset that was just
displayed and `BACK` returns the surface the guest should draw into next.

Writing `FLIP` with bit 0 clear is a no-op. The register reads as 0.

Notes:

- Flipping while disabled is legal and commits `ENABLED = 0`; the host blanks.
  This is how the guest turns the display off in-band.
- Flipping front and back to the same offset is legal; `FRAME` still
  increments.
- `FRAME` increments only on success, so a rejected flip is invisible to the
  host. `FRAME` wraps at `2^32`; hosts compare for inequality, never order.

### 5.4 Pixel format

`FORMAT = 1` is **XRGB8888**:

- A pixel is one 32-bit word `0x00RRGGBB`.
- Stored little-endian, so the four bytes in memory are `B, G, R, 0`.
- The top byte (X) is ignored by the host, which always presents opaque.
  Guests should write zero there.
- There is no alpha, no hardware palette, and no packed 16-bit mode in v1. The
  console's 16-entry palette (section 7.2) is software: it expands 4-bit indices
  to these XRGB words before writing pixels.

This is deliberately the same layout as `ui::Canvas` pixels (`0xAARRGGBB`) and
SDL's `ARGB8888` texture, so presenting a row is a masked copy rather than a
channel shuffle. The earlier "RGBA8888" wording in `devices.md` was ambiguous
about byte order; this section replaces it.

### 5.5 Reset state

`DeviceBus::reset()` restores: `CONTROL = 0`, 1920x1080 mode, `STRIDE = 7680`,
`FORMAT = 1`, `FRONT = 0`, `BACK = 0x007E_9000`, committed frame = disabled
with those geometry values, `FRAME = 0`, `FLIP_ERROR = 0`, and clears VRAM.

### 5.6 Ordering and multicore

- `FLIP` is an ordinary store. A guest that wrote pixels from another core
  must execute `FENCE` before flipping if it needs those writes visible.
- Concurrent flips are serialized by the bus but their order is scheduler
  order. A multi-core guest that cares must use a lock.
- The emulator's host thread is single-threaded, so presentation never races
  with guest execution: the host samples when the guest is stopped between
  instructions.

## 6. Host presentation contract

### 6.1 Device-side view

The device exposes exactly one committed frame:

```cpp
struct DisplayFrame {
    bool enabled;
    std::uint32_t offset;   // byte offset into VRAM
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    std::uint32_t format;   // 1 = XRGB8888
    std::uint32_t number;   // committed-frame counter
};

[[nodiscard]] const DisplayFrame& frame() const;
[[nodiscard]] std::span<const std::uint8_t> frame_pixels() const;
```

`frame_pixels()` is `height * stride` bytes starting at
`kFramebufferBase + offset`. It is valid whether or not `enabled` is set; the
host decides what to paint when disabled. The host must not read `WIDTH`,
`FRONT`, or other live registers, and there is no dirty-rectangle channel in
v1: `fb_present_rect` (graphics.md §3.1) is a guest-side hint reserved for a
later host.

Hosts copy the whole committed frame when `number` changes, when the geometry
changes, on first show, and after a reset. A 32-bit wrap is harmless for
equality comparison.

### 6.2 Display-only window (first host target)

New command:

```text
kr32 display [image.k32]           # guest program, no debugger
kr32 display --os                  # boot KR32OS
```

- One window, sized to the committed mode once one exists and otherwise
  800x600, resized when a committed frame changes geometry; 1:1 pixels.
- Runs instructions in slices until the window closes; presents only when
  `number` changes.
- The guest always has keyboard focus.
- `Esc` quits. `F2` cycles 1x/2x/3x integer zoom. There is no debugger UI.
- If the guest halts, stop stepping but keep the window and event loop alive so
  the final frame stays visible; `Esc` still quits.
- If the guest never flips, the window shows "no frame".

This is the "shell in a window" deliverable. It is intentionally separate from
the debugger so the console can be used as a normal terminal.

### 6.3 Debugger screen pane (second host target)

The debugger keeps the existing `CONSOLE` panel for the serial tty and gains a
`SCREEN` view for the framebuffer.

- A tab strip over the source/disassembly region: `SOURCE | SCREEN`.
  `SCREEN` shows the committed frame with integer scaling when it fits, and
  fit-to-pane with nearest-neighbor sampling when it does not, centered and
  letterboxed.
- Title: `SCREEN 800x600  FRAME 42`, and `DISPLAY OFF` when disabled.
- Click the pane to give the guest keyboard focus; the title shows a green dot
  while focused. Clicking elsewhere in the debugger releases focus. `Esc`
  releases focus first and only quits when focus is released.
- Debugger keys (`F5`, `F6`, `F9`-`F11`) always reach the debugger even while
  the screen is focused.
- Hovering a pixel shows: screen `(x, y)`, guest address
  `0xFC00_0000 + FRONT + y*STRIDE + x*4`, and the pixel as hex and RGB.
  Clicking it points the memory panel at that address. This is the feature that
  connects what is drawn to what the machine stored.
- The `CONSOLE` panel keeps its current hover-to-type behavior for serial.
  Click-to-focus for `SCREEN` is deliberate: accidental keystrokes should not
  disappear into the guest.

Layout impact: no new permanent panel. `SCREEN` replaces the top-left region
while selected, so an 800x600 frame fits near 1:1 at 1440x900.

### 6.4 Headless screenshots

Two artifacts:

- `kr32 gui --headless ... --screenshot X.ppm` keeps its current meaning and
  includes the `SCREEN` tab when it is active.
- `--screen-shot X.ppm` writes only the committed frame, as RGB rows, without
  debugger chrome. This is the golden-frame artifact for tests.

Both must be deterministic: same instruction stream, same file bytes, same
hash.

### 6.5 Platform layer

`platform::Window::present` currently takes a `ui::Canvas`. Add a second
entry point for a raw surface:

```cpp
void present_surface(const std::uint8_t* vram, int width, int height,
                     int stride, int scale);
```

It creates or reuses one streaming texture, uploads `height` rows (honoring
stride), and renders with integer scaling. The Canvas path is unchanged, so
the debugger and display-only window share the window, event, and texture
plumbing.

## 7. System console in a window

### 7.1 Why the guest renders it

The console could be emulated on the host by parsing serial output. That would
be less work and would not exercise the display. We take the guest-side path
because the console is the first real client of the framebuffer, it forces the
pixel format, mode, and flip contract to be usable, and it gives KR32OS a real
screen to grow a graphics API on.

### 7.2 Architecture

```text
kr32os shell (transport-agnostic)
        |
   line discipline (echo, editing, history)
        |
   +----+-------------------+
   |                        |
serial console          framebuffer console
(0xFFF0_5000)           (VRAM + 8x16 font)
   |                        |
CONSOLE panel           SCREEN pane / display window
```

- **Text grid.** `cols = width / 8`, `rows = height / 16`. At 800x600 that is
  100x37. The grid is the source of truth; the renderer paints cells. A cell is
  two bytes: the character (7 bits, `0x20..0x7E`) and a colour byte holding a
  4-bit foreground index and a 4-bit background index.
- **Renderer.** For each dirty cell: fill the 8x16 background and blit the
  glyph. Then draw the cursor cell. Then `FLIP`.
- **Dirty tracking.** Keep a dirty rectangle in cell coordinates. Writes mark
  cells dirty; `present` redraws that rectangle, flips, and clears it. A
  screen clear or scroll marks the whole grid.
- **Scrolling.** When the cursor passes the last row, move the grid up one row,
  clear the last row, mark the full grid dirty.
- **Scrollback.** The kernel keeps the last 256 lines in a ring buffer owned by
  `con_state`, so history is identical in the display window and the debugger.
  Host-side capture is not used; D4 adds navigation.
- **Cursor.** A filled block drawn in inverse video over the cell. No blink in
  v1: blinking needs a timer and makes frame hashes time-dependent. A
  timer-driven blink is a later, opt-out option.
- **Colors.** Each cell carries a 4-bit foreground and a 4-bit background index
  into a 16-entry XRGB palette, so colour is per character and survives
  scrollback. `con_set_colors` sets the pen used for subsequent writes; cells
  already written keep their colours. The table is the 16 ANSI colours, with
  index 0 set to `0x00101820` (theme background) and index 7 to `0x00E6EDF3`
  (theme foreground); it lives in `con_palette` in `console.c`. ANSI SGR
  parsing is a later phase and maps onto the same indices. `gfx_*` stays
  true-colour XRGB; only the console is a palette device.
- **Cell cost.** Two bytes per cell: 7.4 KiB for the 100x37 visible grid and
  51.2 KiB for the 256-line ring.
- **Present policy.** Present at the end of a burst of output, not per
  character. The shell writes a line, then presents once. Input echo presents
  once per keystroke.
- **Buffer policy: single surface.** The console sets `FRONT == BACK` and
  treats `FLIP` as a publish, not a swap. Double buffering a text console has a
  trap: after a flip the new back surface is the previous frame, so a
  dirty-cell redraw leaves stale cells everywhere else, and syncing the whole
  surface costs more than the redraw. A console writes to the surface the host
  reads, between instruction slices, and publishes when a burst is complete.
  Graphical programs that animate should use two surfaces instead; a partial
  frame sampled mid-redraw is invisible at human speed and never affects
  correctness or test hashes because tests stop the guest before sampling.

### 7.3 Font

- 8x16 monospace, glyphs `0x20..0x7E`, 16 bytes per glyph (one byte per row),
  1,536 bytes total. Box-drawing and block glyphs can be added later.
- Generate the table offline from the OFL JetBrains Mono face already vendored
  for the debugger, so the console matches the rest of the system, and check in
  the generated `uint8_t` table with its attribution. Do not depend on
  `stb_truetype` in the kernel.
- The generator is a host tool under `tools/`; the generated table is
  committed so the kernel build stays freestanding.

### 7.4 Transports and line discipline

The shell must not know which transport is active.

- **Output** goes to both transports: serial always, so logs and tests keep
  working; the framebuffer console when a display is present.
- **Input** is polled from both: keyboard first, then serial. In tests the
  keyboard FIFO is empty, so serial remains the scripted path.
- **Line discipline** is shared: echo, backspace, Enter, `Ctrl-C` (cancel
  line), `Ctrl-U` (erase line), `Ctrl-L` (clear screen). `Ctrl-C` cancels a line
  only while the shell is the foreground consumer; while a user program owns the
  console the kernel terminates that process instead (see graphics.md §3.2).
  History and tab completion are later phases.
- **Form feed.** The existing `console_clear()` emits `\f` to serial, and the
  debugger console treats `\f` as clear. The framebuffer console keeps that
  contract: `\f` clears the grid and homes the cursor.

### 7.5 Boot sequence

1. Kernel validates `BootInfo` as today.
2. Probe `0xFFF0_4028` for `"FBR1"`. If absent or wrong, continue with
   serial-only output and note it once.
3. Choose the mode: 800x600 by default. Write `WIDTH`, `HEIGHT`, `FORMAT`,
   `FRONT = 0`, `BACK = 0` (single-surface console), then `CONTROL = 1`, then
   `FLIP`. The first flip publishes the cleared surface; a graphical program
   can later switch `BACK` to a second surface and double-buffer.
4. Clear the grid, print the banner, present.
5. Continue to the existing shell; input now also arrives from the keyboard.

If any `FLIP` fails (`STATUS.FLIP_ERROR`), fall back to serial and report the
reason once. A broken display must never stop the machine from booting.

### 7.6 Focus and key routing in the debugger

- Serial console: pointer hovers the `CONSOLE` panel (current behavior).
- Framebuffer console: `SCREEN` has been clicked and shows the focus dot.
- While the screen is focused, printable keys, `Enter`, `Backspace`, `Tab`,
  and arrows go to the keyboard device. `F5`/`F6`/`F9`-`F11` stay with the
  debugger. `Esc` releases focus.
- The display-only window always routes to the guest.

### 7.7 Failure and fallback

- Missing or wrong `ID`: serial only.
- `FLIP` rejected: keep the previous committed frame; log once.
- Keyboard FIFO overrun: set the status bit; the line discipline drops the
  extra characters rather than replaying them out of order.
- A guest that writes outside the active surface is not an error and not
  reported; it is scratch VRAM.

## 8. Keyboard device

New MMIO page at `0xFFF0_6000` (page 6).

| Offset | Name | Access | Reset | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | `DATA` | RO | 0 | Pop one event word; reads 0 when empty. |
| `0x04` | `STATUS` | RO/W1C | 0 | bit 0: event available. bit 1: overrun, write 1 to clear. bits 8..15: FIFO count. |
| `0x08` | `CONTROL` | RW | 0 | bit 0: IRQ enable. |
| `0x0C` | `IRQ` | RW | 0 | IRQ line 0..15. |

### 8.1 Event word

| Bits | Field |
| --- | --- |
| 0..7 | ASCII code for this event, or 0 when the key has no character |
| 8..15 | Modifiers: bit 0 shift, bit 1 control, bit 2 alt, bit 3 caps lock |
| 16..23 | KR32 scancode |
| 24..31 | Event type: 0 key down, 1 key up |

Events are popped in arrival order. A word of `0` means "empty": no real event
has scancode 0.

### 8.2 Scancodes

Printable keys use a fixed ASCII-based code for the key: letters are uppercase
`0x41..0x5A`, digits `0x30..0x39`, punctuation its unshifted ASCII value, and
space is `0x20`. The ASCII field carries the actual character, so the A key is
scancode `0x41` with ASCII `'a'` or `'A'`, and the `1` key is scancode `0x31`
with ASCII `'1'` or `'!'`.

Non-printable keys use `0x80` and above:

| Code | Key | Code | Key |
| --- | --- | --- | --- |
| `0x80` | Enter | `0x8A` | Up |
| `0x81` | Escape | `0x8B` | Down |
| `0x82` | Backspace | `0x8C` | Left |
| `0x83` | Tab | `0x8D` | Right |
| `0x84` | Delete | `0x90`-`0x9B` | F1-F12 |
| `0x85` | Insert | `0xA0` | Left shift |
| `0x86` | Home | `0xA1` | Right shift |
| `0x87` | End | `0xA2` | Left control |
| `0x88` | Page up | `0xA3` | Right control |
| `0x89` | Page down | `0xA4` | Left alt |
| | | `0xA5` | Right alt |
| | | `0xA6` | Caps lock |

Modifier keys generate their own down/up events. Key repeat arrives as
repeated key-down events; the device does not filter it.

### 8.3 FIFO and interrupts

- 64 events. When full, the new event is dropped and `STATUS.OVERRUN` is set.
- The IRQ line is asserted while the FIFO is non-empty and `CONTROL.bit0` is
  set. The handler drains until `STATUS.bit0` is clear.
- `STATUS` count is the current occupancy (0..64).
- Reset empties the FIFO and clears control, IRQ line, and overrun.

### 8.4 Host mapping

The platform layer translates:

- `SDL_KEYDOWN` / `SDL_KEYUP` -> event with type 0/1.
- Modifier bits from `SDL_GetModState()` (`KMOD_SHIFT`, `KMOD_CTRL`,
  `KMOD_ALT`, `KMOD_CAPS`).
- ASCII from the translated `keysym.sym` when it is a printable ASCII code,
  with shift and caps applied to letters. Layouts where the keysym is not
  ASCII, dead keys, and IME use `SDL_TEXTINPUT` in a later phase; v1 targets
  ASCII input for a shell.
- Scancode from a fixed table keyed by `SDL_Scancode`, so layout changes do not
  move physical keys. The table lives in `src/platform` — the only layer that
  may include SDL headers (section 9.5) — and is exposed as a pure lookup so it
  can be unit tested without opening a window; `tests/unit/test_ui.cpp` may
  include SDL because the boundary check scans `src/ui` only.

### 8.5 Pointer device (page 7)

Page 7 at `0xFFF0_7000` carries a pointer. It has its own FIFO and event word
because the keyboard word has no room for 11-bit coordinates (section 8.1).

| Offset | Name | Access | Reset | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | `DATA` | RO | 0 | Pop one event word; reads 0 when empty. |
| `0x04` | `STATUS` | RO/W1C | 0 | bit 0: event available. bit 1: overrun, write 1 to clear. bits 8..15: FIFO count. |
| `0x08` | `CONTROL` | RW | 0 | bit 0: IRQ enable. |
| `0x0C` | `IRQ` | RW | 0 | IRQ line 0..15. |

Event word:

| Bits | Field |
| --- | --- |
| 0..11 | x, in active-mode pixels, `0..4095` |
| 12..23 | y, in active-mode pixels, `0..4095` |
| 24..27 | buttons: bit 0 left, bit 1 right, bit 2 middle, bits 3..4 reserved |
| 28..30 | type: 0 motion, 1 button down, 2 button up, 3 enter, 4 leave |
| 31 | reserved, zero |

Coordinates are absolute and already inside the active mode when the host sends
them; the device stores them unchanged. A word of 0 is a legal event (motion at
the origin with no buttons), so unlike the keyboard (section 8.1) a zero word
does not mean empty: guests test `STATUS.bit0`.

The FIFO holds 64 events and shares the overrun and IRQ rules of section 8.3.
Nothing injects pointer events until a guest consumer exists; the layout is
fixed here so the device table does not change when one arrives.

## 9. Changes to the current implementation

### 9.1 `include/kr32/core/devices.h`

- Add `kKeyboardBase`, register constants, modifier masks, and the event
  packing helpers.
- Replace `FramebufferDevice`'s public mutable `pixels()` with `vram()` spans
  and add `DisplayFrame`, `frame()`, `frame_pixels()`, `flip()`, `status()`.
- Keep `read_pixel`/`write_pixel` for the bus path, but have them operate on
  the span and validate 4-byte fit.
- Add `KeyboardDevice final : Device` with `push_event`, `pop_event`,
  `status`, and IRQ handling.
- Add the pointer page constants and event packing; the device itself is
  implemented when a guest consumer exists (section 8.5).
- Add `keyboard()` to `DeviceBus`.

### 9.2 `src/core/devices.cpp`

- Rewrite `FramebufferDevice::read_control`/`write_control` to the rules in
  section 5: no `false` returns, range-checked writes, computed `MODE_OK`,
  `FLIP` commit, sticky error.
- Implement `flip()` with 64-bit bounds arithmetic.
- Add `KeyboardDevice`.
- Replace the `if` chain in `DeviceBus::read32`/`write32` with a 240-entry page
  table (`std::array<Device*, 240>` plus the VRAM range). This is not required
  for correctness but keeps adding devices to one line each.

### 9.3 `src/core/machine.cpp`

- No change for phase D1. In phase D4, add a bulk-memory fast path so the
  aperture is checked like RAM instead of dispatched through the bus. Keep the
  bus path for the control page. Measure before and after; the acceptance
  criterion is in section 11.5.

### 9.4 `src/ui/debugger_ui.{h,cpp}`

- `Layout`: add a tab-strip rect and a screen rect; both are derived from the
  existing top-left region, so no permanent panel is added.
- State: `screen_tab_active_`, `screen_focus_`, `screen_frame_` cache, cached
  `DisplayFrame`, and a cached copy of the pixels.
- `draw_screen`: refresh the cache when `number` changes, then blit with
  integer scaling or fit.
- `click`/`key`: tab selection, focus, and routing as in section 7.6.
- Hover: pixel-to-guest-address mapping and tooltip.
- `headless`: include the screen tab and support `--screen-shot`.

### 9.5 `src/ui/app.cpp`, `src/cli/cli.cpp`, `src/platform/sdl2/platform.cpp`

- `app.cpp`: a `run_display` loop used by the new command; key events go to the
  keyboard device; present the committed frame.
- `cli.cpp`: `display` command, `--screen-shot`, and the `SCREEN` tab default
  when an OS image is loaded.
- `platform.cpp`: `present_surface`, plus key event translation to the packed
  event word and the SDL scancode table.
- `BootInfo` is unchanged; the guest probes `ID` instead. If a discovery field
  is ever wanted, add it with a version bump, not silently.

### 9.6 `kr32os/`

- `kernel/fb.h`, `kernel/fb.c`: probe, mode set, `fb_fill`, `fb_present`,
  `fb_surface`.
- `kernel/font8x16.c`: generated table plus attribution.
- `kernel/console.c`: keep the serial writers, add the framebuffer grid,
  dirty rectangle, renderer, cursor, and the 256-line ring buffer.
  `console_getc` polls both transports.
- `kernel/keyboard.h`, `kernel/keyboard.c`: FIFO pop and decoding helpers.
- `kernel/shell.c`: remove direct transport assumptions; it already only calls
  `console_*`, so this should be small.
- `kernel/main.c`: display probe and init before the banner.
- Keep `console_putc`'s serial write unconditional so logs and existing tests
  are unaffected.

### 9.7 Docs

- `devices.md`: point the framebuffer section at this document, correct the
  byte-order wording, add the keyboard page once implemented, and fix the
  timer tick wording.
- `ui.md`: add the `SCREEN` tab and focus rules when D3 lands.
- `isa-v1.md`: add the keyboard MMIO page to the summary if the map table is
  updated there too.
- `testing.md`: add the new CTest entries when they exist.

## 10. Implementation phases

Each phase ends with a green suite and a written exit criterion.

### D0 — specification (this document)

Exit: reviewed; open questions resolved or explicitly deferred.

### D1 — device v2 and keyboard

- Framebuffer v2 registers, commit/flip, format, bounds, status.
- Keyboard device and packed events.
- Unit and integration tests (section 11.1, 11.2).
- No host UI changes beyond compiling.

Exit: `unit_test_devices` covers every register rule; a guest assembly program
can set a mode, fill VRAM, flip, and observe the committed frame from the
device API.

### D2 — host surface

- `platform::Window::present_surface`.
- `kr32 display` with the committed-frame loop and key injection.
- `--screen-shot` for headless.
- A guest test image that draws a color-bar pattern and flips.

Exit: the pattern is visible in the window; the headless PPM hash is stable
across runs and platforms (within the existing font-independent surface).

### D3 — console

- Font generator and table.
- Framebuffer console with dirty rectangles and scrolling.
- Boot probe, mode set, banner.
- Keyboard input and line discipline.
- Serial mirror stays.

Exit: `kr32 display --os` boots to a prompt; typing `ls` and Enter runs it in
the window; the serial transcript of the same run matches the existing shell
tests.

### D4 — debugger integration and performance

- `SCREEN` tab, focus, hover inspection.
- Aperture fast path if profiling requires it.
- Scrollback and cursor blink (opt-out for tests).

Exit: `ui-headless` includes the screen; pixel hover reports the correct guest
address; full-screen fill within the budget in section 11.5.

### D5 — programmer graphics API

- `gfx_*` kernel library and console API (see [graphics.md](graphics.md)).
- User-mode presentation by syscall with dirty rectangles.
- One or two example programs.

Exit: examples build and run; the API is documented and tested.

## 11. Testing

### 11.1 Unit tests (`tests/unit/test_devices.cpp`)

Framebuffer:

- Reset state: registers, committed frame, `FRAME == 0`, VRAM zero.
- `ID` reads the word `0x46425231`; compare the word, not a `memcmp` against
  the string `FBR1`.
- `WIDTH` write sets `STRIDE = WIDTH*4`; out-of-range writes are ignored and
  read back unchanged.
- `STRIDE` accepts padding and rejects `< WIDTH*4` and non-multiples of 4.
- `FORMAT` accepts 1, ignores everything else.
- `FLIP` with a valid bundle commits, swaps `FRONT`/`BACK`, increments `FRAME`.
- The committed descriptor names the surface that was `BACK` before the flip;
  `FRONT` after the flip is that surface, not the one displayed before.
- `FLIP` with `FRONT + HEIGHT*STRIDE == 32 MiB` succeeds; one byte more fails.
- `FLIP` failure sets `FLIP_ERROR`, leaves `FRAME` and the committed frame
  unchanged; writing 1 to `STATUS.bit2` clears it.
- `MODE_OK` tracks the register bundle; `ENABLED` tracks the committed frame.
- Disabled flip commits `ENABLED = 0` and still increments `FRAME`.
- `FRAME` wraps at `2^32`; add a small test seam to preset the counter rather
  than flipping four billion times.
- Committed descriptor matches the registers at the moment of the flip, not
  subsequent writes.
- Byte, halfword, word, and unaligned accesses across a 4 KiB boundary preserve
  neighboring bytes.
- Store at `base + 32 MiB` faults; store at `base + 32 MiB - 4` succeeds.

Keyboard:

- Event fields pack and unpack.
- FIFO order, empty read returns 0, 64 entries, 65th drops and sets overrun.
- Overrun W1C.
- IRQ asserted while non-empty with control enabled; not asserted when empty.
- Reset clears everything.

Bus:

- Unlisted offsets in a device page read 0 and ignore writes.
- Unclaimed pages still fault.
- Page-table dispatch matches the old range checks for every device.

### 11.2 Integration tests (guest assembly through `Machine`)

- A program writes a 4x4 pattern, flips, and reads back the committed frame
  descriptor and bytes.
- Two flips return to the original front.
- A second surface at a non-default offset works.
- A bad mode (stride too small, front out of range) is rejected without
  changing the display.
- Keyboard events injected by the host are read in order by guest code.
- `BootInfo` values still match the device's reset registers (1920x1080,
  stride 7680, format 1).

### 11.3 Golden frames

- `--screen-shot` a fixed pattern at 800x600; compare a 64-bit hash stored in
  the test with a comment naming the expected image. Update deliberately.
- Run the same program twice; hashes must match.
- Console text: render a known string into an empty grid and assert specific
  cells' pixel colors, not only a whole-frame hash, so a font change points at
  the glyph rather than an opaque number.
- Keep the existing PPM artifacts for debugging; do not make PPM bytes the
  assertion on platforms with different endianness (all current targets are
  little-endian, but the hash is cheaper and clearer).

### 11.4 Console and input

- `con_putc` at the right margin wraps; at the bottom it scrolls.
- `\n`, `\r`, `\b`, `\f` behavior matches the serial console contract.
- Dirty rectangle covers exactly the cells that changed (assert cell states,
  then assert the rect).
- Cursor cell is restored when the cursor moves.
- Line discipline: backspace, `Ctrl-U`, `Ctrl-C`, `Ctrl-L`.
- Shell: inject `ls\n` through the keyboard FIFO, run until the prompt, assert
  the serial transcript contains `init` and the grid contains the echoed line.
- A key-up event with the same scancode does not produce a character.
- Modifier translation: shift+`1` yields ASCII `!` with scancode `0x31`.

### 11.5 Performance budgets

Gate on emulated instruction counts, which are deterministic for a given
program (section 3, rule 5). Record the ceiling in the phase log when the phase
lands, and assert it in the headless tests.

- Guest full-screen fill of an 800x600 surface: an instruction-count ceiling
  recorded at D2. The Windows Debug build fills it in under 5 ms as an
  observation, not a threshold.
- Host copy and present of 800x600: one full-frame copy per committed frame,
  because v1 has no partial-upload channel (section 6.1). Observed per frame,
  never a pass/fail value.
- Console redraw of a 100-column line: the guest cost is the dirty cells plus
  one flip; record the instruction count, do not gate on milliseconds.
- Aperture fast path, if added: at least 2x fewer instructions than the bus
  path for a full-screen fill, and byte-for-byte identical results under a
  randomized comparison.

Wall-clock figures vary by machine and build; only instruction counts and
ratios gate.

### 11.6 CI

- New unit cases live in `tests/unit/test_devices.cpp` and
  `tests/unit/test_ui.cpp`; the per-source-file CTest loop picks them up.
- New runtime tests use the existing OS resource lock, since they share
  `kr32os/build`.
- `ui_smoke` with `SDL_VIDEODRIVER=dummy` gains a display-window path.
- Headless screenshot tests run where the UI is built; `KR32_BUILD_UI=OFF`
  still builds the core device tests.

## 12. Open questions

None outstanding; the D0 exit condition (section 10) is met as of
2026-11-12T14:55:40-08:00. Add new questions here rather than editing a
normative table.

Resolved in this review: scrollback is a 256-line kernel ring buffer
(section 7.2); console cells are two bytes with 4-bit palette indices
(section 7.2); pointer events use a separate FIFO and event word on page 7
(section 8.5); `Ctrl-C` terminates a foreground user program (section 7.4); the
display `ID` is compared as a 32-bit word (section 5); the dirty rectangle is a
guest-side hint with no v1 host channel (section 6.1); the SDL scancode table
lives in `src/platform` (section 8.4); performance gates on instruction counts
(section 11.5); and `FLIP` commits the surface that was `BACK` before the call
(section 5.3).
