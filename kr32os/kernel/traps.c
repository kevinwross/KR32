#include "console.h"

void trap_dispatch(int vector, unsigned int *frame) {
    console_write("panic vector=");
    console_write_uint((unsigned int)vector);
    console_write(" pc=0x");
    console_write_uint(frame[0]);
    console_write("\n");
}
