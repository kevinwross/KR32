# KR32 Instruction Set v1 — Draft

> **Status:** draft 0.7, 2026-11-15. For review; **not frozen**. Encoding
> decisions D8, D13–D16, D20–D22, D51, and D52 are applied. The encoding
> becomes v1 only after the golden vectors in §12 are checked and the open items
> in §13 are closed.
>
> **Sources:** `KR32-plan.md` §5.3 (format), §5.8–§5.10 (FP, M, transform
> stack), §5.11–§5.12 (paging, exceptions), and decision rows D45–D50.

## 0. Conventions

- 32-bit little-endian machine; instructions are 4 or 8 bytes.
- `0x` hexadecimal; `#` immediate; `[ ]` memory; `Rs`/`Rd` a GPR; `Fs`/`Fd` an
  FPR; `Ts`/`Td` a tile.
- Bits are numbered 31 (most significant) to 0.
- `u32`/`s32` are 32-bit unsigned/signed; `u64`/`s64` are 64-bit; `f32`/`f64`
  are IEEE 754 binary32/binary64.

## 1. Machine state

| State | Width | Count | Notes |
| --- | --- | --- | --- |
| `R0..R15` | 32 | 16 | GPRs; no hard-wired zero |
| `F0..F15` | 64 | 16 | FPRs; scalar FP |
| `T0..T15` | 512 | 16 | 4x4 `f32` tiles, row-major |
| `PC` | 32 | 1 | next instruction; 4-byte aligned |
| `SP` | 32 | 1 | stack pointer, grows down |
| `TSP` | 32 | 1 | transform stack pointer, grows down |
| `FLAGS` | 32 | 1 | status bits below |
| `CR0..CR15` | 32 | 16 | control registers, §9 |

`SP` and `TSP` are user-accessible. `PC` is not addressable by an instruction.
`TSP` is read and written with `RDSP`/`WRSP`.

### 1.1 Flags

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | Z | result was zero |
| 1 | N | result bit 31 set |
| 2 | C | carry out / no borrow |
| 3 | V | signed overflow |
| 4 | I | interrupts enabled |
| 5 | K | kernel mode |
| 6..31 | | reserved; read as zero, ignored on write |

`MODE` and `IE` are views of `FLAGS.K` and `FLAGS.I`; there is one canonical copy.

## 2. Instruction format

### 2.1 Base word

| Bits | Field |
| --- | --- |
| 31..25 | opcode (7) |
| 24..21 | `Rd` (4) |
| 20..17 | `Rs1` (4) |
| 16..13 | `Rs2` / mode / condition (4) |
| 12 | `E`: a 32-bit extension word follows |
| 11 | `L`: the payload is a literal |
| 10..0 | inline value, signed 11-bit |

### 2.2 Rules

1. `E = 1` means the next four bytes are a little-endian 32-bit payload and the
   instruction is 8 bytes long. The inline field is ignored, except that
   `MLOAD`/`MSTORE` require it to be zero when `E = 1` (the extension word is a
   stride, §7.1).
2. `E = 0` means the payload is the inline field sign-extended from 11 bits, and
   the instruction is 4 bytes long.
3. `L = 1` means the payload is a literal, immediate, or displacement. `L = 0`
   means the 16..13 field names a register. `FLOAD`/`FSTORE` and
   `MLOAD`/`MSTORE` reinterpret `L` as a width selector (§6.4, §7.1).
4. At most one extension word; chained extensions are not part of v1.
5. The assembler emits the shortest encoding that represents the value and
   relaxes labels when a displacement changes size. A `.long` or force-long form
   is available when predictable sizes matter.
6. Reserved opcodes and reserved modes raise `#UD`; they are never treated as
   no-ops.

### 2.3 Field assignment by instruction class

| Class | `Rd` | `Rs1` | 16..13 |
| --- | --- | --- | --- |
| ALU / shift | destination | first source | second source or literal |
| `MOV` | destination | source | reserved |
| `LOAD` / `STORE` | destination / data source | base | addressing mode |
| Indexed `*X` family | destination / data source | base | index register |
| `LEA` | destination | base | addressing mode |
| `JMP` / `CALL` | reserved | register target (L=0) | reserved |
| `JCOND` | reserved | register target (L=0) | condition |
| `PUSH` / `POP` | source / destination | reserved | reserved |
| `FLOAD` / `FSTORE` | FP destination / source | base | addressing mode |
| `MLOAD` / `MSTORE` | tile destination / source | base | addressing mode |

## 3. Addressing modes

Modes are used by the memory family (`LOAD`, `STORE`, the sized variants,
`LEA`, `FLOAD`/`FSTORE`, `MLOAD`/`MSTORE`):

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

The PC-relative base is the address of the instruction itself, not the following
instruction. `LEA` computes an address without accessing memory.

**Base+index** is a separate opcode family, not a mode. `LOADX`, `STOREX`,
`LOAD8UX`, `LOAD8SX`, `LOAD16UX`, `LOAD16SX`, `STORE8X`, `STORE16X` take
`[Rs1 + Ri + disp]`, where `Ri` is the register named by 16..13 and the
displacement is inline or in the extension word.

## 4. Opcode map

### 4.1 Base integer (0x00–0x4F)

| Op | Mnemonic | Group |
| --- | --- | --- |
| 0x00 | `MOV` | data movement |
| 0x01 | `LOAD` | data movement |
| 0x02 | `STORE` | data movement |
| 0x03 | `LOAD8U` | data movement |
| 0x04 | `LOAD8S` | data movement |
| 0x05 | `LOAD16U` | data movement |
| 0x06 | `LOAD16S` | data movement |
| 0x07 | `STORE8` | data movement |
| 0x08 | `STORE16` | data movement |
| 0x09 | `LEA` | data movement |
| 0x0A | `POPFP` | FP context (`F15..F0`) |
| 0x0B–0x0F | reserved | |
| 0x10 | `LOADX` | indexed |
| 0x11 | `STOREX` | indexed |
| 0x12 | `LOAD8UX` | indexed |
| 0x13 | `LOAD8SX` | indexed |
| 0x14 | `LOAD16UX` | indexed |
| 0x15 | `LOAD16SX` | indexed |
| 0x16 | `STORE8X` | indexed |
| 0x17 | `STORE16X` | indexed |
| 0x18 | `SLT` | ALU extension |
| 0x19 | `SLTU` | ALU extension |
| 0x1A–0x1F | reserved | |
| 0x20 | `ADD` | ALU |
| 0x21 | `SUB` | ALU |
| 0x22 | `MUL` | ALU |
| 0x23 | `MULH` | ALU |
| 0x24 | `MULHU` | ALU |
| 0x25 | `DIV` | ALU |
| 0x26 | `DIVS` | ALU |
| 0x27 | `MOD` | ALU |
| 0x28 | `MODS` | ALU |
| 0x29 | `CMP` | ALU |
| 0x2A | `AND` | ALU |
| 0x2B | `OR` | ALU |
| 0x2C | `XOR` | ALU |
| 0x2D | `NOT` | ALU |
| 0x2E | `NEG` | ALU |
| 0x2F | reserved | |
| 0x30 | `SHL` | shift |
| 0x31 | `SHR` | shift |
| 0x32 | `SAR` | shift |
| 0x33 | `ROL` | shift |
| 0x34 | `ROR` | shift |
| 0x35–0x37 | reserved | |
| 0x38 | `JMP` | branch |
| 0x39 | `JCOND` | branch |
| 0x3A | `CALL` | branch |
| 0x3B | `RET` | branch |
| 0x3C | `PUSH` | stack |
| 0x3D | `POP` | stack |
| 0x3E | `ADJSP` | stack |
| 0x3F | `SETSP` | stack, unwinder support (`SP = Rs1`) |
| 0x40 | `INT` | system |
| 0x41 | `SYSCALL` | system |
| 0x42 | `IRET` | system |
| 0x43 | `HALT` | system |
| 0x44 | `BRK` | system |
| 0x45 | `NOP` | system |
| 0x46 | `LL` | atomic |
| 0x47 | `SC` | atomic |
| 0x48 | `FENCE` | atomic |
| 0x49 | `RDCR` | system |
| 0x4A | `WRCR` | system |
| 0x4B | `RDSP` | transform stack |
| 0x4C | `WRSP` | transform stack |
| 0x4D | `PUSHR` | bulk stack (`R1..R15`) |
| 0x4E | `POPR` | bulk stack (`R15..R1`) |
| 0x4F | `PUSHFP` | bulk FP stack (`F0..F15`) |

### 4.2 Memory extensions (0x50–0x5F)

| Op | Mnemonic | Notes |
| --- | --- | --- |
| 0x50 | `FLOAD` | `L` selects `.W` (0) / `.D` (1) |
| 0x51 | `FSTORE` | `L` selects `.W` (0) / `.D` (1) |
| 0x52 | `MLOAD` | `L` selects matrix 64 B (0) / vector 16 B (1) |
| 0x53 | `MSTORE` | `L` selects matrix 64 B (0) / vector 16 B (1) |
| 0x54–0x5F | reserved | |

### 4.3 Escapes (0x60–0x62)

| Op | Class | Format |
| --- | --- | --- |
| 0x60 | FP | `fd`, `fs1`, `fs2`, 11-bit function |
| 0x61 | M | `Td`, `Ta`, `Tb`, 11-bit function |
| 0x62 | byte/bit | `Rd`, `Rs1`, 4-bit field, 11-bit function/immediate |

### 4.4 Reserved (0x63–0x7F)

The map uses 72 of the 128 top-level slots; 56 are reserved, 29 of them in
`0x63–0x7F`. A reserved opcode raises `#UD`.

## 5. Base instructions

### 5.1 Data movement

| Op | Form | Effect | Flags |
| --- | --- | --- | --- |
| `MOV` | `Rd, Rs` / `Rd, #imm` | `Rd = source` | — |
| `LOAD` | `Rd, [addr]` | `Rd = u32` | — |
| `STORE` | `Rs, [addr]` | `u32 at addr = Rs` | — |
| `LOAD8U` | `Rd, [addr]` | zero-extended `u8` | — |
| `LOAD8S` | `Rd, [addr]` | sign-extended `s8` | — |
| `LOAD16U` | `Rd, [addr]` | zero-extended `u16` | — |
| `LOAD16S` | `Rd, [addr]` | sign-extended `s16` | — |
| `STORE8` | `Rs, [addr]` | low 8 bits | — |
| `STORE16` | `Rs, [addr]` | low 16 bits | — |
| `LEA` | `Rd, [addr]` | `Rd = effective address` | — |

For `STORE`/`STORE8`/`STORE16` and the indexed stores, the data source is in the
`Rd` field. For `LOAD` and friends, `Rd` is the destination.

### 5.2 Indexed family

`LOADX`/`STOREX` and the sized variants compute `Rs1 + Ri + disp`, where `Ri` is
the register named by 16..13. They otherwise behave like their non-indexed
counterparts. `LEA` plus a load is the fallback when an address expression does
not fit.

### 5.3 Arithmetic and logic

| Op | Form | Effect | Flags |
| --- | --- | --- | --- |
| `ADD` | `Rd, Rs1, x` | `Rd = Rs1 + x` | Z N C V |
| `SUB` | `Rd, Rs1, x` | `Rd = Rs1 - x`; `C` = no borrow | Z N C V |
| `CMP` | `Rs1, x` | flags of `Rs1 - x`, no write | Z N C V |
| `MUL` | `Rd, Rs1, x` | low 32 of the product | Z N; `V` = high != 0; `C` = 0 |
| `MULH` | `Rd, Rs1, x` | signed high 32 of the product | Z N; `C` = `V` = 0 |
| `MULHU` | `Rd, Rs1, x` | unsigned high 32 of the product | Z N; `C` = `V` = 0 |
| `DIV` | `Rd, Rs1, x` | unsigned quotient; `#DIV0` if `x = 0` | Z N; `C` = `V` = 0 |
| `DIVS` | `Rd, Rs1, x` | signed quotient; `#DIV0` if `x = 0` | Z N; `C` = `V` = 0 |
| `MOD` | `Rd, Rs1, x` | unsigned remainder; `#DIV0` if `x = 0` | Z N; `C` = `V` = 0 |
| `MODS` | `Rd, Rs1, x` | signed remainder; `#DIV0` if `x = 0` | Z N; `C` = `V` = 0 |
| `AND`/`OR`/`XOR` | `Rd, Rs1, x` | bitwise | Z N; `C` = `V` = 0 |
| `NOT` | `Rd, Rs1` | `Rd = ~Rs1` | Z N; `C` = `V` = 0 |
| `NEG` | `Rd, Rs1` | `Rd = 0 - Rs1` | Z N C V |
| `SLT` | `Rd, Rs1, x` | `Rd = (signed Rs1 < x) ? 1 : 0` | Z N; `C` = `V` = 0 |
| `SLTU` | `Rd, Rs1, x` | `Rd = (unsigned Rs1 < x) ? 1 : 0` | Z N; `C` = `V` = 0 |

`DIVS` overflow (`0x8000_0000 / -1`) produces `0x8000_0000` and does not trap.
`CMP` uses the same flag rules as `SUB` and discards the result. `SLT`/`SLTU`
write 0 or 1 and set `Z` from the result, with `N = C = V = 0`.

### 5.4 Shifts and rotates

The count is the low five bits of the second operand, 0..31.

| Op | Effect | Flags |
| --- | --- | --- |
| `SHL` | logical left | Z N; `C` = last bit out; `V` = 0 |
| `SHR` | logical right | Z N; `C` = last bit out; `V` = 0 |
| `SAR` | arithmetic right | Z N; `C` = last bit out; `V` = 0 |
| `ROL` | rotate left | Z N; `C` = last bit out; `V` = 0 |
| `ROR` | rotate right | Z N; `C` = last bit out; `V` = 0 |

A count of zero leaves the destination unchanged and leaves `C` unchanged.

### 5.5 Branches and stack

| Op | Form | Effect |
| --- | --- | --- |
| `JMP` | `target` | `PC = target` |
| `JCOND` | `cond, target` | `PC = target` when `cond` holds |
| `CALL` | `target` | push `PC + len`; `PC = target` |
| `RET` | | pop `PC` |
| `PUSH` | `Rs` | `SP -= 4`; store `Rs` |
| `POP` | `Rd` | load `Rd`; `SP += 4` |
| `PUSHR` | | `SP -= 60`; push `R1..R15` in ascending order |
| `POPR` | | pop `R15..R1`; `SP += 60` |
| `PUSHFP` | | `SP -= 128`; push `F0..F15` as low word then high word |
| `POPFP` | | pop `F15..F0` as high word then low word; `SP += 128` |
| `ADJSP` | `#imm` | `SP += imm` |
| `SETSP` | `Rs1` | `SP = Rs1` (used by the exception unwinder) |

The target is `Rs1` when `L = 0` and the inline/extension payload when `L = 1`.
`JCOND` puts the condition in 16..13; `JMP` and `CALL` reserve that field.
Branch targets must be 4-byte aligned; otherwise `#ALIGN`.

`SP` must be 4-byte aligned for `PUSH`, `POP`, `PUSHR`, `POPR`, `PUSHFP`,
`POPFP`, `CALL`, `RET`, and `ADJSP`; otherwise `#ALIGN`.

`PUSHR`/`POPR` deliberately exclude `R0`: `R0` is the integer ABI return
register, so a syscall wrapper can preserve `R1..R15` without destroying the
dispatcher's result. Interrupt handlers that also need `R0` do:

```asm
PUSH R0
PUSHR
...
POPR
POP R0
```

`PUSHFP`/`POPFP` save the complete 64-bit bit pattern of all sixteen FPRs; they
do not save `TSP` or tile registers. `TSP` is saved explicitly with `RDSP` and
`WRSP`:

```asm
RDSP R0
PUSH R0
...
POP R0
WRSP R0
```

Bulk stack operations are not restartable if a stack access faults. `PUSHR` and
`PUSHFP` restore `SP` to its original value when a store faults; words already
written remain in memory. `POPR`/`POPFP` may leave earlier registers restored
before the fault. Bulk operations leave the integer flags unchanged.

#### Condition codes

| Code | Name | Test |
| --- | --- | --- |
| 0 | `EQ` | `Z = 1` |
| 1 | `NE` | `Z = 0` |
| 2 | `CS` | `C = 1` |
| 3 | `CC` | `C = 0` |
| 4 | `MI` | `N = 1` |
| 5 | `PL` | `N = 0` |
| 6 | `VS` | `V = 1` |
| 7 | `VC` | `V = 0` |
| 8 | `HI` | `C = 1 and Z = 0` (unsigned >) |
| 9 | `LS` | `C = 0 or Z = 1` (unsigned <=) |
| 10 | `GE` | `N = V` (signed >=) |
| 11 | `LT` | `N != V` (signed <) |
| 12 | `GT` | `Z = 0 and N = V` (signed >) |
| 13 | `LE` | `Z = 1 or N != V` (signed <=) |
| 14 | `AL` | always |
| 15 | `NV` | never |

The assembler provides `JZ`, `JNZ`, `JCS`, `JNC`, `JMI`, `JPL`, `JVS`, `JVC`,
`JHI`, `JLS`, `JGE`, `JLT`, `JGT`, and `JLE` as aliases for `JCOND`.

### 5.6 System, atomic, and transform-stack access

| Op | Form | Effect |
| --- | --- | --- |
| `INT` | `#vec` | software interrupt; `vec` 32..255, else `#GP`; saves the next PC |
| `SYSCALL` | `#vec` | software call; `vec` 32..255, else `#GP`; ABI-defined |
| `IRET` | | kernel only; pop the interrupt frame |
| `HALT` | | kernel only; stop the core |
| `BRK` | | raise `#BRK`; saves the next PC |
| `NOP` | | no effect |
| `LL` | `Rd, [addr]` | load-linked; set this core's reservation |
| `SC` | `Rs, [addr]` | store if the reservation holds; `Z = 1` on success |
| `FENCE` | | full barrier; a no-op under sequential consistency |
| `RDCR` | `Rd, CRn` | kernel only; `Rd = CRn` |
| `WRCR` | `CRn, Rs` | kernel only; `CRn = Rs` |
| `RDSP` | `Rd` | `Rd = TSP` |
| `WRSP` | `Rs` | `TSP = Rs` |

`LL`/`SC` require a 4-byte-aligned address in RAM; unaligned is `#ALIGN`, ROM
or MMIO is `#GP`. The reservation is per core and word-granular; it is cleared
by any store to the reserved word, by `SC`, and by exception or interrupt
delivery. `SC` sets `Z` and leaves `N`, `C`, and `V` unchanged.

## 6. FP escape (opcode 0x60)

Format: `fd` in 24..21, `fs1` in 20..17, `fs2` in 16..13, function in 10..0.
`E` and `L` are reserved zero for the operations below.

### 6.1 Arithmetic and compare

| Function | Mnemonic | Operands | Notes |
| --- | --- | --- | --- |
| 0x000 | `FADD.S` | `fd, fs1, fs2` | |
| 0x001 | `FSUB.S` | `fd, fs1, fs2` | |
| 0x002 | `FMUL.S` | `fd, fs1, fs2` | |
| 0x003 | `FDIV.S` | `fd, fs1, fs2` | |
| 0x004 | `FSQRT.S` | `fd, fs1` | `fs2` reserved |
| 0x005 | `FMIN.S` | `fd, fs1, fs2` | NaN: return the non-NaN operand |
| 0x006 | `FMAX.S` | `fd, fs1, fs2` | NaN: return the non-NaN operand |
| 0x007 | `FABS.S` | `fd, fs1` | clears the sign bit |
| 0x008 | `FNEG.S` | `fd, fs1` | flips the sign bit |
| 0x009 | `FEQ.S` | `Rd, fs1, fs2` | GPR destination; 0/1 |
| 0x00A | `FLT.S` | `Rd, fs1, fs2` | GPR destination; 0/1 |
| 0x00B | `FLE.S` | `Rd, fs1, fs2` | GPR destination; 0/1 |

The same functions with bit 8 set are the `.D` forms: `0x100`–`0x10B`.

### 6.2 Conversions and moves

| Function | Mnemonic | Operands |
| --- | --- | --- |
| 0x020 | `FCVT.S.D` | `fd, fs` |
| 0x021 | `FCVT.D.S` | `fd, fs` |
| 0x022 | `FCVT.W.S` | `Rd, fs` |
| 0x023 | `FCVT.WU.S` | `Rd, fs` |
| 0x024 | `FCVT.S.W` | `fd, Rs` |
| 0x025 | `FCVT.S.WU` | `fd, Rs` |
| 0x026 | `FCVT.W.D` | `Rd, fs` |
| 0x027 | `FCVT.WU.D` | `Rd, fs` |
| 0x028 | `FCVT.D.W` | `fd, Rs` |
| 0x029 | `FCVT.D.WU` | `fd, Rs` |
| 0x030 | `FMV.W.X` | `fd, Rs` |
| 0x031 | `FMV.X.W` | `Rd, fs` |
| 0x032 | `FMOV` | `fd, fs` |

`FMV.W.X` writes the low 32 bits and clears bits 63..32; `FMV.X.W` reads the low
32 bits. `FMOV` copies all 64 bits of the source register, so it serves both
precisions; it leaves the integer flags unchanged.

### 6.3 Semantics

- Round to nearest, ties to even; no FP traps; no observable exception flags.
- All NaN results are canonical quiet NaNs (`0x7FC0_0000` for `.S`,
  `0x7FF8_0000_0000_0000` for `.D`).
- Denormals are supported.
- `FEQ`/`FLT`/`FLE` write 0 or 1 to a GPR and set `Z = (Rd == 0)`, `N = C = V = 0`;
  NaN makes every ordered predicate false.
- Arithmetic and move instructions leave the integer flags unchanged.
- `float` occupies bits 31..0 with bits 63..32 zero; `double` occupies all 64
  bits.
- Out-of-range float-to-integer conversions saturate: NaN and `+inf` produce the
  maximum, `-inf` the minimum, and values below zero produce zero for unsigned.
- **Subnormal results are preserved.** An implementation must not enable
  flush-to-zero or denormals-are-zero. Operations that produce a subnormal
  return it, and subnormal operands are not treated as zero.
- **Each operation rounds once**, from its exact value, at the precision of the
  operation: `.S` to binary32, `.D` to binary64. Intermediate results must not be
  carried in a wider format than the operation. This is what makes the host IEEE
  arithmetic the specification rather than a suggestion: every operation in §6
  is correctly rounded, and the set contains no fused multiply-add and no
  transcendental function.

### 6.4 FP loads and stores

`FLOAD`/`FSTORE` use the normal memory format: the FP register is in `Rd`
(destination for `FLOAD`, source for `FSTORE`), `Rs1` is the base, 16..13 is the
addressing mode, `L = 0` selects `.W` (32-bit) and `L = 1` selects `.D` (64-bit),
and the displacement is inline or in the extension word. `E = 1` supplies a
32-bit displacement and the inline field is ignored. Scalar FP accesses may be
unaligned; page and general-protection faults follow the normal data rules.

## 7. M escape (opcode 0x61)

Format: `Td` in 24..21, `Ta` in 20..17, `Tb` in 16..13, function in 10..0.
`E` and `L` are reserved zero.

| Function | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x000 | `MMUL` | `Td, Ta, Tb` | `Td = Ta * Tb` |
| 0x001 | `MMAC` | `Td, Ta, Tb` | `Td += Ta * Tb` |
| 0x002 | `MVEC` | `Td, Ta, Tb` | `Td[0] = Ta * Tb[0]`; rows 1..3 cleared |
| 0x003 | `MTRANS` | `Td, Ta` | transpose |
| 0x004 | `VDOT` | `Fd, Ta, Tb` | dot product of row 0, scalar FPR result |
| 0x005 | `MPUSH` | `Td` | `TSP -= 64; mem[TSP] = Td` |
| 0x006 | `MPOP` | `Td` | `Td = mem[TSP]; TSP += 64` |
| 0x007 | `MDROP` | | `TSP += 64` |

`MMUL`/`MMAC`/`MVEC` accumulate with fused multiply-add and round to nearest,
ties to even. Matrix and tile operations leave the integer flags unchanged.
`Td` may alias `Ta`/`Tb`; sources are read before the destination is written.

### 7.1 Tile loads and stores

`MLOAD`/`MSTORE` use the normal memory format: tile in `Rd`, base in `Rs1`, mode
in 16..13, `L` selects 64 B (`L = 0`) or 16 B (`L = 1`). `E = 0` is a contiguous
transfer at `base + disp`. `E = 1` applies to the 64-byte form only: the
extension word is the row stride, the inline displacement must be zero, and the
four rows are at `base + r * stride`; `E = 1` with `L = 1` is reserved (`#UD`).
`VLOAD`/`VSTORE` are the 16-byte aliases; `VLOAD` writes row 0 and clears rows
1..3, and `VSTORE` stores row 0. Tile transfers require 4-byte alignment; the
stride is a multiple of four.

### 7.2 Transform stack

`TSP` grows down in 64-byte entries and must be 64-byte aligned. `MPUSH`/`MPOP`
move one tile, `MDROP` discards the top save. A push or pop outside the mapped
stack region raises `#PF`; a misaligned `TSP` raises `#ALIGN`; `TSP` is unchanged
when the access faults.

## 8. Byte/bit escape (opcode 0x62)

Format: `Rd` in 24..21, `Rs1` in 20..17, field in 16..13, function/immediate in
10..0. `E` and `L` are reserved zero.

| Function | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x000 | `BSWAP` | `Rd, Rs1` | byte-reverse a 32-bit word |
| 0x001 | `BFX` | `Rd, Rs1, #lsb, #width` | extract `width` bits; `width-1` in 16..13, `lsb` in imm |
| 0x002 | `BFI` | `Rd, Rs1, #lsb, #width` | insert low `width` bits of `Rs1` into `Rd` |
| 0x003 | `CLZ` | `Rd, Rs1` | count leading zeros |
| 0x004 | `CTZ` | `Rd, Rs1` | count trailing zeros |
| 0x005 | `POPCNT` | `Rd, Rs1` | population count |
| 0x006–0x00F | reserved | | |

These instructions set `Z` and `N` from the result and clear `C` and `V`.

## 9. Control registers

| CR | Name | Access | Purpose |
| --- | --- | --- | --- |
| 0 | `IE` | read/write | interrupt enable; reads `FLAGS.I` |
| 1 | `IMR` | read/write | hardware interrupt mask, bits 0..15 |
| 2 | `IRQ` | read/write | pending/active interrupt bitmap |
| 3 | `VTBR` | read/write | virtual base of the 256-entry vector table |
| 4 | `PRIV` | read-only | reads `FLAGS.K` |
| 5 | `ESR` | read/write | vector number of the most recent delivery |
| 6 | `KSP` | read/write | kernel stack pointer |
| 7 | `MISC` | read-only | reserved; reads zero |
| 8 | `PTBR` | read/write | physical base of the page directory |
| 9 | `FAR` | read/write | fault address |
| 10 | `MCTL` | read/write | memory control: bit 0 `PGE`, bit 1 `WP` |
| 11 | `COREID` | read-only | core index, 0..N-1 |
| 12–15 | reserved | | access raises `#GP` |

`RDCR`/`WRCR` are kernel-only. Writing `COREID`, `PRIV`, or `MISC` raises `#GP`.
`PTBR` must be 4 KiB aligned; `MCTL` reserved bits must be zero; `VTBR` must be
4-byte aligned. `PTBR` and `MCTL` writes invalidate translation state. `TSP` is
not a CR; it is accessed with `RDSP`/`WRSP` and is user-accessible.

## 10. Exceptions (summary)

The full contract is in `KR32-plan.md` §5.12. The vector map is:

| Vector | Source |
| --- | --- |
| 0 | reserved |
| 1 | `#UD` |
| 2 | `#DIV0` |
| 3 | `#BRK` |
| 4 | `#PF` |
| 5 | `#GP` |
| 6 | `#ALIGN` |
| 7 | `#DF` |
| 8..15 | reserved |
| 16..31 | hardware interrupt lines 0..15 |
| 32..255 | software `INT`/`SYSCALL` |

Delivery pushes `error code`, saved `SP`, `FLAGS`, and `PC`; user→kernel
switches to `KSP`; `IRET` reverses the frame. `#PF` carries
`bit0 P | bit1 W | bit2 U | bit3 X` and fills `FAR`.

## 11. Memory map (summary)

| Range | Region |
| --- | --- |
| `0x0000_0000..0x0000_FFFF` | 64 KiB guard region, never backed |
| `0x0001_0000..0x0800_FFFF` | RAM, 128 MiB default |
| `0x0801_0000..0xFBFF_FFFF` | unbacked gap |
| `0xFC00_0000..0xFDFF_FFFF` | 32 MiB framebuffer aperture |
| `0xFE00_0000..0xFFEF_FFFF` | unbacked gap |
| `0xFFF0_0000..0xFFFE_FFFF` | 960 KiB MMIO window |
| `0xFFFF_0000..0xFFFF_FFFF` | 64 KiB ROM |

Pages are 4 KiB, two-level 10 + 10, 20-bit PPN, 12 flag bits
(`V/W/U/X/A/D/G`, `PS` reserved). The kernel is identity-mapped; each process
has its own `PTBR`. There is no TLB in v1.

## 12. Golden encodings

| Instruction | Word(s) |
| --- | --- |
| `MOV R1, #42` | `0x0020_082A` |
| `ADD R3, R1, R2` | `0x4062_4000` |
| `SLT R1, R2, R3` | `0x3024_6000` |
| `SLTU R1, R2, R3` | `0x3224_6000` |
| `LOAD R4, [R5 + 8]` | `0x028A_6808` |
| `LOAD8U R4, [R5 + 8]` | `0x068A_6808` |
| `STORE R6, [R7]` | `0x04CE_4800` |
| `LL R1, [R2]` | `0x8C24_4800` |
| `SC R1, [R2]` | `0x8E24_4800` |
| `JMP 0x0040_0000` | `0x7000_1800`, `0x0040_0000` |
| `CALL 0x0040_0000` | `0x7400_1800`, `0x0040_0000` |
| `JZ R7` | `0x720E_0000` |
| `RET` | `0x7600_0000` |
| `HALT` | `0x8600_0000` |
| `ADJSP #-16` | `0x7C00_0FF0` |
| `FADD.S F1, F2, F3` | `0xC024_6000` |
| `FMOV F3, F5` | `0xC06A_0032` |
| `MMUL T1, T2, T3` | `0xC224_6000` |
| `BSWAP R1, R2` | `0xC424_0000` |
| `PUSHR` | `0x9A00_0000` |
| `POPR` | `0x9C00_0000` |
| `PUSHFP` | `0x9E00_0000` |
| `POPFP` | `0x1400_0000` |

The extension word is little-endian and is always the word after the base word.

These rows are a sample, not the coverage. `tests/unit/test_golden_vectors.cpp`
recomputes the encoding of every opcode in §4 (72 of them) and of both widths
from §2.1 with an encoder that does not call the implementation, and checks that
the implementation agrees; it also rebuilds every row above from its fields.

## 13. Before freezing

- **Pseudo-ops.** `JZ`/`JNZ`/…, `VLOAD`/`VSTORE`, and `NEG`-style aliases belong
  to the assembler, not the ISA.
- **Data directives.** `.byte`, `.word`, `.float`, and `.double` emit scalar
  data; `.float` writes one little-endian binary32 word per value and `.double`
  writes two little-endian binary64 words per value. The object assembler keeps
  the same encoding.

- **Assembler syntax.** Checked against the programmer's reference (§3.1-§3.7):
  register names, `#` immediates, the operand forms, and the `JCOND` aliases all
  behave as documented, and the documented limits on expressions (a sum of terms,
  no `*`, `/`, or parentheses) are real. The one gap was `.long`, which is named
  above but did not exist; it is now a 32-bit alias of `.word` in both assembler
  paths. The only other correction was a reference wording fix, since a COMDAT
  section is written `.section .text.<name>` rather than as a bare directive.

Two former items are decided and no longer open. **Q9:** v1 always has floating
point, and a no-FP profile is out of scope for v1 — if one is ever wanted it is a
separate subset document against a frozen v1. **Q10:** the policy is host IEEE
arithmetic with canonical NaNs, no traps, and the two conditions in §6.3
(subnormals preserved, one rounding per operation); a soft-float library is not
required, because every operation in §6 is correctly rounded. The evidence is in
`docs/isa-v1-freeze.md`.

## 14. Revision history

| Revision | Date | Change |
| --- | --- | --- |
| 0.2 | 2026-11-01 | Baseline recorded in this document's status note: encoding decisions D8, D13-D16, D20-D22, D51, and D52 applied; §12 golden vectors and §13 open items still to be resolved before v1 is frozen. |
| 0.3 | 2026-11-15 | Added `FMOV Fd, Fs` (FP escape function `0x032`), an FPR-to-FPR copy of all 64 bits. It exists so a cached floating-point value can reach an operand register without a memory round trip; before it, the compiler staged every FP value through a frame slot. Golden encoding for `FMOV F3, F5` is `0xC06A0032` (§6.2, `tests/unit/test_encoding.cpp`). |
| 0.4 | 2026-11-15 | Golden vectors recomputed from §2.1 by an encoder that does not call the implementation: `tests/unit/test_golden_vectors.cpp` covers every opcode in §4 and both widths, and rebuilds each §12 row from its fields. It found one defect in this document: §4.4 claimed 71 of 128 top-level slots in use and 57 reserved; the map uses 72 and reserves 56. The §13 golden-vector item is closed by that coverage. |
| 0.5 | 2026-11-15 | Opcode-map review closed: the disassembler recognises exactly the 72 documented slots and no others, every mnemonic matches, and a reserved slot executed for the purpose raises `#UD` without retiring, so reserved ranges are traps. Recorded with the Q9/Q10 decision brief in `docs/isa-v1-freeze.md`. |
| 0.6 | 2026-11-15 | Q9 and Q10 decided and recorded as policy. v1 always has floating point; a no-FP profile is out of scope for v1. Host IEEE arithmetic is the specification, with subnormal results preserved and one rounding per operation at the operation's precision (§6.3), so no soft-float library is required. The evidence for both decisions is in `docs/isa-v1-freeze.md`. |
| 0.7 | 2026-11-15 | Assembler-syntax pass closed: `.long` added as the 32-bit alias of `.word` in both assembler paths, and the reference's directive table gained it; the COMDAT section row corrected to show that the dotted name is an argument to `.section`. Every other documented syntax rule was checked against the parser and held, including the documented absence of parentheses, `*`, `/`, and character or binary literals in expressions. |