#include "console.h"
#include "initrd.h"
#include "memory.h"
#include "process.h"
#include "user_memory.h"

const unsigned char *initrd_address(void);

void kernel_enter_user(unsigned int entry, unsigned int stack);

#define USER_MAX_PAGES 16

mem_phys_t user_virt_to_phys(mem_virt_t address) {
    unsigned int *directory =
        (unsigned int *)mem_phys_to_virt(USER_PAGE_DIRECTORY_PHYS);
    const unsigned int directory_index = address >> 22;
    const unsigned int directory_entry = directory[directory_index];
    if ((directory_entry & 5) != 5) {
        return MEM_PHYS_NONE;
    }
    unsigned int *table =
        (unsigned int *)mem_phys_to_virt(directory_entry & 0xFFFFF000U);
    const unsigned int table_index = (address >> 12) & 0x3FF;
    const unsigned int table_entry = table[table_index];
    if ((table_entry & 5) != 5) {
        return MEM_PHYS_NONE;
    }
    return (table_entry & 0xFFFFF000U) | (address & 0xFFFU);
}

static void setup_user_pages(void) {
    unsigned int *directory =
        (unsigned int *)mem_phys_to_virt(USER_PAGE_DIRECTORY_PHYS);
    unsigned int *code =
        (unsigned int *)mem_phys_to_virt(USER_CODE_PAGE_TABLE_PHYS);
    unsigned int *stack =
        (unsigned int *)mem_phys_to_virt(USER_STACK_PAGE_TABLE_PHYS);

    unsigned int index = 0;
    while (index < 1024) {
        code[index] = 0;
        ++index;
    }
    directory[1] = USER_CODE_PAGE_TABLE_PHYS | 5; // V | U

    // Stack virtual pages 0x07E01000..0x07EFFFFF map the same physical
    // window; 0x07E00000 remains the guard page.
    for (index = 0x201; index < 0x300; ++index) {
        stack[index] = (USER_STACK_PHYS_BASE + index * 4096) | 7; // V | W | U
    }
    stack[0x200] = 0;
    directory[0x1F] = USER_STACK_PAGE_TABLE_PHYS | 5; // V | U
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

static const struct initrd_entry *initrd_entries(void) {
    const unsigned char *initrd = initrd_address();
    const struct initrd_header *header = (const struct initrd_header *)initrd;
    if (header->magic != 0x4932334B || header->version != 1) {
        return 0;
    }
    return (const struct initrd_entry *)(initrd + sizeof(struct initrd_header));
}

static const struct initrd_entry *find_entry(const char *name) {
    const unsigned char *initrd = initrd_address();
    const struct initrd_header *header = (const struct initrd_header *)initrd;
    if (header->magic != 0x4932334B || header->version != 1) {
        return 0;
    }
    const struct initrd_entry *entries =
        (const struct initrd_entry *)(initrd + sizeof(struct initrd_header));
    unsigned int index = 0;
    while (index < header->count) {
        if (name_equal(entries[index].name, name)) {
            return entries + index;
        }
        ++index;
    }
    return 0;
}

void initrd_list(void) {
    const struct initrd_entry *entries = initrd_entries();
    if (entries == 0) {
        return;
    }
    const struct initrd_header *header =
        (const struct initrd_header *)initrd_address();
    unsigned int index = 0;
    while (index < header->count) {
        console_write(entries[index].name);
        console_putc('\n');
        ++index;
    }
}

int initrd_cat(const char *name) {
    const struct initrd_entry *entry = find_entry(name);
    if (entry == 0) {
        return -1;
    }
    const unsigned char *data = initrd_address() + entry->offset;
    unsigned int index = 0;
    while (index < entry->size) {
        console_putc((char)data[index]);
        ++index;
    }
    if (entry->size == 0 || data[entry->size - 1] != '\n') {
        console_putc('\n');
    }
    return 0;
}

int run_named(const char *name) {
    const struct initrd_entry *entry = find_entry(name);
    if (entry == 0) {
        return -1;
    }
    if (entry->size == 0 || entry->size > USER_MAX_PAGES * 4096 ||
        entry->entry < USER_CODE_VA ||
        entry->entry >= USER_CODE_VA + USER_MAX_PAGES * 4096) {
        return -1;
    }

    setup_user_pages();
    unsigned int *code =
        (unsigned int *)mem_phys_to_virt(USER_CODE_PAGE_TABLE_PHYS);
    const unsigned int pages = (entry->size + 4095) / 4096;
    mem_phys_t frames[16];
    const unsigned char *source = initrd_address() + entry->offset;

    unsigned int index = 0;
    while (index < pages) {
        const mem_phys_t frame = mem_page_alloc_phys();
        if (frame == MEM_PHYS_NONE) {
            while (index > 0) {
                --index;
                mem_page_free_phys(frames[index]);
            }
            return -1;
        }
        frames[index] = frame;
        // Map the frame writable just long enough for the kernel to copy the
        // image through the user virtual address; then drop W before entry.
        code[index] = frame | 15; // V | W | U | X

        unsigned char *destination =
            (unsigned char *)(USER_CODE_VA + index * 4096);
        const unsigned int start = index * 4096;
        unsigned int offset = 0;
        while (offset < 4096 && start + offset < entry->size) {
            destination[offset] = source[start + offset];
            ++offset;
        }
        code[index] = frame | 13; // V | U | X
        ++index;
    }

    unsigned int *exit_status = (unsigned int *)0x0006F000;
    *exit_status = 0;
    int pid = process_spawn(name);
    if (pid < 0) {
        index = 0;
        while (index < pages) {
            mem_page_free_phys(frames[index]);
            ++index;
        }
        return pid;
    }
    kernel_enter_user(entry->entry, USER_STACK_TOP_VA);
    int status = (int)*exit_status;
    (void)process_exit(pid, status);
    (void)process_reap(pid);

    index = 0;
    while (index < pages) {
        mem_page_free_phys(frames[index]);
        ++index;
    }
    return status;
}
