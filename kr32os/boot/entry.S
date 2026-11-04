; KR32OS kernel entry. The BIOS has already selected the kernel stack.
    .text
    .global _start
_start:
    CALL kmain
    MOV R0, #0
    SYSCALL #32
    HALT

; Temporary M0 debug console path. The OS serial device replaces this in M2.
    .global putchar
putchar:
    SYSCALL #33
    RET
