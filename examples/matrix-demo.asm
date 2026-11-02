; Matrix demo: multiply two constant 4x4 matrices, save/restore a tile,
; and reduce its first row to a scalar dot product (256).
        MOV      R2, #matrix_a
        MLOAD    T1, [R2]
        MOV      R2, #matrix_b
        MLOAD    T2, [R2]
        MMUL     T3, T1, T2
        MVEC     T5, T3, T1
        MTRANS   T6, T5
        MOV      R3, #0x00018000
        WRSP     R3
        MPUSH    T3
        MPOP     T4
        VDOT     F0, T4, T4
        FCVT.W.S R1, F0
        MOV      R0, #0
        HALT

matrix_a:
        .word 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
        .word 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
        .word 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
        .word 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
matrix_b:
        .word 0x40000000, 0x40000000, 0x40000000, 0x40000000
        .word 0x40000000, 0x40000000, 0x40000000, 0x40000000
        .word 0x40000000, 0x40000000, 0x40000000, 0x40000000
        .word 0x40000000, 0x40000000, 0x40000000, 0x40000000
