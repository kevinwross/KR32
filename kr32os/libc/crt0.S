    .text
    .global _start
_start:
    CALL main
    SYSCALL #64
    HALT
