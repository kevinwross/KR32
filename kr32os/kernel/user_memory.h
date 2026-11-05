#pragma once

#include "memory.h"

// User-visible virtual addresses.
#define USER_CODE_VA 0x00400000
#define USER_STACK_TOP_VA 0x07F00000

// Physical frames holding the user page tables.
#define USER_PAGE_DIRECTORY_PHYS 0x00040000
#define USER_CODE_PAGE_TABLE_PHYS 0x00070000
#define USER_STACK_PAGE_TABLE_PHYS 0x00071000

// Physical window the identity-mapped user stack comes from.
#define USER_STACK_PHYS_BASE 0x07C00000

// Translate a user virtual address through the active user page directory.
// Returns MEM_PHYS_NONE when the page is not present or not user-accessible.
mem_phys_t user_virt_to_phys(mem_virt_t address);
