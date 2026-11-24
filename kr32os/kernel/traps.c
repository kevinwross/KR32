#include "console.h"
#include "syscall.h"
#include "trap.h"

void kernel_leave_user(void);

void trap_dispatch(int vector, unsigned int *frame) {
    if ((frame[TRAP_FRAME_FLAGS] & TRAP_USER_MODE_BIT) == 0) {
        volatile struct syscall_scratch *scratch =
            (volatile struct syscall_scratch *)SYSCALL_SCRATCH_PHYS;
        scratch->result = USER_FAULT_STATUS;
        kernel_leave_user();
    }
    console_write("panic vector=");
    console_write_uint((unsigned int)vector);
    // Every frame word, not just the one the header names: a diagnostic that reads
    // the wrong slot is worse than none, because it names an address that never
    // executed.
    console_write(" frame=");
    for (int word = 0; word < 4; ++word) {
        console_write("0x");
        console_write_uint(frame[word]);
        console_write(" ");
    }
    console_write("\n");
}
