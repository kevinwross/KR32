# KR32 `.k32` container

The `.k32` file is used for relocatable objects and linked executables. It is a
little-endian binary container: a fixed 64-byte header, section/symbol/relocation
tables, a string table, and section payloads.

## Header

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `K32\0` |
| 4 | 2 | Version, currently `1` |
| 6 | 2 | Image type: `0` object, `1` executable |
| 8 | 2 | Header size, currently `64` |
| 10 | 2 | Flags, reserved zero |
| 12 | 4 | Section count |
| 16 | 4 | Symbol count |
| 20 | 4 | Relocation count |
| 24 | 4 | Entry address |
| 28 | 4 | Link base address |
| 32 | 4 | String-table offset |
| 36 | 4 | String-table size |
| 40 | 4 | File size |
| 44 | 4 | CRC32 of bytes `64..file_size` |
| 48 | 16 | Reserved zero |

## Section entry

Each section entry is 32 bytes.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Name offset in the string table |
| 4 | 2 | Type: text 1, rodata 2, data 3, bss 4, debug 5, other 0 |
| 6 | 2 | Flags: alloc 1, write 2, execute 4 |
| 8 | 4 | Address (zero in objects) |
| 12 | 4 | Payload offset (zero for `.bss`) |
| 16 | 4 | Size in bytes |
| 20 | 4 | Alignment |
| 24 | 8 | Reserved zero |

`.bss` sections have no payload; their size reserves zero-filled memory at link
time. Debug sections use the same entry shape and can be extended with new
section names without changing the header.

## Symbol entry

Each symbol entry is 20 bytes.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Name offset |
| 4 | 4 | Value: section-relative in objects, absolute in executables |
| 8 | 4 | Size |
| 12 | 2 | Section index, or `0xFFFF` for undefined |
| 14 | 1 | Bind: local 0, global 1, weak 2 |
| 15 | 1 | Reserved zero |
| 16 | 4 | Reserved zero |

## Relocation entry

Each relocation entry is 20 bytes.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Section index |
| 4 | 4 | Offset within the section |
| 8 | 4 | Symbol-name offset |
| 12 | 4 | Addend |
| 16 | 2 | Type: `1` absolute 32, `2` PC-relative 32, `3` absolute 16 |
| 18 | 2 | Reserved zero |

Relocations name their target symbol directly. This keeps the object model
independent of symbol-table ordering and lets the linker resolve externs by
name.

## Layout

Tables follow the header immediately. The string table follows the tables, and
section payloads follow after 16-byte alignment. Section payloads are aligned to
their declared alignment. The CRC covers everything after the header, including
the tables, strings, and payloads.

## Linker contract

- `.text` is allocated, executable, and read-only.
- `.rodata` is allocated and read-only.
- `.data` is allocated and writable.
- `.bss` is allocated and writable, with no file payload.
- Debug sections are not allocated into the run image by default but remain in
  the executable for the debugger and `size`.
- Global symbols are resolved by name; locals remain object-scoped.
- The linker defines `__heap_start`, `__heap_end`, `__stack_top`, and
  `__stack_limit`. `__heap_end` reserves 64 KiB below the stack top for the
  runtime stack.
- `strip` removes local symbols and relocation records from the output.

## Versioning

Readers reject unknown magic, version, header size, truncation, out-of-range
tables, and checksum mismatches. New section types and optional debug sections do
not require a version bump; changing the fixed entry layouts does.
