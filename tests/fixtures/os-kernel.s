.text
.global _start
_start:
    MOV R0, #42
    SYSCALL #32
    HALT
