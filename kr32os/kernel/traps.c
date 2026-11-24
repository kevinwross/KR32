#include "console.h"
#include "syscall.h"
#include "trap.h"

void kernel_leave_user(void);

// CR9 holds the fault address, read by a three-instruction assembly helper: the
// C front end has no inline assembly, and a page fault's victim PC says where the
// instruction was while only FAR says what it touched.
unsigned int kernel_read_far(void);

void trap_dispatch(int vector, unsigned int *frame) {
    // Bit 0x20 is kFlagK: set means the trap was taken in *kernel* mode, so the
    // test is "K clear" -- user mode -- and the earlier "!=" I wrote here was
    // backwards. The comment in trap.h names the bit TRAP_USER_MODE_BIT, which is
    // what misled me; the hardware calls it the kernel bit.
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
    // Hex, and the fault address from CR9: a page fault's victim PC says where the
    // instruction was, and only FAR says what it touched.
    console_write(" pc=");
    console_write_hex(frame[TRAP_FRAME_PC]);
    console_write(" sp=");
    console_write_hex(frame[2]);
    console_write(" flags=");
    console_write_hex(frame[TRAP_FRAME_FLAGS]);
    console_write(" far=");
    console_write_hex(kernel_read_far());
    console_write("\n");
}
