# Embedded UI fonts

The portable KR32 canvas embeds the same font families used by KR16:

- **Inter Regular / Inter Bold** — Rasmus Andersson, SIL Open Font License 1.1.
- **JetBrains Mono Regular / Bold** — JetBrains, SIL Open Font License 1.1.
- **stb_truetype** — Sean Barrett, public domain / MIT.

The font binaries are embedded in
`src/resources/embedded_fonts.cpp`; licenses are in this directory. The UI uses
Inter 14 px for labels and JetBrains Mono 15 px / 13 px for source,
disassembly, registers, and secondary labels, matching KR16's font roles.
