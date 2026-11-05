#include "console.h"
#include "memory.h"
#include "user_memory.h"

void sys_exit(int status) {
    // Kernel scratch page, identity-mapped low memory.
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
    const mem_virt_t address = (mem_virt_t)buffer;
    if (address < USER_CODE_VA || address >= USER_STACK_TOP_VA) {
        return -14; // EFAULT
    }
    int index = 0;
    while (index < length) {
        const mem_virt_t current = address + (mem_virt_t)index;
        const mem_phys_t physical = user_virt_to_phys(current);
        if (physical == MEM_PHYS_NONE) {
            return -14; // EFAULT
        }
        console_putc(*(const char *)mem_phys_to_virt(physical));
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
