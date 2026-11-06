# Host services and sandbox (M7)

The guest reaches the host through `SYSCALL` vectors 32..51. Arguments are in
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
| 44 | thread_create | entry, arg, stack_size | handle or `-1` |
| 45 | thread_join | handle | exit code (guest join is stage-2) |
| 46 | thread_yield | — | 0 |
| 47 | thread_exit | code | halts the thread |
| 48 | futex_wait | address, expected | 0 blocked, 1 value mismatch |
| 49 | futex_wake | address, count | woken count |
| 50 | thread_self | — | current thread handle |
| 51 | file_dialog | mode, flags, path, capacity | path length or error |
| 52 | unlink | path | 0 or error |
| 53 | rename | old_path, new_path | 0 or error |
| 54 | stat | path, buffer, capacity | 0 or error |
| 55 | truncate | path, length | 0 or error |

Thread services are provided by `vm::ClusterHost`, which maps one guest thread
per `core::Cluster` core and advances one core turn at a time. A thread handle
is `core + 1`; a mismatch in `futex_wait` returns immediately with `1`, and a
matching wait suspends the thread until `futex_wake`. Guest `thread_join` blocks
by servicing other runnable cores until the target exits and returns its exit
code. Each thread receives a TCB at `R11` with the frozen offsets in
`docs/abi.md` and a 128-byte FPR save area.

Open flags: bits 0..1 select read/write (`0` read, `1` write, `2` both),
bit 2 creates, bit 3 truncates, bit 4 appends.

## Runtime wrappers

The C runtime supplies `getchar`, `open`, `close`, `read`, `write`, `seek`,
`sys_time`, `sys_random`, `sys_argc`, and `sys_arg`. `putchar` and `exit`
remain in the same path. The built-in `<stdio.h>`/`<string.h>`/`<stdlib.h>`
prototypes expose the console, memory, string, and file helpers.

The standalone debugger `Session` also handles vectors `32..43` and `51`. It
uses a host instance rooted at the current directory for file I/O, so a file
selected through the dialog can be opened by the guest. Paths outside that root
remain blocked unless they came from a native dialog selection.

## File dialogs

Vector `51` shows a native open/save dialog:

```text
R0  mode       0 = open, 1 = save
R1  flags      bit 0 = must exist, bit 1 = overwrite prompt
R2  path       guest buffer
R3  capacity   buffer bytes, including the trailing NUL

returns >= 0   UTF-8 path length, excluding NUL
        -1     canceled
        -2     dialog backend unavailable
        -5     dialog/host error
        -14    guest path buffer fault
        -22    invalid mode/flags/buffer
        -28    buffer too small; R1 returns the required capacity
```

`sys_open_dialog(path, capacity)` and `sys_save_dialog(path, capacity)` are
runtime wrappers. The C and C++ `<stdlib.h>` surfaces expose all three
functions.

The host callback is `HostOptions::file_dialog`. The command-line `kr32`
wrappers and the GUI debugger install the platform dialog when the UI is built;
tests inject a fake callback. A selected absolute path is recorded by the host
as an authorized path, so a guest `open(path, ...)` can use the returned path
without weakening the normal sandbox rule for other paths. Save does not create
the file: the guest still calls `open` with the desired
write/create/truncate flags.

`stat` writes a 16-byte guest structure: `st_size`, `st_mode`, `st_mtime`, and
a reserved word. `st_mode` uses `0x8000` for regular files and `0x4000` for
directories. `unlink` and `rename` use the same path authorization and
read-only rules as `open`.

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
