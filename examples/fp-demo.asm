; Scalar FP demo: 1.5 + 2.25 = 3.75, converted to integer 3.
        MOV      R2, #half
        FLOAD.W  F1, [R2]
        MOV      R2, #quarter
        FLOAD.W  F2, [R2]
        FADD.S   F0, F1, F2
        FCVT.W.S R1, F0
        MOV      R0, #0
        HALT

half:    .word 0x3FC00000
quarter: .word 0x40100000
