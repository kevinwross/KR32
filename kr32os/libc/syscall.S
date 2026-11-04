    .text
    .global sys_exit
sys_exit:
    SYSCALL #64
    RET

    .global sys_write
sys_write:
    SYSCALL #65
    RET

    .global sys_read
sys_read:
    SYSCALL #66
    RET

    .global sys_yield
sys_yield:
    SYSCALL #67
    RET

    .global sys_getpid
sys_getpid:
    SYSCALL #68
    RET

    .global sys_brk
sys_brk:
    SYSCALL #69
    RET
