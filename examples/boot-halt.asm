; Minimal boot ROM: reset vector jumps to a kernel HALT.
        .word entry
entry:
        HALT
