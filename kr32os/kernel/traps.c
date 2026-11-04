#include "console.h"

void kernel_leave_user(void);

void trap_dispatch(int vector, unsigned int *frame) {
    if ((frame[1] & 0x20U) == 0) {
        unsigned int *status = (unsigned int *)0x0006F000;
        *status = 139; // user fault reported as SIGSEGV-like exit
        kernel_leave_user();
    }
    console_write("panic vector=");
    console_write_uint((unsigned int)vector);
    console_write(" pc=0x");
    console_write_uint(frame[0]);
    console_write("\n");
}
