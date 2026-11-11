# HowToDebugUsingKR32

Instructions for running and debugging externally compiled KR32 binaries with
the `kr32.exe` built in this repository.

This is written for a compiler/runtime author. It assumes the other toolchain
emits KR32 machine code; it does not require the KR32 C or C++ compilers.

The host build used here:

```text
C:\Users\kevin\source\repos\KR32\build\windows-msvc\Debug\kr32.exe
```

Linux equivalent:

```text
/mnt/c/Users/kevin/source/repos/KR32/build/linux-gcc/kr32
```

Tool version:

```text
kr32 0.1.0 (ISA 0.2-draft, container 1, M7 host services)
```

No SDL2 DLL is required for `vm`, `dis`, `debug`, or `dap`. It is only needed
for the GUI.

---

## 1. Short version

If your compiler emits a **flat binary**:

```powershell
$KR32 = "C:\Users\kevin\source\repos\KR32\build\windows-msvc\Debug\kr32.exe"
& $KR32 vm .\tests\hello.bin --base 0x00010000 --max-steps 5000000 --stats
```

If your compiler emits a **KR32 `.k32` object**, link and run it:

```powershell
& $KR32 as .\tests\hello.s --object -o .\tests\hello.o
& $KR32 ld .\tests\hello.o --base 0x00010000 -o .\tests\hello.k32
& $KR32 vm .\tests\hello.k32
```

For source-level DAP or GUI debugging, use a `.k32` **executable**. A raw
`.bin` cannot be loaded by `dap` or `gui`.

---

## 2. Flat `.bin` loading

### 2.1 What `vm` does with a raw file

For a non-`.k32` file:

1. read the entire file as bytes;
2. load those bytes starting at `--base`;
3. set `PC = --base`;
4. set `SP` to the top of the configured RAM;
5. build the `argc`/`argv` block on the stack unless `--bios` was supplied;
6. start executing at the first byte of the file.

There is no ELF/COFF parser and no relocation step. The first byte of the file
must be the first instruction you want to execute. Instructions are
little-endian 32-bit words; emit them in target byte order and keep the load
base and instruction stream 4-byte aligned.

### 2.2 Default addresses

| Item | Value |
| --- | --- |
| Default load address / PC | `0x00010000` |
| RAM base | `0x00010000` |
| Default RAM size | `0x08000000` (128 MiB) |
| Default stack top | `0x08010000` |
| Alignment | stack top and argument block are 16-byte aligned |

`--base` changes the load address and initial PC. It does not change the stack
top. The standalone `vm` command identifies containers by the `.k32` **file
extension**, not by magic; a raw image must not be named `.k32`.

```powershell
& $KR32 vm .\hello.bin --base 0x00010000
& $KR32 vm .\hello.bin --base 0x00400000
```

Use the same base your linker/compiler used. Raw KR32 code is normally
position-dependent; the bytes are not relocated by `kr32.exe`.

If your compiler emits a file with an ELF/COFF/container header, **do not point
`vm` at the container file as if it were flat**. Extract the loadable image
first. The raw path expects code/data only.

### 2.3 Stack and image collision

`--base`/`--ram-size` control the RAM window:

```powershell
& $KR32 vm .\hello.bin --base 0x00010000 --ram-size 0x08000000
```

The stack grows down from `RAM base + RAM size`. Do not place the loaded image
so high that it overlaps the stack. For small test programs, the default
`0x00010000`-based layout is safest.

### 2.4 Entry and startup contract

The VM does not provide a C runtime, CRT startup, heap, paging, or syscall
wrappers. Raw programs start in kernel mode with paging off; this is a flat
machine test environment, not a KR32OS user process. Your program must provide
its own startup code.

At the initial PC:

- `PC = load base`
- `SP = stack top` (16-byte aligned)
- `R0 = argc`, including `"program"` as `argv[0]`
- `R1 = argv`, an array of `argc` pointers followed by a null pointer

The argument strings and pointer array are already placed on the stack.
`SP` points at the start of that argument area. If your runtime does not want
the argument block, simply ignore `R0`/`R1` and establish your own stack usage.

There is no implicit return path. End through one of:

```asm
MOV R0, #0
SYSCALL #32        ; exit(0)
```

or:

```asm
HALT               ; exit code falls back to R0 & 0xFF
```

Returning with `RET` from the entry instruction is not a defined program exit.

### 2.5 `--bios` is not for standalone programs

If you pass `--bios`, `kr32.exe` loads the ROM, resets to the ROM entry, and
runs the BIOS. It does **not** start your flat image at `--base`.

For an externally compiled flat program, omit `--bios`. Use `--bios` only when
testing a BIOS/boot image.

---

## 3. Standard input and output

### 3.1 There is no terminal stdin

The CLI does not attach a TTY. Guest input is a finite preloaded string:

```powershell
& $KR32 vm .\app.bin --base 0x10000 --input "2`n3`n"
```

Bash/WSL:

```bash
"$KR32" vm app.bin --base 0x10000 --input $'2\n3\n'
```

The host `getchar` syscall consumes this string and returns `0xFFFFFFFF`
(`-1`) when it is empty. It does not block waiting for a real keyboard.
`--serial-input` is for KR32OS serial devices; standalone `vm` tests use
`--input`.

### 3.2 Guest output is `putchar`, not POSIX `write(1)`

The host has these relevant syscalls:

| Vector | Name | Arguments | Result |
| --- | --- | --- | --- |
| 32 | exit | `R0=code` | halts the machine |
| 33 | putchar | `R0=character` | `0` |
| 34 | getchar | none | byte or `0xFFFFFFFF` |
| 35 | open | `R0=path`, `R1=flags` | fd or negative error |
| 36 | close | `R0=fd` | 0 or error |
| 37 | read | `R0=fd`, `R1=buffer`, `R2=count` | bytes or error |
| 38 | write | `R0=fd`, `R1=buffer`, `R2=count` | bytes or error |
| 39 | seek | `R0=fd`, `R1=offset`, `R2=whence` | position or error |
| 42 | argc | none | count of `--arg` values |
| 43 | arg | `R0=index`, `R1=buffer`, `R2=size` | copied length |

`SYSCALL #33` is the console output path. `kr32 vm` echoes each character to
its own stdout immediately. A runtime that implements `printf`, `puts`, or
`putchar` should route output through vector 33.

There are **no pre-opened stdout/stderr descriptors**. Vector 38 writes only to
file descriptors returned by vector 35. A runtime that calls
`write(1, ...)` or `write(2, ...)` will not reach the CLI console; use
`SYSCALL #33` or open a real file.

### 3.3 Arguments

There are two related mechanisms:

- At process entry: `R0` = argc, `R1` = argv (includes `"program"`).
- At runtime: `SYSCALL #42` returns the number of `--arg` values, and
  `SYSCALL #43` copies a `--arg` value to a guest buffer.

Example:

```powershell
& $KR32 vm .\app.bin --base 0x10000 --arg alpha --arg beta
```

The entry `argc` includes the implicit `"program"` name; `sys_argc` returns
only the two explicit `--arg` values.

### 3.4 File I/O

```powershell
& $KR32 vm .\app.bin --base 0x10000 `
  --fs-root .\sandbox --read-only --quota 65536
```

Guest paths are relative to the host root (default: current directory).
`--read-only` rejects writes and write-open modes. `--quota` limits total
bytes written through the host. See `docs/host.md` for the full flag and
error contract.

---

## 4. Running tests from the CLI

### 4.1 Basic invocation

```powershell
$KR32 = "C:\Users\kevin\source\repos\KR32\build\windows-msvc\Debug\kr32.exe"

& $KR32 vm .\tests\program.bin `
  --base 0x00010000 `
  --input "7`n" `
  --max-steps 5000000 `
  --stats
$code = $LASTEXITCODE
```

Bash:

```bash
KR32=/mnt/c/Users/kevin/source/repos/KR32/build/linux-gcc/kr32
"$KR32" vm tests/program.bin \
  --base 0x00010000 \
  --input $'7\n' \
  --max-steps 5000000 \
  --stats
code=$?
```

### 4.2 Exit codes

`vm` returns the guest exit code as its process exit code:

- `SYSCALL #32` sets the guest exit code.
- `HALT` without an explicit exit syscall uses `R0 & 0xFF`.
- A fault prints `FAULT ...` and returns `1`.
- Reaching `--max-steps` prints `LIMIT after ...` and returns `1`.

The default step limit is `1,000,000`. For compiler-generated code, pass a
larger limit while bringing the backend up:

```powershell
& $KR32 vm .\program.bin --base 0x10000 --max-steps 50000000
```

### 4.3 Useful diagnostics

```powershell
# Disassemble a raw image at its load address
& $KR32 dis .\program.bin --base 0x00010000 --max-words 200

# Execute with a PC/word trace. Very noisy; use for short traces only.
& $KR32 vm .\program.bin --base 0x10000 --trace --max-steps 500

# Print instruction ticks after a halt
& $KR32 vm .\program.bin --base 0x10000 --stats
```

`--trace` writes to stdout before each instruction, interleaved with guest
`putchar` output. Do not use it for normal automated output matching.

### 4.4 Python harness

If your test driver is Python, use an argument list and read both streams:

```python
import subprocess

kr32 = r"C:\Users\kevin\source\repos\KR32\build\windows-msvc\Debug\kr32.exe"
result = subprocess.run(
    [kr32, "vm", "tests/program.bin",
     "--base", "0x00010000",
     "--input", "7\n",
     "--max-steps", "5000000"],
    capture_output=True,
    text=True,
)

print("exit:", result.returncode)
print("stdout:", result.stdout)
print("stderr:", result.stderr)

assert result.returncode == 0, result.stdout
assert "expected output" in result.stdout
```

The final register dump and `HALTED exit=N` line are appended to stdout.

---

## 5. Turning a flat `.bin` into a `.k32` for DAP/GUI

`kr32 dap` and `kr32 gui` accept **KR32 container executables**, not raw flat
files. If your compiler only emits a flat image, wrap it first.

### 5.1 Wrapper generator

Save this as `wrap_flat.py`:

```python
import sys

source, target = sys.argv[1], sys.argv[2]
data = open(source, "rb").read()
data += b"\0" * ((-len(data)) % 4)

with open(target, "w", encoding="ascii") as out:
    out.write(".section .text\n")
    out.write(".global _start\n")
    out.write("_start:\n")
    for offset in range(0, len(data), 4):
        word = int.from_bytes(data[offset:offset + 4], "little")
        out.write(f"    .word 0x{word:08X}\n")
```

Generate, assemble, and link:

```powershell
python .\wrap_flat.py .\tests\program.bin .\tests\program_wrapped.S
& $KR32 as .\tests\program_wrapped.S --object -o .\tests\program_wrapped.o
& $KR32 ld .\tests\program_wrapped.o --base 0x00010000 -o .\tests\program.k32
```

The `--base` passed to `ld` must match the address the flat binary was built
for. The wrapper becomes one `.text` section and does not preserve original
section names or writable-data flags; it is a runnable image, not a faithful
reconstruction of your linker's section layout.

Verify the executable and entry with:

```powershell
& $KR32 size .\tests\program.k32
& $KR32 dis .\tests\program.bin --base 0x00010000 --max-words 100
```

### 5.2 Better: emit KR32 objects directly

If your compiler can emit KR32 assembly or a KR32 object, avoid the wrapper:

```powershell
& $KR32 as .\tests\program.s --object -o .\tests\program.o
& $KR32 ld .\tests\program.o --base 0x00010000 -o .\tests\program.k32
```

This preserves `_start`, sections, symbols, and any `.file`/line debug records
you emit.

---

## 6. DAP

### 6.1 Start the adapter

Both forms require a `.k32` executable:

```powershell
# DAP over stdin/stdout; stdout is reserved for DAP framing.
& $KR32 dap --stdio .\tests\program.k32

# DAP over loopback TCP.
& $KR32 dap --port 4711 .\tests\program.k32
```

The TCP form prints `DAP listening on 127.0.0.1:4711` to stdout, then serves
DAP on the socket. Do not expect guest program output in that stdout.

The `--stdio` form uses DAP base protocol framing:

```text
Content-Length: <bytes>\r\n
\r\n
<UTF-8 JSON request>
```

A DAP client library is strongly recommended.

### 6.2 Request sequence

1. `initialize`
2. `launch` with `arguments.program` (or `arguments.image`) pointing at the
   `.k32`
3. `setBreakpoints` for source lines (optional)
4. `configurationDone`
5. `continue`, `next`, `stepIn`, `stepOut`, `stackTrace`, `scopes`,
   `variables`, `readMemory`, `evaluate`, `disconnect`, or `terminate`

`launch` accepts only the image path. It does not accept `--base`, guest
arguments, or stdin text. Use the CLI for those.

### 6.3 What DAP does and does not do here

Implemented:

- one guest thread (`threadId` 1)
- source-line breakpoints, if the image carries debug line records
- call stack and variables, if the image carries function/variable debug
  records
- register/memory/expression inspection through `evaluate` and `readMemory`
- instruction stepping through `next`, `stepIn`, and `stepOut`; these map to
  the instruction-level session operations, not source-line stepping
- `continue` in slices of up to 200,000 instructions per request

Important limitations:

- **Raw `.bin` is not accepted by DAP.** Use a `.k32`.
- **Guest console output is not forwarded as a DAP `output` event.** The
  session captures it internally, but the DAP server does not emit it. Use
  the CLI to verify stdout.
- **There is no DAP input injection.** Use the CLI `--input` for programs that
  read `getchar`.
- `setBreakpoints` matches by line number and does not filter by source path.
  Avoid duplicate line numbers across files, or emit one debug source per
  test image.
- A wrapper-generated `.k32` has no source/function metadata. DAP can start at
  entry and step instructions, but source breakpoints and variables will not
  verify.
- `stepOut` requires frame metadata; with no `F` records it falls back to a
  single instruction.

For source-level DAP, emit KR32 debug records (`.file`, `L`, and `F` records)
and produce the `.k32` with `kr32 as --object` + `kr32 ld`; `kr32 debug`
prints the resulting source/function/line tables:

```powershell
& $KR32 debug .\tests\program.k32
```

The record format is documented in `docs/debug.md`; the container layout is in
`docs/container.md`.

### 6.4 Minimal DAP launch body

After `initialize` and the `initialized` event:

```json
{
  "seq": 2,
  "type": "request",
  "command": "launch",
  "arguments": {
    "program": "C:\\Users\\kevin\\source\\repos\\KR32\\tests\\program.k32"
  }
}
```

Then send `setBreakpoints`, `configurationDone`, and `continue` using your DAP
client library.

---

## 7. GUI debugging (optional)

The GUI also requires a `.k32`:

```powershell
& $KR32 gui .\tests\program.k32
```

The GUI can set instruction breakpoints in the disassembly gutter and inspect
registers/memory even when no source debug records exist. If you only have a
flat `.bin`, wrap it first with the procedure in section 5.

For KR32OS itself, use:

```powershell
& $KR32 gui --os --embedded
```

That is unrelated to a standalone compiler test binary.

---

## 8. Recommended workflow for a new compiler

1. **Start with CLI functional tests.** Emit a flat `.bin`; run it with
   `kr32 vm ... --base <your link address> --input ...`.
2. **Match the exit ABI early.** End with `SYSCALL #32`; verify the process
   exit code in your test harness.
3. **Route output through `SYSCALL #33`.** There is no POSIX stdout descriptor.
4. **Use `--max-steps` generously.** The default is only 1,000,000.
5. **Add `--trace` only for short instruction-level failures.**
6. **For DAP, produce `.k32`.** If the compiler can emit KR32 assembly with
   debug records, link it directly; otherwise use the flat wrapper for
   instruction-level debugging only.
7. **Use the CLI for stdin/stdout assertions.** DAP has no guest-output event
   and no input injection.
8. **Verify source metadata independently** with `kr32 debug image.k32` before
   blaming the DAP client.

---

## 9. Troubleshooting checklist

| Symptom | Likely cause |
| --- | --- |
| `image does not fit at 0x...` | `--base` too high or image larger than RAM |
| `LIMIT after N steps` | program did not exit; increase `--max-steps` or fix startup |
| `FAULT ...` | bad opcode, alignment, or faulting load/store |
| Guest prints nothing | output went through `write(1)` instead of `SYSCALL #33` |
| `getchar` returns immediately at `-1` | no `--input` text, or input already consumed |
| DAP says `not an executable` / container error | passed a raw `.bin` instead of `.k32` |
| DAP breakpoints unverified | no `L`/`F` debug records, or wrong line number |
| Source pane has no lines | `.k32` lacks `.debug_info`; `kr32 debug` will show this |
| Address breakpoint needed | use GUI disassembly, or emit debug lines; DAP only sets source lines |

For the low-level syscall and sandbox details, see `docs/host.md`. For the
debug record format, see `docs/debug.md`.
