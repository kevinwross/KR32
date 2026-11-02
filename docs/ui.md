# Windowed debugger (M6)

`kr32 gui <image.k32|source.c>` opens the portable debugger. A C source is
compiled, assembled, linked, and loaded in memory; `.k32` executables load
directly.

![KR32 debugger](screenshots/m6-debugger.png)

![KR32 machine panel](screenshots/m6-machine.png)

## Architecture

- `ui::Canvas` is a 32-bit RGBA software canvas (`0xAARRGGBB`) with rectangles,
  frames, lines, a built-in 5x7 bitmap font, PPM output, and a deterministic
  frame hash. It has no OS or SDL dependency.
- `ui::DebuggerUI` owns a `debug::Session`, source text, breakpoints, tab state,
  and all painting/hit-testing. It is usable headlessly.
- `platform::Window` is the SDL2 boundary: one window, one streaming ARGB8888
  texture, keyboard/mouse event mapping, and the `SDL_VIDEODRIVER=dummy` smoke
  path.

## Layout

| Area | Contents |
| --- | --- |
| Toolbar | Run, Step, Over, Out, Breakpoint, Reset, Load, Build, status, counters |
| Source | Source lines, current-line highlight, breakpoint markers |
| Disassembly | Current PC and surrounding instructions |
| Registers | R0-R15, PC, SP, FLAGS, mode, ticks, core |
| Variables | Locals, globals/watch values, call stack |
| Bottom tabs | Memory hex dump, machine state, console |
| Machine tab | FPRs, T0 4x4 tile, PTBR/page table entries, control registers, TSP, core/mode |

## Controls

- `F5` run to a breakpoint or halt.
- `F10` step over, `F11` step in, `Shift+F11` step out.
- `F9` toggle a breakpoint at the current source line.
- `R` restart/reload; `Tab` cycles the bottom tabs; `Esc` quits.
- Click a source line or disassembly row to toggle a breakpoint.

## Headless checks

```text
kr32 gui --headless build/hello-c.k32 --screenshot build/hello-ui.ppm
```

renders the memory, machine, and console tabs, writes PPM screenshots, and
prints stable per-tab hashes. CTest's `ui_headless` uses this path, while
`ui_smoke` uses the SDL dummy driver. `KR32_BUILD_UI=OFF` excludes the SDL,
canvas, and GUI targets and keeps the headless test suite green.

## Current limits

- The font is a built-in uppercase 5x7 atlas; lowercase source is displayed in
  uppercase.
- There is no native file dialog, settings panel, or guest-argument editor yet;
  those are reserved for the M7 host services.
- "Break" is expressed through breakpoints; a cooperative pause of a running
  guest is not implemented.
