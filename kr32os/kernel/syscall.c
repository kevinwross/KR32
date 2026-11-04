#include "console.h"

void sys_exit(int status) {
    unsigned int *result = (unsigned int *)0x0006F000;
    *result = (unsigned int)status;
}

int sys_write(int descriptor, const char *buffer, int length) {
    if (descriptor != 1 && descriptor != 2) {
        return -9; // EBADF
    }
    if (length < 0) {
        return -22; // EINVAL
    }
    const unsigned int address = (unsigned int)buffer;
    if (address < 0x00400000 || address >= 0x07F00000) {
        return -14; // EFAULT
    }
    int index = 0;
    while (index < length) {
        console_putc(buffer[index]);
        ++index;
    }
    return length;
}

int sys_read(int descriptor, char *buffer, int length) {
    (void)descriptor;
    (void)buffer;
    (void)length;
    return 0;
}

int sys_yield(void) {
    return 0;
}

int sys_getpid(void) {
    return 1;
}

int sys_brk(unsigned int address) {
    (void)address;
    return 0;
}

int sys_dispatch(int argument0, int argument1, int argument2) {
    unsigned int *vector = (unsigned int *)0x0006F004;
    if (*vector == 66) {
        return sys_read(argument0, (char *)argument1, argument2);
    }
    if (*vector == 67) {
        return sys_yield();
    }
    if (*vector == 68) {
        return sys_getpid();
    }
    if (*vector == 69) {
        return sys_brk((unsigned int)argument0);
    }
    return -38; // ENOSYS
}
