#include "console.h"
#include "process.h"

const unsigned char *initrd_address(void);

void kernel_enter_user(unsigned int entry, unsigned int stack);

#define USER_CODE_BASE 0x00400000
#define USER_STACK_TOP 0x07F00000
#define USER_STACK_PT 0x00071000
#define USER_CODE_PT 0x00070000
#define PAGE_DIRECTORY 0x00040000

static void setup_user_pages(unsigned int image_size) {
    unsigned int *directory = (unsigned int *)PAGE_DIRECTORY;
    unsigned int *code = (unsigned int *)USER_CODE_PT;
    unsigned int *stack = (unsigned int *)USER_STACK_PT;
    unsigned int pages = (image_size + 4095) / 4096;
    if (pages == 0) {
        pages = 1;
    }
    unsigned int index = 0;
    while (index < pages && index < 16) {
        code[index] = (USER_CODE_BASE + index * 4096) | 15; // load with W
        ++index;
    }
    while (index < 16) {
        code[index] = 0;
        ++index;
    }
    directory[1] = USER_CODE_PT | 5; // V | U

    // Stack pages 0x07E01000..0x07EFFFFF; 0x07E00000 is the guard.
    for (index = 0x201; index < 0x300; ++index) {
        stack[index] = (0x07C00000 + index * 4096) | 7; // V | W | U
    }
    stack[0x200] = 0;
    directory[0x1F] = USER_STACK_PT | 5; // V | U
}

int run_named(const char *name);

int run_init(void) {
    return run_named("init");
}

static int name_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static const unsigned char *find_entry(const char *name) {
    const unsigned char *initrd = initrd_address();
    const unsigned int *header = (const unsigned int *)initrd;
    if (header[0] != 0x4932334B || header[1] != 1) {
        return 0;
    }
    unsigned int index = 0;
    while (index < header[2]) {
        const unsigned char *entry = initrd + 12 + index * 44;
        if (name_equal((const char *)entry, name)) {
            return entry;
        }
        ++index;
    }
    return 0;
}

void initrd_list(void) {
    const unsigned char *initrd = initrd_address();
    const unsigned int *header = (const unsigned int *)initrd;
    if (header[0] != 0x4932334B || header[1] != 1) {
        return;
    }
    unsigned int index = 0;
    while (index < header[2]) {
        const unsigned char *entry = initrd + 12 + index * 44;
        console_write((const char *)entry);
        console_putc('\n');
        ++index;
    }
}

int initrd_cat(const char *name) {
    const unsigned char *entry = find_entry(name);
    if (entry == 0) {
        return -1;
    }
    const unsigned int offset = *(const unsigned int *)(entry + 32);
    const unsigned int size = *(const unsigned int *)(entry + 36);
    const unsigned char *data = initrd_address() + offset;
    unsigned int index = 0;
    while (index < size) {
        console_putc((char)data[index]);
        ++index;
    }
    if (size == 0 || data[size - 1] != '\n') {
        console_putc('\n');
    }
    return 0;
}

int run_named(const char *name) {
    const unsigned char *entry = find_entry(name);
    if (entry == 0) {
        return -1;
    }
    const unsigned int offset = *(const unsigned int *)(entry + 32);
    const unsigned int size = *(const unsigned int *)(entry + 36);
    const unsigned int entry_address = *(const unsigned int *)(entry + 40);
    if (size == 0 || size > 16 * 4096 || entry_address < USER_CODE_BASE ||
        entry_address >= USER_CODE_BASE + 16 * 4096) {
        return -1;
    }
    setup_user_pages(size);
    unsigned char *destination = (unsigned char *)USER_CODE_BASE;
    const unsigned char *source = initrd_address() + offset;
    unsigned int index = 0;
    while (index < size) {
        destination[index] = source[index];
        ++index;
    }
    unsigned int pages = (size + 4095) / 4096;
    unsigned int *code = (unsigned int *)USER_CODE_PT;
    for (index = 0; index < pages; ++index) {
        code[index] = (USER_CODE_BASE + index * 4096) | 13;
    }
    unsigned int *exit_status = (unsigned int *)0x0006F000;
    *exit_status = 0;
    int pid = process_spawn(name);
    if (pid < 0) {
        return pid;
    }
    kernel_enter_user(entry_address, USER_STACK_TOP);
    int status = (int)*exit_status;
    (void)process_exit(pid, status);
    return status;
}
