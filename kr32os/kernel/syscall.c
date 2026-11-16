#include "console.h"
#include "errno.h"
#include "gfx.h"
#include "input.h"
#include "memory.h"
#include "syscall.h"
#include "process.h"
#include "user_memory.h"

void sys_exit(int status) {
    volatile struct syscall_scratch *scratch =
        (volatile struct syscall_scratch *)SYSCALL_SCRATCH_PHYS;
    scratch->result = (unsigned int)status;
}

int sys_write(int descriptor, const char *buffer, int length) {
    if (descriptor != SYSCALL_FD_STDOUT &&
        descriptor != SYSCALL_FD_STDERR) {
        return -ERRNO_EBADF;
    }
    if (length < 0) {
        return -ERRNO_EINVAL;
    }
    const mem_virt_t address = (mem_virt_t)buffer;
    if (address < USER_CODE_VA || address >= USER_STACK_TOP_VA) {
        return -ERRNO_EFAULT;
    }
    int index = 0;
    while (index < length) {
        const mem_virt_t current = address + (mem_virt_t)index;
        const mem_phys_t physical = user_virt_to_phys(current);
        if (physical == MEM_PHYS_NONE) {
            return -ERRNO_EFAULT;
        }
        console_putc(*(const char *)mem_phys_to_virt(physical));
        ++index;
    }
    return length;
}

// Reads one line from the console, which is fed by the keyboard and the serial
// device. The call blocks: a program that wants a key waits here rather than
// polling input_pending. Returns the bytes read, including the newline.
int sys_read(int descriptor, char *buffer, int length) {
    if (descriptor != SYSCALL_FD_STDIN) {
        return -ERRNO_EBADF;
    }
    if (length < 0) {
        return -ERRNO_EINVAL;
    }
    if (length == 0) {
        return 0;
    }
    const mem_virt_t address = (mem_virt_t)buffer;
    if (address < USER_CODE_VA || address >= USER_STACK_TOP_VA) {
        return -ERRNO_EFAULT;
    }
    int index = 0;
    while (index < length) {
        const int character = console_getc();
        if (character < 0) {
            // Nothing pending: block. The serial device delivers a byte at a
            // time, so returning a partial line here would split it.
            continue;
        }
        const mem_virt_t current = address + (mem_virt_t)index;
        const mem_phys_t physical = user_virt_to_phys(current);
        if (physical == MEM_PHYS_NONE) {
            return -ERRNO_EFAULT;
        }
        *(char *)mem_phys_to_virt(physical) = (char)character;
        ++index;
        if (character == '\n') {
            break;
        }
    }
    return index;
}

// The kernel runs one process at a time, so there is nothing to yield to; the
// call is a no-op rather than an error so portable code can call it.
int sys_yield(void) {
    return 0;
}

int sys_getpid(void) {
    return process_current();
}

// A user image is loaded at a fixed address with a fixed stack, and the kernel
// heap belongs to the kernel, so a user program cannot move its break. The call
// succeeds and changes nothing; init uses it to reset an address it ignores.
int sys_brk(unsigned int address) {
    (void)address;
    return 0;
}

int sys_con(int op, int value) {
    if (op == 1) {
        console_set_colors(value & 0x0F, (value >> 4) & 0x0F);
        return 0;
    }
    if (op == 2) {
        console_set_cursor(value != 0);
        return 0;
    }
    if (op == 3) {
        console_clear();
        return 0;
    }
    if (op == 4) {
        console_present();
        return 0;
    }
    if (op == 5) {
        console_hide(value != 0);
        return 0;
    }
    return -ERRNO_EINVAL;
}

void kernel_halt(int status);

int sys_dispatch(int argument0, int argument1, int argument2) {
    volatile struct syscall_scratch *scratch =
        (volatile struct syscall_scratch *)SYSCALL_SCRATCH_PHYS;
    if (scratch->vector == SYSCALL_READ) {
        return sys_read(argument0, (char *)argument1, argument2);
    }
    if (scratch->vector == SYSCALL_YIELD) {
        return sys_yield();
    }
    if (scratch->vector == SYSCALL_GETPID) {
        return sys_getpid();
    }
    if (scratch->vector == SYSCALL_BRK) {
        return sys_brk((unsigned int)argument0);
    }
    if (scratch->vector == SYSCALL_GFX) {
        return gfx_submit_from_user((unsigned int)argument0);
    }
    if (scratch->vector == SYSCALL_GFX_ALLOC) {
        return (int)gfx_alloc((unsigned int)argument0,
                             (unsigned int)argument1);
    }
    if (scratch->vector == SYSCALL_INPUT_POLL) {
        return input_poll_from_user((unsigned int)argument0);
    }
    if (scratch->vector == SYSCALL_INPUT_PENDING) {
        return input_pending();
    }
    if (scratch->vector == SYSCALL_CON) {
        return sys_con(argument0, argument1);
    }
    if (scratch->vector == SYSCALL_HALT) {
        kernel_halt(0);
        return 0;
    }
    if (scratch->vector == SYSCALL_SHUTDOWN) {
        kernel_halt(argument0);
        return 0;
    }
    return -ERRNO_ENOSYS;
}
