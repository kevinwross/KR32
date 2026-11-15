# ISA v1 freeze: map review and the two open decisions

**Status:** input for the operator's decision on freezing ISA v1.
**Date:** 2026-11-15.

This note closes the opcode-map review that `docs/isa-v1.md` §13 lists as an open
item, and sets out the two remaining questions — Q9 (an optional no-FP profile)
and Q10 (bit-exact floating point) — with the evidence needed to decide them. It
does not itself freeze anything.

## 1. Opcode-map review

**Method.** The disassembler is the reference decoder, so it was used as the
oracle rather than the assembler's mnemonic tables. One word was built for each
of the 128 top-level slots with identical, benign fields (`Rd = 1`,
`Rs1 = 2`, field `= 3`), disassembled, and compared with the map in §4.

**Results.**

| Check | Result |
|---|---|
| Slots recognised as an instruction | 72 |
| Slots documented as in use (§4.1–§4.3) | 72 |
| Recognised but undocumented | none |
| Documented but not recognised | none |
| Reserved slots reaching `#UD` | all 56 |

The mnemonic for every slot matches the document. One slot looks different and
is correct: opcode `0x39` is `JCOND`, and the disassembler renders it through its
condition alias — with the probe's field value it prints `JNC`. That is the
documented pseudo-op behaviour, not a mismatch.

A reserved opcode was also executed rather than only decoded: `0x0B` leaves `PC`
unchanged, retires no instruction, and reports the invalid-opcode trap, so
reserved slots are traps and not accidental no-ops (§2.2 rule 6).

**Conclusion.** The map is consistent with the implementation in both directions.
The review found no defect; the arithmetic in §4.4 was already corrected in draft
0.4 (72 slots in use, 56 reserved).

## 2. Q9: an optional no-FP profile?

The document currently says v1 always has FP, with a no-FP profile left open.

**What a profile would remove.** `F0`–`F15` (16 × 64-bit), the FP escape
(`0x60`), the scalar FP and conversion functions, `FLOAD`/`FSTORE`, and the bulk
FP stack operations `PUSHFP`/`POPFP`. It would not touch the rest of the map.

**Evidence from this codebase.**

- None of the OS programs contains a single FP instruction: the kernel, the
  shell, the console, the gfx library, and the user programs are all integer
  code. A no-FP machine would run KR32OS unchanged.
- The compilers emit FP only for `float` and `double` in user programs, and the
  C subset only gained those recently.
- For an emulator, a no-FP profile buys nothing and costs a second validation
  surface. Its value is to a hardware implementation that wants to omit the
  register file and decode.

**Options.**

1. **v1 is FP-always, and a no-FP profile is out of scope for v1.** A future
   profile, if wanted, is a separate subset document against a frozen v1.
2. **Define a no-FP conformance profile now**, as part of v1: a named subset with
   its own reserved-bit rules and test list.

**Recommendation: option 1.** The profile only pays for a hardware target that
does not exist yet, and defining it now would double the conformance surface for
every future change — every FP addition would have to answer "what does the
profile do". Recording it as out of scope is a decision, and it is reversible:
a profile can be added against a frozen v1 without changing v1.

## 3. Q10: must floating point be bit-exact on every host?

**Current policy.** Host IEEE arithmetic, round to nearest even, no traps, no
observable exception flags, canonical quiet NaNs, denormals supported (§6.3).

**What the ISA actually contains.** `FADD`, `FSUB`, `FMUL`, `FDIV`, `FSQRT`,
`FMIN`, `FMAX`, `FABS`, `FNEG`, `FEQ`, `FLT`, `FLE`, the `FCVT` family,
`FMV.W.X`/`FMV.X.W`, `FMOV`, `FLOAD`/`FSTORE`.

Every one of those is **correctly rounded** under IEEE 754-2019, or is exact:
`+ − × ÷ √` are the correctly-rounded operations; `FABS`/`FNEG`/`FMOV` are bit
operations; `FMIN`/`FMAX` and the compares have their NaN rules specified in
§6.3; the conversions are specified to saturate. There is **no fused multiply-add
and no transcendental function** in v1 — the two things that actually differ
between hosts.

**Why that matters.** For this operation set, a conforming IEEE-754 host
produces identical bits, so a **soft-float library is not required** for
bit-exactness. The hazards are narrower than "host FP is nondeterministic":

1. **Double rounding in single precision.** If `.S` operations were computed in
   `double` and rounded back, results would differ from correct single-precision
   rounding. The emulator does not do this: `execute_fp` takes `float` for the
   `.S` forms and `double` for the `.D` forms (`src/core/machine.cpp`, the
   `get_float`/`put_float` helpers and the `0x000`–`0x00B` cases), and NaN
   results are canonicalised per width.
2. **Flush-to-zero / denormals-are-zero.** A host that enables FTZ or DAZ would
   break the "denormals are supported" clause. Neither x86-64 SSE2 nor ARM64
   enables them by default, but it is a *host configuration* dependency worth
   stating in the document.
3. **Extended precision.** x87 80-bit intermediates would change results; 64-bit
   x86-64 code uses SSE2 for `double`, so this does not arise on the supported
   hosts.

**Options.**

1. **Keep host IEEE, and state the two host conditions explicitly** (no
   FTZ/DAZ; no extended-precision intermediates). Add a conformance test with the
   rounding cases that catch double rounding.
2. **Require a soft-float reference implementation**, so results are defined by
   the reference rather than by the host.

**Recommendation: option 1, plus the test.** The ISA cannot use a soft-float
library's advantage — there is nothing host-dependent in its operation set —
while a soft-float requirement would slow every emulated FP operation and impose
a second implementation on every host. The honest risk is configuration, not
arithmetic, and that belongs in the document as a stated condition. If v2 adds
FMA or transcendentals, this decision must be revisited: those *are* host
dependent, and at that point a software reference becomes the only way to stay
bit-exact.

## 4. What is left for the operator

1. **Q9:** confirm "FP-always for v1, no-FP profile out of scope" (or ask for a
   profile document).
2. **Q10:** confirm the host-IEEE policy with the two stated conditions, or
   require soft-float.
3. **Then freeze:** with both answered, §13's remaining items are the pseudo-op
   list, the data-directive wording, and the assembler-syntax pass — all
   documentation work, none of which can change the encoding.
