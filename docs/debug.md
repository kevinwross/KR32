# Debug information and `DebugSession`

M5 adds a debug section to the `.kr32` container and a shared debug model used by
the CLI today and by the GUI/DAP in later milestones.

## `.debug_info`

The compiler emits NUL-terminated text records into `.debug_info`:

| Record | Meaning |
| --- | --- |
| `S <id> <path>` | Source file id and path |
| `L <label> <source> <line> <column>` | Source line starts at the address of `label` |
| `F <name> <start> <end> <frame> <type>` | Function range, frame size, result type |
| `V <function> <name> <offset> <type> <size>` | Local/parameter at `R7 + offset` |
| `P <function> <name> <register> <start> <end>` | Same variable, held in `R<n>` or `F<n>` over `[start, end)` |
| `G <name> <label> <type> <size>` | Global at the address of `label` |
| `T <name> <size>` | Type table entry |
| `C <class> <size> <base>` | C++ class layout |
| `M <class> <name> <offset> <type> <size>` | C++ member at a class offset |

Types include `float` (4 bytes) and `double` (8 bytes) alongside the integer,
pointer, array, struct, union, and function types emitted by the C compiler.
Floating-point variables and globals use the same `V`/`G` records with their
size and type name, so the debugger can identify them without changing the
container format. C++ array types carry their element count (`float[16]`,
`char[32]`) so the GUI can expand and page them; an unknown bound is written as
`[]`. Built-in C++ header injection emits `#line` restoration records so source
lines after `#include <kr32/matrix.hpp>` and the other freestanding headers keep
their original numbers. Source-line lookup is scoped to a function’s `F` range:
addresses in runtime/libc code without debug records do not inherit the last
line of the previous user function.

The libc is compiled with its own source id and `src/cc/libc.c` source records.
The GUI loads all debug sources and switches the SOURCE panel to the file
containing the current PC, so stepping into `printf` or `malloc` displays the C
source and libc locals instead of only assembly.

Labels are resolved against the linked image's symbol table when the debug info
is loaded. This keeps the format relocatable without changing the object model;
the linker already preserves the local labels unless `--strip` is used.

A `P` record means the optimizer moved a variable out of its frame slot: the
promotion passes rewrite the stores, so the slot `V` points at is never written
and reading it would report zero. A reader prefers the `P` record when the PC
lies inside its range and falls back to `R7 + offset` otherwise. A whole-function
promotion takes its range from the labels of the function's own `F` record, so
the record also covers the stretch before the first assignment: a local that has
not been assigned yet reads as the register's stale contents rather than as
unavailable, the same way a frame slot reads before its first store. Starting the
range at the first store would report "unavailable" instead; that is deferred,
because it makes the record depend on a line position again. A span promotion
instead names the labels that bracket its interval, so its range is a subset of
the interval and may omit a few instructions at each end.

The object assembler accepts `.debug_info` as a non-alloc debug section. The
linker concatenates debug payloads at address zero; they are never loaded into
guest memory.

## `DebugSession`

`kr32::debug::Session` owns a `core::Machine`, the parsed `Info`, breakpoints,
and captured console output. It exposes:

- `load`, `machine`, `info`, `dump`
- `set_breakpoint`, `set_breakpoint_enabled`, `clear_breakpoint`,
  `clear_breakpoints`, `at_breakpoint`, `breakpoint_enabled`
- `step`, `run`, `step_over`, `step_out`
- `line_at`, `function_at`, `call_stack`
- `read_variable`, `read_global`, `read_memory`
- `console`, `clear_console`

`step_over` decodes the current instruction and runs through a `CALL` to its
return address. `step_out` reads the return address from the current frame at
`R7 + frame + 4`. Call frames unwind through the saved `R7` at `R7 + frame`.
`step_line_out` uses the same return address and stops at the caller
instruction after `CALL`, without advancing an additional source line.

## CLI

```text
kr32 debug app.kr32
```

prints the source, functions, globals, variables, types, and line map. The
future GUI and DAP layers consume the same `Session`.

## Current limits

- Line records are emitted at statement boundaries, not every instruction.
- C++ emits class/member and `this` records; template-instance and namespace
  scope records are still deferred.
- Thread snapshots (`ThreadInfo`) are exposed by `debug::Session`, DAP `threads`,
  and the GUI call-stack panel; per-thread variable/stack expansion is partial.
- Debug labels are resolved at load time rather than through linker
  relocations.
