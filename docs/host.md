# Host services and sandbox (M7)

The guest reaches the host through `SYSCALL` vectors 32..43. Arguments are in
`R0..R3`; the result is in `R0`. Error results are small negative values
(`0xFFFFFFFF`, `0xFFFFFFFC`, ...).

| Vector | Name | Arguments | Result |
| --- | --- | --- | --- |
| 32 | exit | code | halts the machine |
| 33 | putchar | character | 0 |
| 34 | getchar | — | character or `-1` |
| 35 | open | path, flags | fd or `-1` |
| 36 | close | fd | 0 or error |
| 37 | read | fd, buffer, count | bytes or error |
| 38 | write | fd, buffer, count | bytes or error |
| 39 | seek | fd, offset, whence | position or error |
| 40 | time | — | host seconds |
| 41 | random | — | deterministic xorshift value |
| 42 | argc | — | guest argument count |
| 43 | arg | index, buffer, size | copied length |

Open flags: bits 0..1 select read/write (`0` read, `1` write, `2` both),
bit 2 creates, bit 3 truncates, bit 4 appends.

## Runtime wrappers

The C runtime supplies `getchar`, `open`, `close`, `read`, `write`, `seek`,
`sys_time`, `sys_random`, `sys_argc`, and `sys_arg`. `putchar` and `exit`
remain in the same path. The built-in `<stdio.h>`/`<string.h>`/`<stdlib.h>`
prototypes expose the console, memory, string, and file helpers.

## Sandbox

`HostOptions` controls:

- `fs_root`: root directory for guest paths.
- `read_only`: rejects every write/open-for-write.
- `quota`: maximum bytes written through the host.
- `args`: strings returned by `argc`/`arg`.
- `input`: text consumed by `getchar`.

Guest paths must be relative. Absolute paths and any `..` component are
rejected before the path is resolved. The resolved path must remain below the
canonical root. The host checks this on every open.

CLI:

```text
kr32 vm app.k32 --fs-root sandbox --read-only --quota 4096 --arg hello
kr32 build examples/fileio.c --run --fs-root sandbox
```

## Shell

`kr32 shell` accepts `as`, `build`, `cc`, `debug`, `dis`, `ld`, `run`, `size`,
`vm`, `version`, `history`, `clear`, and `exit`. It keeps history and works from
piped input for scripted tests.

## DAP

`kr32 dap --stdio <image.k32>` speaks DAP over stdio; `kr32 dap --port 4711
<image.k32>` listens on loopback TCP. Implemented requests are `initialize`,
`launch`, `setBreakpoints`, `configurationDone`, `threads`, `stackTrace`,
`scopes`, `variables`, `continue`, `next`, `stepIn`, `stepOut`, `evaluate`,
`readMemory`, `disconnect`, and `terminate`. Events include `initialized`,
`stopped`, `exited`, and `terminated`. The server shares `DebugSession` with
the GUI and CLI.
