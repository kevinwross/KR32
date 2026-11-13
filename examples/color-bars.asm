; Color bars for the framebuffer: 320x200, eight horizontal bands, then FLIP.
; Run with: kr32 display build/color-bars.k32
; The display command maps the low 4 MiB, the aperture, and the device window,
; so this standalone image can drive the control page directly.

        MOV   R0, #0xFFF04000      ; control page
        MOV   R1, #320
        STORE R1, [R0 + 4]         ; WIDTH
        MOV   R1, #200
        STORE R1, [R0 + 8]         ; HEIGHT
        MOV   R1, #0
        STORE R1, [R0 + 20]        ; FRONT = 0
        STORE R1, [R0 + 24]        ; BACK = 0
        MOV   R1, #1
        STORE R1, [R0 + 0]         ; CONTROL = enable

        MOV   R2, #0xFC000000      ; VRAM
        MOV   R5, #8000            ; 25 rows of 320 pixels per band

        MOV   R3, #0x00E6EDF3
        MOV   R4, R5
band0:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band0

        MOV   R3, #0x00F28C28
        MOV   R4, R5
band1:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band1

        MOV   R3, #0x00F2C94C
        MOV   R4, R5
band2:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band2

        MOV   R3, #0x0038C97A
        MOV   R4, R5
band3:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band3

        MOV   R3, #0x0028A0D8
        MOV   R4, R5
band4:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band4

        MOV   R3, #0x00805AC0
        MOV   R4, R5
band5:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band5

        MOV   R3, #0x00C06C9A
        MOV   R4, R5
band6:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band6

        MOV   R3, #0x00101820
        MOV   R4, R5
band7:  STORE R3, [R2]
        ADD   R2, R2, #4
        SUB   R4, R4, #1
        JNZ   band7

        MOV   R1, #1
        STORE R1, [R0 + 32]        ; FLIP
        HALT
