; KR32 M1 demo: arithmetic, a conditional branch, and a label.
; Expected: R3=30, R1=12, R0=0.

        MOV  R1, #10
        MOV  R2, #20
        ADD  R3, R1, R2
        CMP  R3, #30
        JZ   ok
        MOV  R0, #1
        HALT

ok:
        MOV  R1, #5
        MOV  R2, #7
        ADD  R1, R1, R2
        MOV  R0, #0
        HALT
