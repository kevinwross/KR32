.file "kr32os/kernel/traps-asm.S"
; KR32OS M3 trap entry stubs and vector installation.
    .text

    .global traps_install
traps_install:
    MOV R1, #0x00050000
    LEA R2, [trap_invalid_opcode]
    STORE R2, [R1 + 4]
    LEA R2, [trap_divide_zero]
    STORE R2, [R1 + 8]
    LEA R2, [trap_breakpoint]
    STORE R2, [R1 + 12]
    LEA R2, [trap_page_fault]
    STORE R2, [R1 + 16]
    LEA R2, [trap_general_protection]
    STORE R2, [R1 + 20]
    LEA R2, [trap_alignment]
    STORE R2, [R1 + 24]
    LEA R2, [trap_double_fault]
    STORE R2, [R1 + 28]
    LEA R2, [trap_timer]
    STORE R2, [R1 + 64]
    RET

    .global kernel_enable_interrupts
kernel_enable_interrupts:
    MOV R1, #1
    WRCR CR0, R1
    RET

trap_invalid_opcode:
    MOV R0, #1
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_divide_zero:
    MOV R0, #2
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_breakpoint:
    MOV R0, #3
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_page_fault:
    MOV R0, #4
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_general_protection:
    MOV R0, #5
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_alignment:
    MOV R0, #6
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_double_fault:
    MOV R0, #7
    LEA R1, [SP]
    CALL trap_dispatch
    MOV R0, #1
    HALT

trap_timer:
    PUSH R0
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
    CALL timer_tick
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
    POP R0
    IRET
