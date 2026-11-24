.equ SYSCALL_EXIT, 64

    .text
    .global _start
.extern main
_start:
    CALL main
    SYSCALL #SYSCALL_EXIT
    HALT
