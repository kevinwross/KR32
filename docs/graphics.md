# Graphics and console API (proposal)

**Status:** proposal. This document defines the API presented to people writing
KR32 programs. It builds on the display contract in [display.md](display.md).

**Implementation model (2026-11-13).** Drawing runs in the **host**, through the
graphics command device (page 9, [terminal.md](terminal.md) §3.1), and the
console is the host terminal (page 8). The functions in this document are the
guest-facing wrappers; they submit commands and never rasterise. The surface
convention and the semantics below are unchanged, and the guest cost is one
command per call rather than a pixel loop.

**Owner:** display/hardware stream. The C/C++ compiler and libc stream owns the
guest headers; section 8 is the hand-off, and nothing here should be
implemented in the compilers until both streams agree.

## 1. Who the API is for

Three audiences, in order of arrival:

1. **KR32OS itself.** The console API (`con_*`) and the shell are the first
   client; text and simple drawing go through the host.
   They need text, colors, a cursor, and flips. No allocation, no user
   pointers, no blocking.
2. **Kernel and driver code.** Splash screens, panic screens, simple demos.
   Same primitives, direct framebuffer access.
3. **User programs.** Eventually a graphical program that draws and gets
   keyboard input. It runs in its own address space and must not be able to
   scribble over the rest of the screen.

The API is layered so the same drawing code serves all three:

```text
        user program                kernel
             |                        |
      kr32/gfx.h (drawing)      kr32/gfx.h (drawing)
             |                        |
   display_present syscall      kr32/fb.h (device)
             |                        |
             +-------- VRAM ----------+
```

`kr32/gfx.h` is pure memory manipulation: it takes a `gfx_surface` and never
talks to a device, allocates, or blocks. That is what makes it identical in
kernel and user mode, and testable on the host.

## 2. Pixel and surface conventions

- A pixel is a 32-bit word `0x00RRGGBB` (XRGB8888), the format defined in
  [display.md](display.md) section 5.4.
- A surface is:

```c
typedef struct {
    uint32_t *pixels;  /* first pixel of row 0 */
    int width;         /* visible pixels per row */
    int height;        /* visible rows */
    int stride;        /* pixels between rows; >= width */
    int clip_x, clip_y, clip_w, clip_h;  /* current clip rectangle */
} gfx_surface;
```

- `stride` is in **pixels**, not bytes, so `pixels + y*stride + x` is the
  address of `(x, y)`.
- A freshly initialized surface has its clip set to `(0, 0, width, height)`.
- Rectangles are half-open: `(x, y, w, h)` covers `x..x+w-1` and `y..y+h-1`.
- Coordinates may be negative; primitives clip and never fault.
- Colors are opaque. There is no alpha and no blending in v1; a future blend
  path gets separate `gfx_blend_*` functions so the fast path stays fast.

Helpers:

```c
#define GFX_RGB(r, g, b) ((uint32_t)(((r) & 0xFF) << 16 | \
                                    ((g) & 0xFF) << 8  | \
                                    ((b) & 0xFF)))

#define GFX_BLACK  0x00000000u
#define GFX_WHITE  0x00FFFFFFu
#define GFX_RED    0x00FF0000u
#define GFX_GREEN  0x0000FF00u
#define GFX_BLUE   0x000000FFu
```

## 3. Surfaces and presentation

### 3.1 Kernel side (`<kr32/fb.h>`)

Device-level, thin, and boring. It exists so callers do not hard-code MMIO
addresses.

```c
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;   /* bytes between rows; note the unit */
    uint32_t format;         /* 1 */
    uint32_t frame;          /* committed-frame counter */
} fb_info;

int      fb_probe(void);              /* 0 if "FBR1" is present, else -ENODEV */
int      fb_mode(int width, int height);  /* 0, -EINVAL, or -ENODEV */
int      fb_info_get(fb_info *out);
gfx_surface *fb_back(void);           /* draw here */
gfx_surface *fb_front(void);          /* currently displayed */
int      fb_present(void);            /* commit + swap; 0 or -errno */
int      fb_present_rect(int x, int y, int w, int h);  /* dirty-rect variant */
```

`stride_bytes` is named for its unit on purpose: `gfx_surface.stride` above is
in pixels, matching how drawing code indexes rows. They are related by
`stride_bytes == stride * 4`.

`fb_present()` is one `FLIP`: it commits the frame and swaps the surfaces.
When `FRONT == BACK` it is a publish, which is how the console works.

`fb_present_rect` commits the whole frame exactly like `fb_present` and passes
the touched rectangle as a hint. There is no device register for it and no host
channel in v1, so the host copies the whole committed frame (display.md §6.1);
the rectangle costs nothing and is reserved for a later host that uploads a
sub-rectangle with `SDL_UpdateTexture`. It must not be relied on for
performance.

### 3.2 User side (`<kr32/display.h>`)

A user program does not write VRAM. It draws through the graphics command
device into a surface the host owns, and presents with a flip:

```c
typedef struct {
    uint32_t offset;         /* surface offset in VRAM, or 0 for the default */
    int width, height;       /* surface extent, in pixels */
    int stride;              /* pixels per row */
} display_surface;

int sys_display_info(fb_info *info);
int sys_display_present(void);           /* FLIP; 0 or -errno */
```

Every drawing call (`gfx_*`, below) becomes a command to the host with the
surface offset as its first parameter; the host validates the rectangle against
the surface and the clip. The kernel still intercepts `Ctrl-C` and terminates
the foreground process, so a program that stops polling input cannot lock the
shell.

The wrapper hides the command submission:

```c
gfx_surface *gfx_create(int width, int height);  /* VRAM-backed handle */
void gfx_destroy(gfx_surface *surface);
int  gfx_present(gfx_surface *surface, int x, int y, int w, int h);
```

`gfx_create` allocates a VRAM surface through the device; there is no guest
`calloc` and no pixel copy. `gfx_present` issues a flip; the rectangle is a hint
until the host gains a partial-present channel.

## 4. Drawing primitives (`<kr32/gfx.h>`)

All functions honor `surface->clip`, clip internally, and never fault.
`color` is an XRGB word. Each call submits the matching graphics command
(`FILL_RECT`, `COPY_RECT`, `DRAW_LINE`, …) and the host rasterises; the guest
loop is one command, not a pixel loop.

```c
void gfx_clear(gfx_surface *s, uint32_t color);
void gfx_pixel(gfx_surface *s, int x, int y, uint32_t color);
void gfx_hline(gfx_surface *s, int x, int y, int w, uint32_t color);
void gfx_vline(gfx_surface *s, int x, int y, int h, uint32_t color);
void gfx_fill_rect(gfx_surface *s, int x, int y, int w, int h, uint32_t color);
void gfx_frame_rect(gfx_surface *s, int x, int y, int w, int h, uint32_t color);
void gfx_line(gfx_surface *s, int x0, int y0, int x1, int y1, uint32_t color);

void gfx_blit(gfx_surface *dst, int dx, int dy,
              const gfx_surface *src, int sx, int sy, int w, int h);
void gfx_blit_keyed(gfx_surface *dst, int dx, int dy,
                    const gfx_surface *src, int sx, int sy, int w, int h,
                    uint32_t key);
void gfx_blit_mask(gfx_surface *dst, int dx, int dy,
                   const gfx_surface *src, int sx, int sy, int w, int h,
                   const uint8_t *mask, int mask_stride, uint32_t key);
```

Semantics worth writing down because they cause bugs:

- `w` or `h` <= 0 does nothing.
- `gfx_blit` with overlapping source and destination on the same surface is
  **undefined**; use an off-screen surface. (Documenting this is cheaper than
  guaranteeing memmove semantics that nobody uses.)
- `gfx_blit_keyed` skips source pixels equal to `key`.
- `gfx_blit_mask` treats bit `(x & 7)` of `mask[y*mask_stride + x/8]` as
  opaque; `key` is the transparent source color for masked-off pixels.
- `gfx_line` uses Bresenham and includes both endpoints.
- `gfx_frame_rect` draws the outline inside the rectangle; a width or height of
  1 or 2 degenerates correctly.

Clipping:

```c
void gfx_set_clip(gfx_surface *s, int x, int y, int w, int h);
void gfx_reset_clip(gfx_surface *s);
```

`gfx_set_clip` intersects with the surface bounds; an empty intersection makes
all drawing a no-op.

## 5. Text (`<kr32/gfx.h>`)

```c
int gfx_font_width(void);    /* 10 */
int gfx_font_height(void);   /* 20 */
int gfx_text_width(const char *text);

int gfx_text(gfx_surface *s, int x, int y, const char *text,
             uint32_t fg, uint32_t bg);   /* returns advance in pixels */
int gfx_text_clipped(gfx_surface *s, int x, int y, int max_width,
                     const char *text, uint32_t fg, uint32_t bg);
```

- 10x20 bitmap font, ASCII `0x20..0x7E`; other bytes render as `?`. This is the
  same atlas the terminal uses, so console and graphics text match.
- `bg` equal to the current background is still written; there is no
  transparency in v1. A `gfx_text_cell` variant that draws only the glyph
  pixels can come later.
- `gfx_text_clipped` stops at `max_width` and clips the last partial glyph.

## 6. Console (`<kr32/con.h>`)

The console is the **host terminal** ([terminal.md](terminal.md)); the grid,
font, palette, scrollback, and rendering live in the host, and this API is the
guest wrapper over the terminal page. It is the only part of this API the shell
uses.

```c
int  con_init(void);      /* probes the terminal; -ENODEV if none */
void con_shutdown(void);

void con_clear(void);     /* COMMAND 1 */
void con_putc(char c);    /* DATA */
void con_puts(const char *text);
void con_write(const char *text, int count);
int  con_printf(const char *format, ...);   /* kernel build only */

void con_set_colors(int fg_index, int bg_index);  /* COLOR; 0..15 */
void con_set_cursor(int visible);                 /* CONTROL bit 0 */
void con_hide(int hidden);                        /* CONTROL bit 1 */
void con_present(void);                 /* FLIP */

int  con_getc(void);                    /* -1 when no input */
int  con_readline(char *buffer, int capacity);
```

Behavior:

- `con_putc` handles `\n`, `\r`, `\b`, `\t`, and `\f` (clear and home) in the
  host, matching the serial console contract so the shell does not branch on
  transport.
- A cell is two bytes in the host: a character plus a 4-bit foreground and
  4-bit background palette index. `con_set_colors` sets the pen for subsequent
  writes; it does not recolour cells already on screen or in the scrollback.
- At the right margin the cursor wraps; at the bottom the host scrolls the grid.
  Neither costs the guest pixel work.
- `con_present` is a flip only.
- `con_getc` polls the keyboard and then serial, so the same shell works in a
  window and in a headless test.
- `con_readline` implements echo, backspace, `Ctrl-U`, `Ctrl-C`, and `Ctrl-L`.
  History is not part of the first version.

Console state is host-side and not exposed; tests use the terminal device
directly ([display.md](display.md) §11.4). Scrollback is internal to the host.

## 7. Input (`<kr32/input.h>`)

```c
typedef struct {
    uint8_t ascii;      /* 0 when none */
    uint8_t modifiers;  /* bit0 shift, bit1 ctrl, bit2 alt, bit3 caps */
    uint8_t scancode;   /* KR32 scancode */
    uint8_t type;       /* 0 key down, 1 key up */
} kr32_event;
```

The field order matches the packed device word byte for byte in little endian
(bits 0..7 ASCII, 8..15 modifiers, 16..23 scancode, 24..31 type), so the kernel
can copy a word and cast. Do not reorder the fields.

```c
int input_poll(kr32_event *event);              /* 1 event, 0 none */
int input_pending(void);
int input_wait(kr32_event *event, int timeout_ms);  /* kernel build */
```

Helpers:

```c
int  input_key_down(const kr32_event *e);   /* type == 0 */
char input_char(const kr32_event *e);       /* ascii or 0 */
```

The packed word from the device maps to the struct byte for byte in little
endian, so the kernel can copy a word and cast.

## 8. Headers, packages, and the compiler hand-off

Guest headers are synthesized by the C and C++ front ends; there are no `.h`
files on disk (see the `#include` handling in `src/cc/cc.cpp` and
`src/cxx/preprocessor.cpp`). Adding these headers therefore requires a change
in the compiler stream. That is deferred, and the compiler stream should treat
this section as the specification when it picks the work up.

Proposed header names and contents:

| Header | Declares |
| --- | --- |
| `<kr32/fb.h>` | `fb_info`, `fb_probe`, `fb_mode`, `fb_back`, `fb_front`, `fb_present*` |
| `<kr32/display.h>` | `display_surface`, `sys_display_info`, `sys_display_present` |
| `<kr32/gfx.h>` | `gfx_surface`, `GFX_RGB`, drawing and text functions |
| `<kr32/con.h>` | `con_*` |
| `<kr32/input.h>` | `kr32_event`, `input_*` |

Prefixes stay short: `fb_`, `gfx_`, `con_`, `input_`. The headers are already
scoped by their `<kr32/...>` names, and the freestanding libc exposes
unprefixed names such as `printf` and `malloc`; there is no `kr32_` symbol
prefix.

Rules for the front ends:

- Each header is idempotent and self-contained; include guards are implicit
  because the current synthesizer emits declarations once.
- They declare only; no macros that depend on compiler internals.
- `uint32_t`/`uint8_t` are assumed to be already available; the synthesizer
  should emit the `stdint.h` typedefs if they are not.
- Unknown headers are currently ignored silently, which means a typo compiles
  and fails at link time. Consider warning on an unknown `#include`; that is a
  compiler-stream decision, noted here because it will bite graphics users.

Until the headers exist, guest code can declare the handful of prototypes it
uses locally. The ABI is plain C, so nothing else is needed.

## 9. Performance contract

The CPU is emulated, so instruction count is the real budget — and the guest
must not rasterise.

- Every primitive is one command; the host clips and rasterises.
- `gfx_fill_rect` of `w*h` costs the guest a handful of register writes, not
  `w*h` stores.
- `gfx_present` is a flip; the host copies the committed frame as before
  (display.md §6.1).
- The terminal does all text work in the host: typing one character costs the
  guest one `DATA` store.
- No drawing call allocates or blocks; the host validates every parameter.

Acceptance criteria: a console keystroke performs no guest pixel loop, and no
`gfx_*` call contains a loop over pixels on the guest side.

## 10. Examples

Console hello (kernel or shell):

```c
#include <kr32/con.h>

void banner(void) {
    con_set_colors(7, 0);  /* palette indices; see display.md 7.2 */
    con_clear();
    con_puts("KR32OS\n");
    con_puts("type 'help' for commands\n");
    con_present();
}
```

Drawing and flipping:

```c
#include <kr32/fb.h>
#include <kr32/gfx.h>

void splash(void) {
    gfx_surface *back = fb_back();
    gfx_clear(back, GFX_RGB(0x10, 0x18, 0x20));
    gfx_fill_rect(back, 40, 40, 320, 200, GFX_RGB(0x1F, 0x6F, 0xB2));
    gfx_frame_rect(back, 40, 40, 320, 200, GFX_WHITE);
    gfx_text(back, 56, 56, "KR32", GFX_WHITE, GFX_RGB(0x1F, 0x6F, 0xB2));
    fb_present();
}
```

Animated box with dirty rectangles (user mode):

```c
#include <kr32/display.h>
#include <kr32/gfx.h>
#include <kr32/input.h>

void bounce(void) {
    gfx_surface *s = gfx_create(800, 600);
    int x = 0, y = 0, dx = 3, dy = 2;
    for (;;) {
        int old_x = x, old_y = y;
        if (input_pending()) break;
        x += dx; y += dy;
        if (x < 0 || x > 760) dx = -dx;
        if (y < 0 || y > 560) dy = -dy;
        gfx_fill_rect(s, old_x, old_y, 40, 40, GFX_BLACK);
        gfx_fill_rect(s, x, y, 40, 40, GFX_RED);
        gfx_present(s, old_x < x ? old_x : x, old_y < y ? old_y : y,
                    80, 80);
    }
    gfx_destroy(s);
}
```

## 11. What is deliberately not here

- **Retained-mode or scene graphs.** Immediate mode is transparent for
  teaching: what runs is what you see on screen. A widget toolkit can be built
  on these primitives later without changing them.
- **Alpha blending.** Separate functions if it is ever needed.
- **Rounded rectangles, ellipses, gradients.** Straightforward additions with
  their own cost; not first.
- **Font scaling and TTF.** The host font is a fixed 10x20 bitmap atlas; scaling
  and system fonts are out of scope (1x/2x integer scaling can come later).
- **Multiple windows.** One full-screen surface per program until there is a
  compositor and a reason for one.
- **Allocation in the drawing core.** `gfx_create` allocates a VRAM surface
  through the device; drawing calls never allocate.

## 12. Testing

### 12.1 Host unit tests for the drawing core

`gfx.c` is portable C with no OS dependencies. Add it to the host test target
and test it against a plain host buffer:

- Fill and clip: negative origins, zero/negative sizes, clip smaller than the
  surface, clip entirely outside.
- `gfx_fill_rect` touches exactly the expected pixels and no others (sentinel
  rows and columns).
- `gfx_pixel`, hline, vline edge cases at `width-1` and `height-1`.
- `gfx_frame_rect` for 1x1, 1xN, Nx1.
- `gfx_line` endpoints, steep and shallow slopes, single point.
- `gfx_blit` source and destination clipping, `gfx_blit_keyed`, `gfx_blit_mask`
  bit order.
- `gfx_text` advances by 8 per character; unknown bytes render.
- Sentinel checks that no function writes outside the surface allocation.

This is the cheapest and most valuable test layer: no emulator, no window.

### 12.2 Guest runtime tests

- A kernel program clears to a known color, draws a pattern, presents, and the
  host-side test hashes the committed frame.
- The console grid is asserted at the cell level for wrapping, scrolling, and
  control characters, then the frame hash is checked.
- A user program presents a dirty rectangle; the test checks that pixels
  outside the rectangle are unchanged.
- `display_present` validation: bad pointer, bad stride, oversized rectangle,
  and a rectangle partly off screen (clipped, not an error).

### 12.3 Input

- Pure mapping test for host key codes to `kr32_event` (no SDL in the test).
- `input_poll` order, empty, and overrun behavior.
- Shift and caps translation through the packed event.

### 12.4 Examples as tests

Each example under `examples/gfx/` is built and run in the emulator; the run
must halt or produce a stable screen hash. This keeps the API honest because
the documentation examples are the tests.

## 13. Open questions

None outstanding; the D0 exit condition is met as of
2026-11-12T14:55:40-08:00. Add new questions here rather than editing a
normative table.

Resolved in this review: prefixes stay short (section 8); clip lives in
`gfx_surface` (section 2); `fb_present_rect` is a guest-side hint with no v1
host channel (section 3.1); a user program owns the whole screen while it runs
(section 3.2); `Ctrl-C` terminates the foreground process (section 3.2); text
is ASCII in v1 (section 5); scrollback is internal to the host terminal
([terminal.md](terminal.md) §2); `gfx_create` returns `NULL` on failure
(section 3.2); and the font is the OFL JetBrains Mono 10x20 rasterisation
([terminal.md](terminal.md) §3).
