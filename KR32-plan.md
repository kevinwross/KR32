# KR32 — Virtual CPU and Toolchain Plan

> **Status:** draft 0.2 — planning. The decision log in §10 records what has
> actually been agreed; everything else is a proposal or an open question.
>
> **Working directory:** `C:\Users\kevin\source\repos\KR32`.
>
> **Implementation tracker:** [implementation.md](implementation.md).
>
> **Inputs reviewed:** `KR63.md`, `kr16-final-plan.md`, and `docs/isa-v1.md`.

## 1. What KR32 is

KR32 is a new virtual CPU, its emulator, and its toolchain. It is a fresh
design, not a fork: it takes the ideas that worked in KR63 and KR16 and leaves
behind the parts that only made sense in those machines.

v1 is a full platform: user mode, an MMU with a paging fault model, multicore,
scalar FP, and a 4x4 matrix extension. A C++ toolchain is in scope as a
separate, long-term workstream.

The name is taken to mean a **32-bit machine** (32-bit registers and a 32-bit
address space). That assumption is itself an open question (§11, Q2) because
KR63 is also a 32-bit machine and the two projects need distinct identities.

## 2. Goals (draft)

These are candidates to confirm in the interactive session.

1. **A clean, legible 32-bit ISA** — small enough to hold in one's head, regular
   enough to decode in a few hundred lines, with no legacy encoding accidents.
2. **A deterministic reference emulator** — same image, same initial state, same
   final state, every run. Determinism is a testability feature, not an
   implementation detail.
3. **A complete toolchain in one executable** — assembler, linker,
   disassembler, image format, C/C++ compilers, CLI, shell, and a windowed
   debugger, all in one `kr32` binary.
4. **Honest scope** — each milestone has an exit criterion and the spec names
   what is deliberately out of scope.
5. **Spec-first development** — the frozen encoding lives in a versioned
   document; code and tests are derived from it, not the other way round.
6. **A C++ toolchain, eventually** — a freestanding C++ compiler and runtime for
   KR32, treated as its own workstream rather than a v1 deliverable.

## 3. Non-goals (draft)

- No attempt to be binary-compatible with KR63 or KR16 unless that is explicitly
  chosen.
- No host OS emulation, no networking, no graphics hardware, no USB in v1.
- No cycle-accurate microarchitecture; "cycles" are retired instructions.
- No wall-clock-dependent behaviour in the reference model.
- No hosted C++ standard library (`<iostream>`, `<filesystem>`, threads); the
  compiler targets a freestanding subset.

## 4. Design principles

1. **One canonical spec.** Decisions live in one place, with a revision history.
   The encoding is frozen at a named revision; changes ship as `isa-v1.1`, not as
   quiet edits.
2. **Every access goes through one seam.** Instruction fetch, loads, stores,
   stack traffic, page walks, and vector fetches all pass through the MMU /
   memory interface. No instruction bypasses translation.
3. **Faults are architecture, not host exceptions.** Guest-visible faults are
   delivered through the vector table; C++ exceptions are reserved for host-side
   problems at the CLI boundary.
4. **Determinism first.** A cooperative scheduler and instruction-count clock
   make every run reproducible. Host threads, if ever added, are an opt-in
   backend behind the same interface.
5. **Make the invalid case loud.** Guard page at address zero, reserved opcodes
   raise `#UD` (never NOP), misaligned `PTBR` raises `#GP`, writes to read-only
   registers raise `#GP`.
6. **Golden vectors pin the encoding.** Hand-derived byte sequences, not
   emulator output, are the authority for assemble/disassemble tests.
7. **Documentation ships with the milestone.** A milestone is not done until the
   README and spec match the shipped state.

## 5. Candidate machine sketch (v0 — to be confirmed)

This is a starting point for discussion, deliberately close to KR63 where KR63
got it right and deliberately silent where KR32 should choose differently.

### 5.1 CPU state

- 16 general-purpose 32-bit registers `R0..R15`; no hard-wired zero.
- 16 floating-point 64-bit registers `F0..F15`, separate from the GPRs (§5.8).
- `PC`, `SP`, `FLAGS` as separate architectural state.
- Flags `Z N C V` plus `I` (interrupt enable) and `K` (kernel mode).
- A small control-register file: at minimum a vector-table base, a fault
  address, a memory-control register, and a core id.
- 64-bit retired-instruction counter per core.

### 5.2 Memory

- 32-bit little-endian address space.
- **64 KiB guard region** at `0x0000_0000..0x0000_FFFF`, never backed, so NULL
  and small offsets fault instead of silently succeeding.
- RAM from `0x0001_0000`; 128 MiB installed by default.
- A 960 KiB memory-mapped I/O window at `0xFFF0_0000..0xFFFE_FFFF`.
- A 64 KiB ROM at `0xFFFF_0000..0xFFFF_FFFF` holding the reset vector and BIOS.
- **Decided:** v1 has user mode, an MMU, and a paging fault model. Pages are
  4 KiB with a two-level 10 + 10 walk; the full map, PTE format and fault
  contract are in §5.11.

### 5.3 Encoding (decided)

Instructions are 32-bit words, optionally followed by one 32-bit extension word.
The base word is:

| Bits | Field |
| --- | --- |
| 31..25 | opcode (7) |
| 24..21 | Rd (4) |
| 20..17 | Rs1 (4) |
| 16..13 | Rs2 / mode / condition (4) |
| 12 | E: a 32-bit extension word follows |
| 11 | L: the payload is a literal |
| 10..0 | inline value, signed 11-bit |

Rules:

1. `E = 1` means the next four bytes are a little-endian 32-bit payload; the
   instruction is 8 bytes and the inline field is ignored.
2. `E = 0` means the payload is the inline field sign-extended from 11 bits.
3. `L = 1` means the payload is a literal, immediate, or displacement; `L = 0`
   means the field names a register.
4. At most one extension word; chained extensions are not part of v1.
5. The assembler emits the shortest encoding that represents the value and
   relaxes labels when a displacement changes size. A `.long` or force-long
   directive is available when predictable sizes matter.
6. Reserved opcodes and reserved modes raise `#UD`.

The 7-bit opcode provides 128 top-level slots. FP, M, and byte/bit each get a
separate escape opcode with the same 4-bit fields and an 11-bit function code
in bits 10..0; `E` and `L` remain available to the class.

**Branches.** `JMP` is unconditional. `JCOND` carries a 4-bit condition in the
`16..13` field; the target is `Rs1` when `L = 0` or the inline/extension word
when `L = 1`. The assembler provides `JZ`, `JNZ`, `JCS`, `JNC`, `JMI`, `JPL`,
`JVS`, `JVC`, `JHI`, `JLS`, `JGE`, `JLT`, `JGT`, and `JLE` as aliases.

**Loads and stores.** Width and sign are distinct opcodes: `LOAD`, `LOAD8U`,
`LOAD8S`, `LOAD16U`, `LOAD16S`, `STORE`, `STORE8`, `STORE16`.

**Addressing modes.** The `16..13` field selects a mode for the non-indexed
memory family:

| Value | Mode | Effective address |
| --- | --- | --- |
| 0 | register direct | not a memory mode |
| 1 | immediate literal | payload is data, not an address |
| 2 | register indirect | `[Rs1]` |
| 3 | indexed | `[Rs1 + disp]` |
| 4 | stack-relative | `[SP + disp]` |
| 5 | absolute | `[disp]` |
| 6 | PC-relative | `[instruction address + disp]` |
| 7..15 | reserved | `#UD` |

Base+index is a separate instruction family, not a mode: `LOADX`, `LOAD8UX`,
`LOAD8SX`, `LOAD16UX`, `LOAD16SX`, `STOREX`, `STORE8X`, and `STORE16X`. In these
the `16..13` field names the index register, `Rs1` is the base, and the
displacement is inline or in the extension word. This keeps the mode field
unambiguous and gives `[Rb + Ri + disp]` without splitting the immediate. `LEA`
uses the mode table and computes an address without accessing memory.

The draft opcode map and instruction tables are in `docs/isa-v1.md`.

### 5.4 Privilege and exceptions

- Kernel/user split with a kernel stack pointer and a shared interrupt frame
  (`error code`, `saved SP`, `FLAGS`, `PC`).
- A 256-entry vector table: low vectors for exceptions, the middle for hardware
  lines, the top for software `INT`/`SYSCALL`.
- `#UD`, `#DIV0`, `#BRK`, `#PF`, `#GP`, `#ALIGN`, `#DF` as the initial fault set.
- Kernel-only control-register access, `HALT`, and `IRET`.
- A `BRK` instruction for debugger integration.

The decided delivery contract, vector map, frame, and kernel-stack rules are in
§5.12.

### 5.5 Boot

- Reset enters kernel mode, interrupts disabled, paging off.
- `PC` is loaded from the first word of ROM; `SP` is initialised to the top of
  installed RAM.
- The boot ROM installs the vector table (and page tables, if paging exists)
  before transferring to the kernel or user entry point.

The decided reset and BIOS sequence is in §5.12.

### 5.6 I/O and time

- Memory-mapped only; no `IN`/`OUT` opcodes.
- A global `ticks` counter (sum of retired instructions) is the only clock.
- Devices advance at deterministic points in the scheduler turn.
- Interrupt routing table only if interrupts and multiple cores exist.

### 5.7 Concurrency

KR63's model — up to 128 core slots, only core 0 enabled at reset, deterministic
round-robin cooperative scheduling on one host thread, `LL`/`SC`/`FENCE`,
per-core reservations, an inter-processor block for bring-up and IPIs — is
coherent and proven. **Decided:** multicore is in v1; the core count and
scheduler details remain open (§11, Q5).

### 5.8 Floating point (decided)

**Status.** Decided 2026-11-01: separate **16 × 64-bit** file, `F0..F15`;
`float` in low 32 with the upper half zeroed; `double` full 64;
`FEQ`/`FLT`/`FLE` write a GPR boolean; RNE, canonical NaN, denormals supported,
no traps, saturating conversions. The ABI subsection remains proposed.

**Register file.** A separate file of **16 64-bit registers**, `F0..F15`, not
shared with `R0..R15`. A `float` occupies bits 31..0 and bits 63..32 are cleared
on every write; a `double` occupies all 64 bits. One register per value avoids
even/odd pairing, and FP temporaries never consume one of the 16 GPRs.

**Encoding.** FP arithmetic, compare, convert and move instructions use one
top-level escape opcode. Inside it the usual 4-bit fields are `fd`, `fs1`, and
`fs2`; `E` is bit 12, `L` is bit 11, and bits 10..0 are an 11-bit function code
selecting the operation and the `S`/`D` format. `FLOAD` and `FSTORE` are
separate top-level opcodes in the normal memory format: the `16..13` field is
the addressing mode, `Rs1` is the base register, and the `L` bit selects 32-bit
(`.W`) or 64-bit (`.D`) width while `Rd` names the FP register.

**Operations (base v1).**

| Group | Instructions |
| --- | --- |
| Memory | `FLOAD.W`, `FSTORE.W` (32-bit), `FLOAD.D`, `FSTORE.D` (64-bit) |
| Arithmetic | `FADD`, `FSUB`, `FMUL`, `FDIV`, `FSQRT` in `.S` and `.D` |
| Min/max | `FMIN`, `FMAX` in `.S` and `.D` |
| Sign | `FABS`, `FNEG` (bit operations on the FP register) |
| Compare | `FEQ`, `FLT`, `FLE` in `.S`/`.D`: write 0/1 to a GPR and set `Z` to `(result == 0)`, so `JNZ` branches when the predicate holds |
| Convert | `FCVT.S.D`, `FCVT.D.S`; `FCVT.W.S`/`FCVT.WU.S`/`FCVT.S.W`/`FCVT.S.WU` and `.D` equivalents |
| Move | `FMV.W.X`, `FMV.X.W` for raw 32-bit patterns between files |
| Later | `FFMA`, `FCLASS`, dynamic rounding, accrued exception flags |

Implicit promotion is not part of the ISA: every conversion is explicit. NaN
makes every ordered predicate false, so `!=` is `FEQ` followed by `JZ`, while
`>`/`>=` are `FLT`/`FLE` with the operands swapped.

**Semantics.** Round to nearest, ties to even. No FP traps in v1 and no
observable exception flags. All NaN results are canonical quiet NaNs
(`0x7FC00000` for `.S`, `0x7FF8000000000000` for `.D`) so results do not depend
on the host's NaN propagation. Denormals are supported. Out-of-range
float-to-integer conversions saturate.

**ABI (if C is in scope).** `F0..F7` carry arguments and return values,
`F8..F15` are callee-saved; only scalar `float`/`double` travel in FP registers;
aggregates pass in integer registers or memory. `long double` is not supported.

**Determinism.** The emulator stores raw 64-bit patterns and performs each
operation in host `float`/`double` with FP contraction disabled and strict
floating-point flags. The canonical-NaN rule removes the remaining
host-dependent payload differences, and a differential test compares host and
guest results.

FP is part of base v1, not an optional extension in the first profile. The file
holds 16 registers (§10, D12); 32 was rejected because it would require an
FP-specific 5-bit format and would not have made matrix multiply fit anyway —
three 4x4 matrices need 48 scalar values.

### 5.9 Matrix and vector extension (decided)

**Status.** Decided 2026-11-01: 16 tiles, extension M, vectors in row 0,
strided `MLOAD`, the op set below, row-major layout, and `f32`-only tiles.

**Idea.** A small tile unit for 4x4 `float32` matrices and 1x4 vectors. It is
the graphics primitive (model/view/projection transforms) and a legible
teaching model of the tiled matrix units behind modern AI accelerators.

**Register file.** A separate file of **16 tile registers** `T0..T15`, each
512 bits: a 4x4 matrix of `float32` lanes, row-major. Total 1 KB. A 1x4 vector
occupies row 0 of a tile; vector ops ignore rows 1..3 and clear them on write.

A matrix is **one tile**, not 16 scalar registers. A 4x4 matrix in the FP file
would consume all 16 FPRs, and `A * B` needs three matrices, so the scalar and
tile files stay separate. The 16 GPRs and 16 FPRs are unchanged.

**Instructions (extension M).**

| Group | Instructions |
| --- | --- |
| Memory | `MLOAD`/`MSTORE` (64 B, optional row stride), `VLOAD`/`VSTORE` (16 B) |
| Multiply | `MMUL Td, Ta, Tb`; `MMAC Td, Ta, Tb`; `MVEC Td, Ta, Tb` |
| Unary | `MTRANS Td, Ta` |
| Vector | `VDOT Fd, Ta, Tb` (scalar result); `VADD`, `VSCALE` later |
| Later | element-wise `MADD`, strided `MLOAD`, bf16/int8 tiles |

**Semantics.**

- Row-major. `MMUL`: `Td[i][j] = sum(k) Ta[i][k] * Tb[k][j]`, accumulated with
  fused multiply-add, round-to-nearest-even.
- `MMAC` adds into `Td`; `MVEC` computes `Td[0][i] = sum(k) Ta[i][k] * Tb[0][k]`
  and clears rows 1..3; `VDOT` writes `Fd = sum(k) Ta[0][k] * Tb[0][k]`.
- Sources are read before the destination is written, so `Td` may alias
  `Ta`/`Tb`.
- `MLOAD` reads 16 consecutive little-endian `float32` from `[addr]`; `VLOAD`
  reads 4. Both are page-checked per element, naturally 4-byte aligned, and not
  atomic.
- `MLOAD`/`MSTORE` take an optional row stride: with `E = 0` the transfer is one
  contiguous 64-byte block at `base + disp`; with `E = 1` the extension word
  holds the byte stride, the inline displacement must be zero, and the four
  16-byte rows sit at `base + r * stride`.
- `float32` only in v1; canonical NaN and no traps, as for scalar FP.

**Encoding.** One matrix escape opcode with an 11-bit function field; 4-bit
`Td`/`Ta`/`Tb` fields. `MLOAD`/`MSTORE` use the normal memory format with the
tile in the `Rd`/`Rs` field and a width bit selecting 64 B or 16 B.

**Scope.** Extension M: designed now, implemented after the integer/FP core.
It is present in the reference machine and reported by a capability bit; a
profile without it raises `#UD`.

**What it is not.** A 4x4 tile is a teaching size, not a competitive tensor
core. Real AI hardware uses larger tiles (8x8/16x16) and bf16/int8/fp8; the
useful lesson here is blocking a large matmul into 4x4 tiles and accumulating
with `MMAC`. A 4-wide dense layer is `MVEC` plus a bias add.

**Strided loads (decided).** The stride form is in v1 because contiguous
64-byte loads only work for packed blocks; tiling a larger row-major matrix
needs the row pitch. It is the one addition that makes the 4x4 unit useful on
real data.

### 5.10 Transform stack (decided)

**Status.** Decided 2026-11-01: **register-top** model. The current transform
lives in a tile; `TSP` addresses a memory stack of saved copies; `MMUL` stays
register-only.

**Purpose.** Graphics code keeps a stack of 4x4 transforms; the top is the
current model/view/projection. A dedicated stack makes nested transforms cheap
and gives students a concrete hardware feature rather than a software pattern.

**Model.** The *current* transform lives in a tile (any tile; `T15` by
convention). The memory stack holds saved copies. `TSP` is a 32-bit register
holding the address of the top saved matrix. The stack grows down, in 64-byte
entries, 64-byte aligned.

| Instruction | Effect |
| --- | --- |
| `MPUSH Td` | `TSP -= 64; mem[TSP] = Td` |
| `MPOP Td` | `Td = mem[TSP]; TSP += 64` |
| `MDROP` | `TSP += 64` (discard the top save) |
| `RDSP Rd` / `WRSP Rs` | read or write `TSP` |

The canonical sequence is push, multiply in place, draw, pop:

```asm
    MPUSH T15                  ; save the parent transform
    MLOAD T0, [local_transform]
    MMUL  T15, T15, T0         ; current = parent * local
    CALL  draw_scene
    MPOP  T15                  ; restore the parent
```

**Why not `MMUL [TSP]`.** Keeping `MMUL` register-only avoids a memory-
destination matrix form and keeps the hot transform multiply off the memory
bus. The push/multiply/pop sequence above is the same pattern as
`glPushMatrix`/`glMultMatrix`/`glPopMatrix`, with the top cache in a tile and
the stack as its backing store. A fused compute-and-push could be added later
as `MMUL.PUSH` if it earns its place, but it should not be the primitive.

**Bounds.** Guard pages at both ends of the stack region; a push or pop outside
the mapped region raises `#PF`. `TSP` is unchanged when the access faults, and
a misaligned `TSP` raises `#ALIGN`.

**Privilege and context.** `TSP` is user-accessible, like `SP`; `RDSP`/`WRSP`
are unprivileged. The kernel saves and restores `TSP` on a context switch. It
is per-core.

**Settled details.** `MPUSH`/`MPOP` name the tile they save or restore, so the
current tile is a convention rather than hard-coded. `MDROP` discards the top
save. Exact fault ordering is left to the ISA document.

### 5.11 Paging and machine model (decided)

**Address map (decided 2026-11-01).**

| Range | Region |
| --- | --- |
| `0x0000_0000..0x0000_FFFF` | 64 KiB guard region, never backed |
| `0x0001_0000..0x0800_FFFF` | RAM, 128 MiB by default |
| `0x0801_0000..0xFFEF_FFFF` | unbacked gap (shrinks as RAM grows) |
| `0xFFF0_0000..0xFFFE_FFFF` | 960 KiB MMIO window, 240 pages |
| `0xFFFF_0000..0xFFFF_FFFF` | 64 KiB ROM: reset vector and BIOS |

RAM starts at `0x0001_0000`; the default size is 128 MiB, and the maximum is
bounded by the MMIO base (`0xFFF0_0000 - 0x0001_0000` bytes). The reset vector
is the first little-endian word of ROM. The initial `SP` is one past the last
installed RAM byte, 16-byte aligned.

**Pages and tables.** 4 KiB pages; a two-level 10 + 10 walk over the 20-bit
virtual page number, with a 12-bit page offset:

```text
vpn  = VA >> 12
dir  = phys[PTBR + ((vpn >> 10) * 4)]
leaf = phys[(dir & 0xFFFFF000) + ((vpn & 0x3FF) * 4)]
PA   = (leaf & 0xFFFFF000) | (VA & 0xFFF)
```

`PTBR` holds the **physical** base of the directory and must be 4 KiB aligned;
a misaligned write raises `#GP`. Every table is 4 KiB and holds 1024 32-bit
entries.

**PTE format.** Bits 31..12 are the physical page number; bits 11..0 are flags:

| Bit | Flag | Meaning |
| --- | --- | --- |
| 0 | V | present |
| 1 | W | writable |
| 2 | U | user-accessible |
| 3 | X | executable |
| 4 | A | accessed; set by hardware |
| 5 | D | dirty; set by hardware on write |
| 6 | G | global; survives an invalidation |
| 7 | PS | reserved for 4 MiB pages; must be zero in v1 |
| 8..11 | | reserved |

Directory entries use the same layout: `V` plus the physical base of a leaf
table; the other flag bits are reserved. The hardware sets `A` on any successful
translation and `D` on a successful write.

**Access checks.** Fetch requires `V` and `X`; load requires `V`; store requires
`V` and `W`. User mode additionally requires `U` at both levels. `MCTL.WP`, when
set, requires `W` for kernel stores.

**Faults.** `#PF` uses `bit0 P | bit1 W | bit2 U | bit3 X`, where `P = 0` means
not present and `P = 1` means a protection violation; `FAR` receives the
faulting virtual address. `#GP` covers non-paging violations: a store to ROM or
to storage outside installed RAM with paging off, an invalid control-register
write, a privileged instruction in user mode, and an out-of-range software
vector. `#ALIGN` covers a misaligned instruction fetch and a misaligned `TSP`
access. `#DF` covers a fault during delivery; if `#DF` delivery itself fails,
the core halts.

**No TLB.** v1 has no translation cache. `PTBR` and `MCTL` writes invalidate
translation state (currently empty), and the hook exists so a TLB can be added
without changing call sites. Because the memory model is sequentially
consistent, page-table edits are visible to other cores without shootdowns.

**Address spaces.** The kernel is identity-mapped: kernel virtual addresses are
physical, and the same kernel mappings appear in every process page table. Each
process has its own `PTBR` and its own user pages marked `U`. The guard region
is never mapped in any address space. User and kernel share one 4 GiB space, so
pointers have the same numeric range but different permissions.

**Large pages.** Bit 7 is reserved for 4 MiB pages, but v1 does not implement
them; a set `PS` bit is a reserved-bit fault.

### 5.12 Exceptions, interrupts, and boot (decided)

**Vector table.** 256 little-endian 32-bit entries at the virtual address in
`VTBR`; entry `v` holds the handler address and must be 4-byte aligned. The
vector fetch goes through the MMU like any other load. A fault during delivery
escalates to `#DF`; if `#DF` delivery itself fails, the core halts and the
machine records a diagnosable halt reason.

| Vector | Source |
| --- | --- |
| 0 | reserved |
| 1 | `#UD` invalid opcode or encoding |
| 2 | `#DIV0` divide by zero |
| 3 | `#BRK` breakpoint |
| 4 | `#PF` page fault |
| 5 | `#GP` general protection |
| 6 | `#ALIGN` misaligned fetch or `TSP` access |
| 7 | `#DF` double fault |
| 8..15 | reserved |
| 16..31 | hardware interrupt lines 0..15 |
| 32..255 | software `INT` / `SYSCALL` |

`INT`/`SYSCALL` with a vector below 32 raise `#GP`, so software can never
collide with the exception or hardware ranges.

**Delivery frame.** One shape for faults, traps, and asynchronous interrupts:

```text
victim_pc = PC at the faulting instruction (fault)
            or the next instruction (trap or asynchronous interrupt)
saved_sp  = SP
if user mode:
    SP = KSP
push(error_code)
push(saved_sp)
push(FLAGS)
push(victim_pc)
FLAGS.K = 1
FLAGS.I = 0
PC = *(VTBR + vector * 4)
```

Because the stack grows down, `SP` points at the saved PC on entry to the
handler. `#PF` carries `P/W/U/X`; every other vector carries zero. `IRET` is
kernel-only: it pops PC, FLAGS, SP, and the error code in reverse, restores
`MODE` from `FLAGS.K`, and resumes. Faults save the faulting PC so `IRET`
retries the instruction; traps (`BRK`, `INT`, `SYSCALL`) save the following
instruction so `IRET` resumes after them.

**Kernel stack.** `KSP` is per-core. A user→kernel delivery switches to it;
kernel→kernel faults stay on the current kernel stack. The kernel saves and
restores `KSP` on a context switch.

**Interrupts.** Sixteen hardware lines. Each core has a pending bitmap, a mask
`IMR`, and a global `IE`. Before each instruction the CPU selects the lowest
pending line that is unmasked in `IMR` and enabled by `IE`, delivers vector
`16 + line`, and clears the pending bit. Software vectors bypass `IE`, `IMR`,
and the pending bitmap. One core raises a line on another through the
inter-processor block; there is no broadcast.

**Boot.** Reset enters kernel mode with interrupts disabled and paging off.
`PC` is loaded from the first little-endian word of ROM at `0xFFFF_0000`; `SP`
and `KSP` are set to one past the top of installed RAM, 16-byte aligned. The
BIOS runs from ROM, installs the vector table, builds page tables, identity-maps
RAM, ROM, and the MMIO window (never the guard region), loads `PTBR`, enables
paging via `MCTL.PGE`, and jumps to the kernel entry point. A fault before the
vector table is installed therefore escalates to `#DF` and halts, which is the
intended failure mode for a corrupt boot image.

### 5.13 Byte and halfword operations (proposal)

**The idea.** Add small 8-bit and 16-bit register files (say 2–4 registers each)
for characters and UTF-16 code units, with load, compare, and width-specific
operations, instead of doing byte work through the 32-bit GPRs.

**Recommendation: do not add them to the base machine.** The cost is compiler,
ABI, and workload complexity, not just encoding:

- **Integer promotions.** In C and C++, `char` and `short` arithmetic is done in
  `int`, so a compiler widens to 32 bits anyway. The special registers would be
  load/store holding pens, not the place arithmetic happens.
- **Unicode does not fit.** A UTF-16 code unit is 16 bits, but a code point is
  up to 21 bits, and surrogate pairs span two units. Encoding and decoding need
  32-bit shifts, masks, and compares, not a 16-bit ALU. `char32_t` is 32-bit by
  definition.
- **Text is memory-bound.** String and Unicode work is dominated by loads,
  compares, table lookups, and branches. Two to four registers do not change
  that; eight to sixteen is a full file with a full ABI and context-switch cost.
- **Width-specific flags.** An 8-bit add's carry is at bit 8, not bit 31, and a
  16-bit overflow is at bit 16. You either add size-aware flags and branches or
  mask and extend — which is the 32-bit path again.
- **No precedent for a reason.** Sub-registers exist (`x86` AL/AH), 8-bit CPUs
  have 8-bit files, and 32-bit RISC machines use width-specific loads with
  32-bit registers. Separate typed files are rare because the benefit is small.

**What to add instead.** Width-specific memory access plus general bit and byte
primitives, all on the existing GPRs:

| Group | Instructions |
| --- | --- |
| Loads | `LOAD8U`, `LOAD8S`, `LOAD16U`, `LOAD16S` (zero/sign extend) |
| Stores | `STORE8`, `STORE16` |
| Byte order | `BSWAP` |
| Bit fields | `BFX`/`BFI` (or `EXTR`/`INSR`) |
| Counting | `CLZ`, `CTZ`, `POPCNT` |
| Later | `UTF8DEC`/`UTF8ENC`, byte-lane SIMD |

`LOAD8U` + 32-bit `CMP` + branch covers byte comparison; `LOAD16U` does the same
for UTF-16 code units. Surrogate pairs, UTF-8 decoding, and case mapping are
shifts, masks, compares, and table lookups on 32-bit values.

**If you want the experiment.** Make it an optional character coprocessor, not
base and not in the C++ ABI: 8 × 8-bit and 8 × 16-bit registers,
`BLOAD`/`BSTORE`/`BCMP`, `WLOAD`/`WSTORE`/`WCMP`, maybe an ASCII class op. Two to
four registers is too few for real code; eight is the minimum that could hold a
short string slice. It is a teaching feature, not an efficiency win.

**Decision (2026-11-01).** Base primitives only: width-specific loads and stores
plus the byte/bit primitives above. No separate byte or halfword register files.
The optional character coprocessor stays parked.

## 6. Idea inventory from KR63

Each row is a candidate to adopt, adapt, defer, or reject. "Adopt" means the
idea carries over essentially unchanged; "Adapt" means the idea is right but a
detail should change; "Defer" means it belongs in a later milestone; "Revisit"
means the idea is suspect for KR32.

### 6.1 Architecture

| # | Idea (KR63 §) | What it buys | KR32 stance |
| --- | --- | --- | --- |
| A1 | Fixed 32-bit words + optional extension word (§3) | Trivial fetch/decode; predictable instruction sizes | **Adopt** |
| A2 | 16 GPRs, no hard-wired zero (§1) | Simple register file; avoids zero-register asymmetries | **Adopt** |
| A3 | `Z N C V` flags with borrow-carry `C` (§1, §4.3) | Familiar for C codegen and conditional branches | **Adopt** |
| A4 | Separate sized load/store opcodes (§4.2) | Simple decode | **Adapt** — consider a width field to save opcode slots |
| A5 | 4-bit addressing-mode field + `LEA` (§3.3) | Compact memory syntax without a full ModR/M | **Adopt**; consider a base+index mode |
| A6 | PC-relative base is the instruction address (§3.3) | Simple, consistent with absolute label resolution | **Adopt** |
| A7 | Guard page at virtual/physical 0 (§2.1) | NULL dereferences fault in both paging modes | **Adopt** |
| A8 | Two-level page table, per-core `PTBR`, A/D bits (§2) | Real user/kernel isolation without a TLB | **Adapt** — 4 KiB pages, 10 + 10, hardware A/D (decided) |
| A9 | Vector table + error codes + `IRET` frame (§6) | Standard, compact trap model | **Adopt** |
| A10 | `#DF` and halt-on-failed-delivery (§6.5) | Diagnosable corrupt-boot failures | **Adopt** |
| A11 | `LL`/`SC`/`FENCE` instead of `CAS` (§12.2) | Enough for spinlocks and refcounts; no special decoder case | **Adopt** — multicore is in v1 |
| A12 | MMIO only, no `IN`/`OUT` (§13) | One address space; no separate I/O instruction class | **Adopt** |
| A13 | Instruction-count `ticks` clock (§13.5) | Reproducible device timing | **Adopt** |
| A14 | 128 core slots, cooperative round-robin (§12) | Reproducible concurrency without host threads | **Adopt** — cooperative scheduler; core count TBD |
| A15 | Inter-processor block and IRQ route table (§12.4, §13.6) | Clean multicore bring-up without new instructions | **Adopt** — part of v1 multicore |
| A16 | Physical DMA, trusted devices, no IOMMU (§13) | Simple device model | **Defer** |
| A17 | Signed ops and short branches arrived only in `v1.1` (§17.2) | — | **Lesson:** put signed loads, `SAR`, signed `DIV`/`MOD`, and `JLT/JGE/JLE/JGT` in base v1 |
| A18 | 6-bit opcode field exhausted (§4.1, §17.2) | — | **Lesson:** plan opcode headroom or an escape from the start |
| A19 | No TLB in v1 (§2.4, §8.3) | Fewer moving parts; invalidation hook still exists | **Adopt** for v1 |
| A20 | `INT`/`SYSCALL` vectors below 32 fault (§4.6) | Software can never collide with exceptions or IRQ lines | **Adopt** |

### 6.2 Process and tooling

| # | Idea (KR63 §) | What it buys | KR32 stance |
| --- | --- | --- | --- |
| P1 | Locked-decisions table + revision status (§0) | One screen answers "what did we decide?" | **Adopt** |
| P2 | Frozen `isa-v1` document, later revisions as `v1.1` (§17.2) | Encodings never shift under existing images | **Adopt** |
| P3 | One CLI with `assemble` / `disassemble` / `run` / `dap` (§8.5) | One artifact to build, document, and test | **Adopt** |
| P4 | Raw `.rom` image + `.kdbg` JSON sidecar (§8.6) | Boot image stays plain; debug data stays optional | **Adopt** |
| P5 | Assembler syntax: case-insensitive mnemonics, `#` immediates, `[R5+8]` forms (§9.1) | Familiar, terse, easy to parse | **Adopt** |
| P6 | Golden byte vectors derived from the spec (§11 M0) | Pins encoding independently of the emulator | **Adopt** |
| P7 | DAP over stdio, `launch` only, all-stop across cores (§10) | Works with the existing VS Code extension | **Adopt** (all-stop only if multicore) |
| P8 | CMake presets, warnings-as-errors, MSVC + GCC + Clang CI (§8.1) | Portability enforced continuously | **Adopt**, unless the host stack changes |
| P9 | doctest single header (§8.1) | No package manager, no network at configure time | **Adopt** |
| P10 | Milestones with explicit exit criteria (§11, §16) | Progress is verifiable, not vibes | **Adopt** |
| P11 | Property tests for decoder and assembler (§16.1) | Random input never crashes the tools | **Adopt** |
| P12 | ASan/UBSan preset (§8.1, §16.1) | Catches memory bugs before they become mysteries | **Adopt** |
| P13 | Deliberately-out-of-scope list (§16.8) | Prevents scope creep by naming it | **Adopt** |

## 7. What to take from KR16

Not yet reviewed. Once `kr16-plan.md` is read, this section becomes a second
inventory table in the same shape as §6. Candidate areas to compare: CPU
architecture, assembler/linker split, C compiler, VM host, and roadmap style.

## 8. Repository and document layout (draft)

```text
KR32/
  KR32-plan.md            this document: goals, decisions, roadmap
  CMakeLists.txt, CMakePresets.json
  cmake/                  platform-boundary and helper checks
  include/kr32/           public engine headers
  src/
    core/                 CPU, machine, MMU, devices
    asm/ link/ container/ assembler, linker, .kr32 format
    cc/ cxx/              C and C++ compilers
    dis/ debug/           disassembler, debug info, DebugSession
    vm/ dap/              runner, host services, DAP
    ui/                   portable debugger layout and painting
    platform/             SDL2 window, canvas, sockets, terminal
    cli/                  dispatch, build pipeline, shell
    resources/            embedded runtime and fonts
  docs/                   isa, machine, abi, devices, debug format
  tests/                  unit, integration, golden, support
  examples/
  packaging/
```

A single repository is assumed. `src/ui` contains no OS or SDL headers;
`src/platform` is the only OS/SDL boundary, enforced by a build check. The
engine and UI are libraries; the `kr32` executable links all of them. The UI
layer starts from KR16's portable `src/ui`; its SDL backend is ported to SDL2.

## 9. Toolchain scope (draft)

| Component | v1? | Notes |
| --- | --- | --- |
| Emulator core | Yes | Deterministic, spec-derived |
| Assembler | Yes | Core subset first, then full ISA |
| Linker | Yes, after M2 | Sections, symbols, relocations, map |
| Container | Yes, after M2 | `.kr32` objects and images; debug sections |
| Disassembler | Yes | Contiguous range; symbols and lines |
| Debug model | Yes | `DebugSession` shared by GUI, DAP, CLI, tests |
| CLI | Yes | `as`, `ld`, `build`, `cc`, `cxx`, `vm`, `dis`, `size`, `debug`, `dap`, `shell` |
| GUI debugger | Yes | Custom portable UI over SDL2; `KR32_BUILD_UI` (default ON) |
| Shell | Yes | `kr32 shell` |
| Host services | Yes | `SYSCALL` ABI, sandbox root/quota/read-only |
| DAP | Yes, secondary | VS Code compatibility; shares `DebugSession` |
| C compiler | Yes (bootstrap) | Assumed bootstrap path for C++ |
| C++ compiler | Yes, long-term | `kr32_cxx` module linked into `kr32` when ready |
| Packaging | Yes | Windows exe, Linux AppImage; embedded runtime/fonts |

### 9.1 Product architecture (decided)

KR32 ships as **one `kr32` executable** containing the core, assembler, linker,
container reader/writer, C compiler, C++ compiler module, disassembler, debug
model, VM, DAP server, shell, and windowed debugger. CLI commands are modes of
the same binary; `kr32` with no arguments opens the debugger when the UI is
built.

The GUI is a build option: `KR32_BUILD_UI=ON` (the default for product builds)
includes it; `OFF` gives the headless binary used by CI and servers. There is no
separate `kr32-app`, and CLI modes never initialize SDL.

**Layers.** `kr32_core` (CPU, machine, MMU, devices), `kr32_asm`, `kr32_link`,
`kr32_container`, `kr32_cc`, `kr32_cxx`, `kr32_dis`, `kr32_debug`, `kr32_vm`,
`kr32_dap`, `kr32_ui`, `kr32_cli`. `src/platform` is the only place OS and SDL
headers live; `src/ui` is portable layout, painting, hit testing, and text. The
UI, DAP, CLI, and tests all use one `DebugSession`; none of them touch `Cpu`,
`Machine`, or `Container` directly.

**UI.** The UI layer is adapted from KR16's portable `src/ui`, and its SDL
backend is ported from SDL3 to SDL2. It paints into a software RGBA canvas that
the platform backend presents as an SDL texture, which keeps the UI testable
offscreen. Text reuses KR16's embedded font atlas. ImGui is not used.

**Toolchain.** Raw `.rom` images are enough for the first milestones. A 32-bit
`.kr32` container (sections, symbols, relocations, debug) and a linker follow
before the GUI needs variables and call stacks. `kr32 build` runs the full
pipeline: source → assembly → object → link → image → run.

**Debug information.** KR16-style sections: lines, sources, variables, frames,
types, globals, strings. The linker relocates them.

**Host services.** A `SYSCALL` ABI provides console, file I/O, time, random, and
exit, with a sandbox root, quota, and read-only mode.

**Product surface.** A `kr32 shell` for interactive use; one Windows exe and one
Linux AppImage with the runtime and fonts embedded.

## 10. Decision log

Rows marked **Decided** carry the date and rationale; everything else is open.

| ID | Decision | Status | Notes |
| --- | --- | --- | --- |
| D1 | KR32 is a 32-bit machine (registers and addresses) | Open | Confirm the "32" in the name |
| D2 | Mission / differentiator vs KR63 and KR16 | Open | Drives everything below |
| D3 | v1 includes user mode and an MMU | Decided 2026-11-01 | User/kernel split, paging, and a fault model are in v1 |
| D4 | v1 includes multicore | Decided 2026-11-01 | Cooperative scheduler, `LL`/`SC`, and IPIs in the first machine |
| D5 | Emulator implementation language and build | Decided 2026-11-02 | C++20, CMake presets, doctest, warnings-as-errors |
| D6 | Page size and page-table shape | Decided 2026-11-01 | 4 KiB pages; two-level 10 + 10; 20-bit PPN |
| D7 | Opcode budget strategy | Decided 2026-11-01 | 7-bit opcode; 11-bit inline immediate; separate FP/M/byte-bit escapes |
| D8 | Signed arithmetic in base v1 | Decided 2026-11-01 | `SAR`, `DIVS`, `MODS`, `MULH`, `MULHU` included |
| D9 | C++ compiler in scope | Decided 2026-11-01 | Long-term workstream; C is the assumed bootstrap path |
| D10 | Compatibility with KR63 images/tools | Open | Clean slate assumed |
| D11 | FP register file is separate from the GPRs | Decided 2026-11-01 | Separate 16 × 64-bit `F0..F15` |
| D12 | FP register width and count | Decided 2026-11-01 | 16 × 64-bit; 32 rejected (5-bit format, no matrix benefit) |
| D13 | `float`/`double` representation | Decided 2026-11-01 | `float` in low 32, upper zeroed; `double` full 64 |
| D14 | FP encoding | Decided 2026-11-01 | One escape opcode + 11-bit function field; functions in `docs/isa-v1.md` §6 |
| D15 | FP compare and NaN policy | Decided 2026-11-01 | `FEQ`/`FLT`/`FLE` to a GPR; ordered predicates false on NaN; canonical NaN |
| D16 | FP rounding, denormals, traps | Decided 2026-11-01 | RNE; denormals supported; no traps; saturating conversions |
| D17 | Matrix/vector extension M in v1 | Decided 2026-11-01 | Extension M; present in the reference machine |
| D18 | Tile file: 16 × 512-bit `float32` | Decided 2026-11-01 | 1 KB total; 4-bit tile fields |
| D19 | Vectors live in row 0 of a tile | Decided 2026-11-01 | No separate vector file; rows 1..3 cleared on vector writes |
| D20 | Matrix op set | Decided 2026-11-01 | As drafted: `MMUL`/`MMAC`/`MVEC`/`MTRANS`/`VDOT` + loads + stack ops |
| D21 | Tile memory layout and alignment | Decided 2026-11-01 | Row-major, 4-byte aligned; 64 B contiguous or strided rows |
| D22 | Tile precision | Decided 2026-11-01 | `float32` only in v1; bf16/int8 later |
| D23 | Strided `MLOAD`/`MSTORE` in v1 | Decided 2026-11-01 | `E = 1`: extension word carries the row stride |
| D24 | Transform stack: current in a tile, saves in memory | Decided 2026-11-01 | Register-top model; current tile plus a memory save stack |
| D25 | `TSP` is a dedicated user-accessible register | Decided 2026-11-01 | `RDSP`/`WRSP`; grows down; guard pages at both ends |
| D26 | `MPUSH`/`MPOP` take a tile operand | Decided 2026-11-01 | The current tile is a convention, not hard-coded |
| D27 | `MMUL` stays register-only | Decided 2026-11-01 | No `[TSP]` destination; `MMUL.PUSH` is not in v1 |
| D28 | Guard region and RAM base | Decided 2026-11-01 | 64 KiB guard; RAM at `0x0001_0000` |
| D29 | Default RAM size | Decided 2026-11-01 | 128 MiB |
| D30 | MMIO window and ROM | Decided 2026-11-01 | 960 KiB at `0xFFF0_0000`; 64 KiB ROM at `0xFFFF_0000` |
| D31 | PTE policy | Decided 2026-11-01 | `V/W/U/X/A/D/G`; hardware A/D; 20-bit PPN + 12 flags; PS reserved |
| D32 | `#PF` error code | Decided 2026-11-01 | P/W/U/X bits; `FAR` = faulting VA |
| D33 | TLB policy | Decided 2026-11-01 | No TLB in v1; `PTBR`/`MCTL` writes invalidate |
| D34 | Kernel/user address-space model | Decided 2026-11-01 | Identity-mapped kernel; per-process user pages |
| D35 | Vector table location | Decided 2026-11-01 | `VTBR` virtual; fetch through MMU; failed delivery escalates to `#DF` |
| D36 | Vector numbering | Decided 2026-11-01 | KR63 map: exceptions 0–7, IRQ 16–31, software 32–255 |
| D37 | Delivery frame | Decided 2026-11-01 | Error code, saved SP, FLAGS, PC; one shape for faults, traps, IRQs |
| D38 | Kernel stack | Decided 2026-11-01 | Per-core `KSP`; user→kernel switch; kernel→kernel stays |
| D39 | Interrupt model | Decided 2026-11-01 | 16 lines, per-core `IMR` + `IE`, lowest pending first |
| D40 | Boot path | Decided 2026-11-01 | ROM reset vector → BIOS → identity map → paging → kernel |
| D41 | Fault vs trap PC | Decided 2026-11-01 | Faults save the faulting PC; traps and IRQs save the next |
| D42 | Separate byte/halfword register files | Decided 2026-11-01 | No; width-specific loads plus 32-bit GPR ops |
| D43 | Byte/bit primitive set | Decided 2026-11-01 | `LOAD8U/S`, `LOAD16U/S`, `STORE8/16`, `BSWAP`, `BFX`/`BFI`, `CLZ`/`CTZ`/`POPCNT` |
| D44 | Optional character coprocessor | Decided 2026-11-01 | Parked; not in v1; 8 × 8-bit + 8 × 16-bit minimum if revisited |
| D45 | Instruction format | Decided 2026-11-01 | 7/4/4/4 + E + L + 11; `E` bit 12, `L` bit 11 |
| D46 | Extension policy | Decided 2026-11-01 | 4-byte base + optional 4-byte extension; shortest encoding |
| D47 | Branch encoding | Decided 2026-11-01 | `JMP` + `JCOND` with a 4-bit condition field |
| D48 | Load/store widths | Decided 2026-11-01 | Separate width/sign opcodes |
| D49 | Addressing modes | Decided 2026-11-01 | Modes 0–6; base+index via `LOADX`/`STOREX`; mode 7 reserved |
| D50 | Escape allocation | Decided 2026-11-01 | Separate top-level escapes for FP, M, and byte/bit |
| D51 | Alignment policy | Decided 2026-11-01 | Scalar data unaligned; tile/`LL`/`SC` 4-byte; `SP`/`TSP` aligned |
| D52 | `SLT`/`SLTU` in base | Decided 2026-11-01 | Two ALU ops for signed/unsigned less-than |
| D53 | GUI platform and UI layer | Decided 2026-11-02 | SDL2 + custom portable UI adapted from KR16; no ImGui; `KR32_BUILD_UI` |
| D54 | App scope | Decided 2026-11-02 | Windowed debugger primary; machine console included |
| D55 | Executable model | Decided 2026-11-02 | One `kr32` binary; UI optional at build; no separate app |
| D56 | Debug surface | Decided 2026-11-02 | Native GUI debugger primary; DAP secondary |
| D57 | Container/linker timing | Decided 2026-11-02 | Raw `.rom` first; `.kr32` objects/linker before GUI variables |
| D58 | Debug info format | Decided 2026-11-02 | KR16-style sections: lines, vars, frames, types, globals, strings |
| D59 | C++ compiler integration | Decided 2026-11-02 | `kr32_cxx` module linked into `kr32` when ready |
| D60 | Shell | Decided 2026-11-02 | `kr32 shell` included |
| D61 | Host services | Decided 2026-11-02 | `SYSCALL` ABI + sandbox (`--fs-root`, quota, read-only) |
| D62 | Packaging | Decided 2026-11-02 | One Windows exe + Linux AppImage; embedded runtime/fonts |
| D63 | UI reuse | Decided 2026-11-02 | Adapt KR16's portable UI; port its SDL backend to SDL2 |
| D64 | UI canvas | Decided 2026-11-02 | Software RGBA canvas presented as an SDL texture |
| D65 | UI text | Decided 2026-11-02 | Reuse KR16's embedded font atlas |
| D66 | Headless build | Decided 2026-11-02 | `KR32_BUILD_UI=OFF` for CI/servers; product builds default ON |

## 11. Open questions for the interactive session

1. **Mission.** What should KR32 do that KR63 does not? The feature set — scalar
   FP, 4x4 tiles, a transform stack — gives it a graphics/AI flavor on top of a
   full OS platform. Is that the identity, or is the platform the point and the
   graphics the demo?
2. **Is "32" the word size, the address size, or just a project number?** Should
   KR32 be a 32-bit machine, a 16-bit machine, or something else?
3. **Opcode map review.** The revised map is in `docs/isa-v1.md` with signed
   arithmetic, the FP/M policies, and `SLT`/`SLTU` applied. Check the golden
   vectors and freeze v1.
4. **C++ toolchain scope.** The compiler is in scope; which subset? Front-end
   choice, bootstrap through C, ABI, exceptions, RTTI, standard-library subset,
   debug info, and self-hosting are all open (§13).
5. **Multicore shape.** v1 is multicore; how many cores (KR63's 128?), does the
   reference model stay cooperative on one host thread, and what do IPIs and
   atomicity look like?
6. **Implementation stack.** C++20 + CMake + doctest as in KR63, or a different
   language/build for KR32 (Rust, C#, Zig, single-file C++)?
7. **Relationship to KR63 tooling.** Clean slate, or reuse the assembler syntax,
   image format, `.kdbg` schema, and DAP adapter so KR32 inherits a working
   debugger?
8. **KR16.** Which ideas from KR16 are worth pulling in, once its plan is read?
   Is KR16 the 16-bit counterpart that KR32 should complement rather than
   replace?
9. **FP optionality.** Always present in v1, or a profile in which FP
   instructions raise `#UD`? Always present is simpler and matches the intent.
10. **FP determinism.** Host IEEE arithmetic with canonical NaNs, or a
    soft-float library for bit-exact results on every host?

## 12. Provisional milestones

The working sequence, task checklists, and test gates live in
[implementation.md](implementation.md). This table is the summary.

| ID | Milestone | Exit criterion |
| --- | --- | --- |
| M0 | Foundation + scaffold | One `kr32` binary, `KR32_BUILD_UI` on/off, CLI dispatcher, CMake, CI |
| M1 | Core emulator, assembler, disassembler | Golden vectors pass; demo program runs headless |
| M2 | Exceptions, MMU, boot, multicore hooks | Faults, paging, BIOS, user mode, two cores |
| M2b | FP and M extension | FP and matrix demos pass |
| M3 | Container, linker, `build` | `.kr32` objects/images; `kr32 build hello.s` runs |
| M4 | Simple C compiler + minimal runtime | C subset builds and runs |
| M5 | Debug info, `DebugSession`, CLI debugger | Lines, variables, frames; CLI inspect |
| M6 | Windowed debugger | KR16 UI adapted to SDL2; debug a C program |
| M7 | Host services, sandbox, shell, DAP | `SYSCALL` I/O; `kr32 shell`; VS Code DAP |
| M8 | Devices, time, full multicore | Timer/block/console; IPIs; reproducible schedules |
| M9 | C++ toolchain | `kr32_cxx` linked; C++ smoke tests |

C is on the main path at M4; C++ is the later M9 add-on. FP and M are M2b. DAP
shares `DebugSession` and lands with M7.

## 13. C++ toolchain (in scope, unscoped)

The decision is that KR32 gets a C++ compiler. That is a separate workstream,
not a v1 emulator feature, and it should not block the ISA or the machine. It
is built as the `kr32_cxx` module and linked into the one `kr32` binary when it
is ready; a separate compiler executable is not the product.

Open questions, in the order they need answers:

- **Front end.** A third-party front end (KR63 evaluated EDG), a new front end,
  or an existing IR. The back end must emit exactly the encodings in
  `docs/isa-v1.md`.
- **Bootstrap.** The assumed path is C first: a C compiler and a shared back
  end, then C++ lowered onto it. Confirm or replace.
- **ABI.** ILP32 (32-bit pointers and `long`), 64-bit `long long`, register
  roles, aggregate passing, varargs, callee-saves, and atomics. One ABI
  document, frozen before code generation.
- **Runtime.** Exceptions (setjmp-based vs table-driven), RTTI, `new`/`delete`,
  static initialization, guard variables, and the freestanding header subset
  (`<cstddef>`, `<cstdint>`, `<new>`, `<typeinfo>`, then a small
  `<type_traits>` and containers subset).
- **Debug info.** `.kdbg` v2 with files, lines, functions, variables, and
  demangled names so DAP can step through a `.cpp` file.
- **Self-hosting.** Does KR32 eventually compile its own C++ toolchain, or does
  the compiler stay a host program? This decides how much standard library and
  runtime must run on the machine.
- **Out of scope (assumed).** Hosted `<iostream>`, `<filesystem>`, threads, TLS,
  coroutines, modules, and `libc++`.

KR63's §17 is the reference for the shape of this work: one repository, a shared
back end, EDG kept external, bootstrap first, native backend later. KR32 should
reuse the argument, not the code.

## 14. Parking lot

Ideas captured but not yet designed. Nothing here is a commitment.

- **Byte operations system** (2026-11-01). Decided in §5.13: no separate
  byte/word register files in the base machine; width-specific loads and bit
  primitives instead. The optional character coprocessor remains parked. Not
  scheduled.

## 15. How this document evolves

- §10 is the only normative section. Proposals live in prose until they are
  recorded as decisions.
- When a decision is made, move the row from **Open** to **Decided**, add the
  date and a one-line rationale, and update the affected sketch sections.
- The ISA encoding is not frozen until `docs/isa-v1.md` exists and M1's golden
  vectors pass. Until then, §5.3 is a sketch, not a promise.
