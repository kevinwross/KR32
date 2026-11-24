#include "console.h"
#include "syscall.h"
#include "trap.h"

void kernel_leave_user(void);

// CR9 holds the fault address, read by a three-instruction assembly helper: the
// C front end has no inline assembly, and a page fault's victim PC says where the
// instruction was while only FAR says what it touched.
unsigned int kernel_read_far(void);

void trap_dispatch(int vector, unsigned int *frame) {
    // A fault taken in user mode is the process's problem, not the kernel's: the
    // syscall result is set and the kernel leaves user mode, which is how a wild
    // pointer in a program becomes a failed syscall. The condition was inverted --
    // it abandoned to the kernel on a *kernel* fault and panicked on a user one --
    // so the first bad pointer in any program halted the machine, and the panic
    // line that followed named the user's instruction as though the kernel had
    // executed it.
    if ((frame[TRAP_FRAME_FLAGS] & TRAP_USER_MODE_BIT) != 0) {
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
