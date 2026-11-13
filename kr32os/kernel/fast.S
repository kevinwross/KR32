.file "kr32os/kernel/fast.S"

; Leaf fill routines for paths where the instruction count matters.
;
; C ABI: the first three integer arguments arrive in R0, R1, and R2. These
; routines never call out and only use R0-R4, so the frame pointer R7 and the
; caller's stack are untouched.

    .text

    .global fast_WordFill
; void fast_WordFill(int cntWords, void *mem, unsigned int fillValue)
fast_WordFill:
    CMP R0, #0
    JLE .Lword_done
.Lword_loop:
    STORE R2, [R1]
    ADD R1, R1, #4
    SUB R0, R0, #1
    JNZ .Lword_loop
.Lword_done:
    RET

    .global fast_ByteFill
; void fast_ByteFill(int cntBytes, void *mem, unsigned char fillValue)
fast_ByteFill:
    CMP R0, #0
    JLE .Lbyte_done
    AND R2, R2, #255
    MOV R3, R2
    SHL R4, R3, #8
    OR R3, R3, R4
    MOV R4, R3
    SHL R4, R4, #16
    OR R3, R3, R4
    ; Bytes before the buffer reaches a word boundary go through STORE8, so
    ; the word loop only ever writes aligned addresses.
    MOV R5, R1
    AND R5, R5, #3
    CMP R5, #0
    JZ .Lbyte_words_setup
    MOV R4, #4
    SUB R4, R4, R5
    CMP R4, R0
    JLE .Lbyte_head
    MOV R4, R0
.Lbyte_head:
    STORE8 R2, [R1]
    ADD R1, R1, #1
    SUB R0, R0, #1
    SUB R4, R4, #1
    JNZ .Lbyte_head
.Lbyte_words_setup:
    MOV R4, R0
    SHR R4, R4, #2
    AND R0, R0, #3
    CMP R4, #0
    JLE .Lbyte_tail
.Lbyte_words:
    STORE R3, [R1]
    ADD R1, R1, #4
    SUB R4, R4, #1
    JNZ .Lbyte_words
.Lbyte_tail:
    CMP R0, #0
    JLE .Lbyte_done
.Lbyte_tail_loop:
    STORE8 R2, [R1]
    ADD R1, R1, #1
    SUB R0, R0, #1
    JNZ .Lbyte_tail_loop
.Lbyte_done:
    RET
