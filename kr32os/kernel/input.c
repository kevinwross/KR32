#include "input.h"

#include "errno.h"
#include "mmio.h"
#include "user_memory.h"

static volatile struct keyboard_registers *keyboard(void) {
    return (volatile struct keyboard_registers *)KEYBOARD_BASE;
}

int input_pending(void) {
    volatile struct keyboard_registers *device = keyboard();
    return (device->status & KEYBOARD_STATUS_AVAILABLE) != 0;
}

int input_poll_from_user(unsigned int address) {
    volatile struct keyboard_registers *device = keyboard();
    if ((device->status & KEYBOARD_STATUS_AVAILABLE) == 0) {
        return 0;
    }
    const unsigned int event = device->data;
    unsigned int index = 0;
    while (index < 4) {
        const mem_phys_t physical =
            user_virt_to_phys((mem_virt_t)(address + index));
        if (physical == MEM_PHYS_NONE) {
            return -ERRNO_EFAULT;
        }
        *(unsigned char *)mem_phys_to_virt(physical) =
            (unsigned char)((event >> (index * 8)) & 0xFFU);
        ++index;
    }
    return 1;
}
