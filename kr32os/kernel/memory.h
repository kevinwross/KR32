#pragma once

void memory_init(void);
unsigned int page_alloc(void);
void page_free(unsigned int address);
void *kmalloc(unsigned int size);
void kfree(void *pointer);
unsigned int memory_used_pages(void);
unsigned int heap_used(void);
