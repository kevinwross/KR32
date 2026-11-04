; KR32OS M0 BIOS: reset vector jumps to the kernel entry.
.word bios_entry
bios_entry:
    JMP 0x00020000
