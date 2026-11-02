        .text
        .global _start
        .extern foo
_start:
        CALL foo
        HALT
