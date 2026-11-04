; KR32OS M1 BIOS: validate BootInfo, identity-map the low kernel window,
; install a default vector table, enable paging, and enter the kernel.
.word bios_entry

bios_entry:
    ; R2 is the loader-provided BootInfo pointer.
    LOAD R3, [R2]
    MOV R4, #0x4232334B          ; "K32B" little-endian
    CMP R3, R4
    JNZ .Lbad_boot
    LOAD R3, [R2 + 4]
    MOV R4, #1
    CMP R3, R4
    JNZ .Lbad_boot
    LOAD R3, [R2 + 8]            ; struct size
    MOV R4, #56
    CMP R3, R4
    JNZ .Lbad_boot

    ; Move the BIOS stack into the low identity window before paging.
    MOV R1, #0x00100000
    SETSP R1
    WRCR CR6, R1

    ; Low 4 MiB page table at 0x41000. Identity-map RAM.
    MOV R1, #0x00041000
    MOV R2, #0
    MOV R3, #0
    MOV R4, #11                  ; V | W | X (X is bit 3)
.Llow_pages:
    ADD R4, R3, #11              ; V | W | X (X is bit 3)
    STORE R4, [R1]
    ADD R1, R1, #4
    ADD R3, R3, #0x1000
    ADD R2, R2, #1
    CMP R2, #1024
    JLT .Llow_pages

    ; Leave the guard region and the stack guard page unmapped.
    MOV R1, #0x00041000
    MOV R2, #0
.Lclear_guard:
    STORE R2, [R1]
    ADD R1, R1, #4
    ADD R2, R2, #1
    CMP R2, #16
    JLT .Lclear_guard
    MOV R1, #0x00041180          ; PTE for 0x00060000
    MOV R2, #0
    STORE R2, [R1]

    ; Page directory entry 0 -> low page table.
    MOV R1, #0x00041001
    STORE R1, [0x00040000]

    ; High page table at 0x42000 for PDE 0x3FF
    ; (0xFFC0_0000..0xFFFF_FFFF): MMIO and ROM.
    MOV R1, #0x00042C00          ; leaf 0x300: 0xFFF0_0000
    MOV R2, #0
    MOV R3, #0xFFF00000
.Lmmio_pages:
    ADD R4, R3, #3               ; V | W
    STORE R4, [R1]
    ADD R1, R1, #4
    ADD R3, R3, #0x1000
    ADD R2, R2, #1
    CMP R2, #6                   ; timer, block, console, IRQ, fb control, serial
    JLT .Lmmio_pages

    ; ROM pages at leaf index 0x3F0.
    MOV R1, #0x00042FC0
    MOV R2, #0
    MOV R3, #0xFFFF0000
.Lrom_pages:
    ADD R4, R3, #9               ; V | X (X is bit 3)
    STORE R4, [R1]
    ADD R1, R1, #4
    ADD R3, R3, #0x1000
    ADD R2, R2, #1
    CMP R2, #16
    JLT .Lrom_pages
    MOV R1, #0x00042001
    STORE R1, [0x00040FFC]       ; PDE 0x3FF

    ; Framebuffer page tables at 0x43000..0x4A000 for 0xFC00_0000..0xFDFF_FFFF.
    MOV R8, #0x00043000
    MOV R9, #0x00040FC0
    MOV R6, #8
    MOV R3, #0xFC000000
.Lfb_tables:
    MOV R1, R8
    MOV R2, #0
.Lfb_pages:
    ADD R5, R3, #3               ; V | W, no X
    STORE R5, [R1]
    ADD R1, R1, #4
    ADD R3, R3, #0x1000
    ADD R2, R2, #1
    CMP R2, #1024
    JLT .Lfb_pages
    ADD R5, R8, #1
    STORE R5, [R9]
    ADD R8, R8, #0x1000
    ADD R9, R9, #4
    SUB R6, R6, #1
    CMP R6, #0
    JNZ .Lfb_tables

    ; Default vector table at 0x50000 points at a halt stub.
    MOV R8, #0x00050000
    MOV R9, #0
    LEA R10, [.Ldefault_handler]
.Lvector_loop:
    STORE R10, [R8]
    ADD R8, R8, #4
    ADD R9, R9, #1
    CMP R9, #256
    JLT .Lvector_loop

    ; Control registers.
    MOV R1, #0x00040000
    WRCR CR8, R1                 ; PTBR
    MOV R1, #0x00050000
    WRCR CR3, R1                 ; VTBR
    MOV R1, #0x00100000
    WRCR CR6, R1
    SETSP R1
    MOV R1, #1
    WRCR CR10, R1                ; enable paging

    ; The BIOS stays in kernel mode and jumps to the kernel entry.
    JMP 0x00020000

.Ldefault_handler:
    MOV R0, #1
    HALT

.Lbad_boot:
    MOV R0, #2
    HALT
