.file "kr32os/boot/entry.S"
.extern kmain
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

; R0 carries the status, which becomes the machine's exit code.
    .global kernel_halt
kernel_halt:
    SYSCALL #32
    HALT

    .global kernel_break
kernel_break:
    BRK
    RET
