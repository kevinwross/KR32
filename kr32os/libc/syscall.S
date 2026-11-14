    .equ SYSCALL_EXIT, 64
.equ SYSCALL_WRITE, 65
.equ SYSCALL_READ, 66
.equ SYSCALL_YIELD, 67
.equ SYSCALL_GETPID, 68
.equ SYSCALL_BRK, 69
.equ SYSCALL_GFX, 70
.equ SYSCALL_GFX_ALLOC, 71
.equ SYSCALL_INPUT_POLL, 72
.equ SYSCALL_INPUT_PENDING, 73
.equ SYSCALL_CON, 74
.equ SYSCALL_HALT, 75

    .text
    .global sys_exit
sys_exit:
    SYSCALL #SYSCALL_EXIT
    RET

    .global sys_write
sys_write:
    SYSCALL #SYSCALL_WRITE
    RET

    .global sys_read
sys_read:
    SYSCALL #SYSCALL_READ
    RET

    .global sys_yield
sys_yield:
    SYSCALL #SYSCALL_YIELD
    RET

    .global sys_getpid
sys_getpid:
    SYSCALL #SYSCALL_GETPID
    RET

    .global sys_brk
sys_brk:
    SYSCALL #SYSCALL_BRK
    RET

    .global sys_gfx
sys_gfx:
    SYSCALL #SYSCALL_GFX
    RET

    .global sys_gfx_alloc
sys_gfx_alloc:
    SYSCALL #SYSCALL_GFX_ALLOC
    RET

    .global sys_input_poll
sys_input_poll:
    SYSCALL #SYSCALL_INPUT_POLL
    RET

    .global sys_input_pending
sys_input_pending:
    SYSCALL #SYSCALL_INPUT_PENDING
    RET

    .global sys_con
sys_con:
    SYSCALL #SYSCALL_CON
    RET

    .global sys_halt
sys_halt:
    SYSCALL #SYSCALL_HALT
    RET
