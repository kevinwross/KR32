#include "memory.h"

static unsigned int mem_bitmap[1024];
static unsigned int mem_used_count = 0;
static unsigned int mem_heap_next = 0x00300000;
static unsigned int mem_heap_start = 0x00300000;
static unsigned int mem_heap_end = 0x003F0000;

static int page_is_used(unsigned int page);

static void mark_used(unsigned int page) {
    if (page < 32768) {
        if (page_is_used(page) == 0) {
            ++mem_used_count;
        }
        mem_bitmap[page >> 5] |= 1U << (page & 31);
    }
}

static void mark_free(unsigned int page) {
    if (page < 32768) {
        if (page_is_used(page) != 0) {
            --mem_used_count;
        }
        mem_bitmap[page >> 5] &= ~(1U << (page & 31));
    }
}

static int page_is_used(unsigned int page) {
    return page < 32768 && (mem_bitmap[page >> 5] & (1U << (page & 31))) != 0;
}

void memory_init(void) {
    unsigned int index = 0;
    while (index < 1024) {
        mem_bitmap[index] = 0;
        ++index;
    }
    mem_used_count = 0;
    index = 0;
    while (index < 1024) {
        mark_used(index);
        ++index;
    }
    // Static M4 user image and code page-table window.
    index = 1024;
    while (index < 1088) {
        mark_used(index);
        ++index;
    }
    mem_heap_next = mem_heap_start;
}

unsigned int page_alloc(void) {
    unsigned int page = 1088;
    while (page < 32768) {
        if (!page_is_used(page)) {
            mark_used(page);
            return page * 4096;
        }
        ++page;
    }
    return 0xFFFFFFFF;
}

void page_free(unsigned int address) {
    if ((address & 4095) == 0) {
        mark_free(address / 4096);
    }
}

void *kmalloc(unsigned int size) {
    unsigned int aligned = (size + 7) & ~7U;
    if (aligned == 0 || mem_heap_next + aligned > mem_heap_end) {
        return 0;
    }
    void *result = (void *)mem_heap_next;
    mem_heap_next += aligned;
    return result;
}

void kfree(void *pointer) {
    (void)pointer;
}

unsigned int memory_used_pages(void) {
    return mem_used_count;
}

unsigned int heap_used(void) {
    return mem_heap_next - mem_heap_start;
}
