; Kernel/user context bridge and syscall entry stubs.
    .section .bss
    .align 4
__kernel_saved_sp:
    .space 4
__kernel_saved_regs:
    .space 64

    .text
    .global initrd_address
initrd_address:
    LEA R0, [__initrd_start]
    RET

    .global user_install_syscalls
user_install_syscalls:    MOV R1, #0x00050000
    LEA R2, [syscall_exit_stub]
    STORE R2, [R1 + 256]         ; vector 64
    LEA R2, [syscall_write_stub]
    STORE R2, [R1 + 260]         ; vector 65
    RET

    .global kernel_enter_user
kernel_enter_user:
    ; Save all kernel registers, then the kernel stack (which holds the
    ; return address of the C call).
    MOV R15, #__kernel_saved_regs
    STORE R0, [R15]
    STORE R1, [R15 + 4]
    STORE R2, [R15 + 8]
    STORE R3, [R15 + 12]
    STORE R4, [R15 + 16]
    STORE R5, [R15 + 20]
    STORE R6, [R15 + 24]
    STORE R7, [R15 + 28]
    STORE R8, [R15 + 32]
    STORE R9, [R15 + 36]
    STORE R10, [R15 + 40]
    STORE R11, [R15 + 44]
    STORE R12, [R15 + 48]
    STORE R13, [R15 + 52]
    STORE R14, [R15 + 56]
    STORE R15, [R15 + 60]
    LEA R12, [SP]
    STORE R12, [__kernel_saved_sp]

    ; User->kernel traps use a dedicated kernel stack so they cannot overwrite
    ; the saved shell context.
    MOV R12, #0x000E0000
    WRCR CR6, R12

    ; R0 = user entry, R1 = user stack. Build the IRET frame.
    ADJSP #-16
    STORE R0, [SP]
    MOV R2, #0x10                ; user mode, interrupts enabled
    STORE R2, [SP + 4]
    STORE R1, [SP + 8]
    MOV R2, #0
    STORE R2, [SP + 12]
    IRET

    .global kernel_leave_user
kernel_leave_user:
    ; Restore the kernel stack and registers, then return to the C caller.
    MOV R12, #0x00100000
    WRCR CR6, R12
    MOV R15, #__kernel_saved_regs
    LOAD R0, [R15]
    LOAD R2, [R15 + 8]
    LOAD R3, [R15 + 12]
    LOAD R4, [R15 + 16]
    LOAD R5, [R15 + 20]
    LOAD R6, [R15 + 24]
    LOAD R7, [R15 + 28]
    LOAD R8, [R15 + 32]
    LOAD R9, [R15 + 36]
    LOAD R10, [R15 + 40]
    LOAD R11, [R15 + 44]
    LOAD R13, [R15 + 52]
    LOAD R14, [R15 + 56]
    LOAD R1, [__kernel_saved_sp]
    SETSP R1
    LOAD R1, [R15 + 4]
    LOAD R12, [R15 + 48]
    LOAD R15, [R15 + 60]
    RET

    .global syscall_write_stub
syscall_write_stub:
    PUSH R1
    PUSH R2
    PUSH R3
    PUSH R4
    PUSH R5
    PUSH R6
    PUSH R7
    PUSH R8
    PUSH R9
    PUSH R10
    PUSH R11
    PUSH R12
    PUSH R13
    PUSH R14
    PUSH R15
    CALL sys_write
    POP R15
    POP R14
    POP R13
    POP R12
    POP R11
    POP R10
    POP R9
    POP R8
    POP R7
    POP R6
    POP R5
    POP R4
    POP R3
    POP R2
    POP R1
    IRET

    .global syscall_exit_stub
syscall_exit_stub:
    MOV R2, #0x0006F000
    STORE R0, [R2]
    JMP kernel_leave_user
