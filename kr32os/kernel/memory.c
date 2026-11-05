#include "memory.h"

static unsigned int mem_physical_bitmap[1024];
static unsigned int mem_used_count = 0;
static mem_phys_t mem_heap_phys_next = 0x00300000;
static mem_phys_t mem_heap_phys_start = 0x00300000;
static mem_phys_t mem_heap_phys_end = 0x003F0000;

static int page_is_used(unsigned int page);

static void mark_used(unsigned int page) {
    if (page < 32768) {
        if (page_is_used(page) == 0) {
            ++mem_used_count;
        }
        mem_physical_bitmap[page >> 5] |= 1U << (page & 31);
    }
}

static void mark_free(unsigned int page) {
    if (page < 32768) {
        if (page_is_used(page) != 0) {
            --mem_used_count;
        }
        mem_physical_bitmap[page >> 5] &= ~(1U << (page & 31));
    }
}

static int page_is_used(unsigned int page) {
    return page < 32768 && (mem_physical_bitmap[page >> 5] & (1U << (page & 31))) != 0;
}

void memory_init(void) {
    unsigned int index = 0;
    while (index < 1024) {
        mem_physical_bitmap[index] = 0;
        ++index;
    }
    mem_used_count = 0;
    // Low 4 MiB: BIOS, page tables, vectors, kernel image, initrd, and heap.
    index = 0;
    while (index < 1024) {
        mark_used(index);
        ++index;
    }
    // User stack window 0x07E00000..0x07EFFFFF, mapped identity by user.c.
    index = 32256;
    while (index < 32512) {
        mark_used(index);
        ++index;
    }
    mem_heap_phys_next = mem_heap_phys_start;
}

mem_phys_t mem_page_alloc_phys(void) {
    unsigned int page = 1024;
    while (page < 32768) {
        if (!page_is_used(page)) {
            mark_used(page);
            return page * 4096;
        }
        ++page;
    }
    return MEM_PHYS_NONE;
}

void mem_page_free_phys(mem_phys_t address) {
    if ((address & 4095) == 0) {
        mark_free(address / 4096);
    }
}

void *mem_phys_to_virt(mem_phys_t physical) {
    return (void *)physical;
}

mem_phys_t mem_virt_to_phys(void *virtual_address) {
    return (mem_phys_t)virtual_address;
}

void *kmalloc(unsigned int size) {
    unsigned int aligned = (size + 7) & ~7U;
    if (aligned == 0 || mem_heap_phys_next + aligned > mem_heap_phys_end) {
        return 0;
    }
    void *result = mem_phys_to_virt(mem_heap_phys_next);
    mem_heap_phys_next += aligned;
    return result;
}

void kfree(void *pointer) {
    (void)pointer;
}

unsigned int memory_used_pages(void) {
    return mem_used_count;
}

unsigned int heap_used(void) {
    return mem_heap_phys_next - mem_heap_phys_start;
}
