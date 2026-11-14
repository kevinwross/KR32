#include "gfx.h"

#include "errno.h"
#include "mmio.h"
#include "user_memory.h"

/* The screen surface (800x600x4 = 1,920,000 bytes) lives at offset zero and
   is owned by the framebuffer device. Surfaces and the text staging area come
   from the rest of the 32 MiB aperture. */
#define GFX_ARENA_START 0x00200000U
#define GFX_ARENA_END 0x02000000U
#define GFX_STAGING_OFFSET 0x001FF000U
#define GFX_STAGING_SIZE 0x00001000U

static unsigned int arena_next;
static int arena_initialized;

static volatile struct graphics_registers *graphics(void) {
    return (volatile struct graphics_registers *)GRAPHICS_BASE;
}

static int user_byte(unsigned int address, unsigned char *out) {
    const mem_phys_t physical = user_virt_to_phys((mem_virt_t)address);
    if (physical == MEM_PHYS_NONE) {
        return 0;
    }
    *out = *(const unsigned char *)mem_phys_to_virt(physical);
    return 1;
}

/* Byte-wise so an unaligned block or a block spanning two pages still reads. */
static int user_word(unsigned int address, unsigned int *out) {
    unsigned int value = 0;
    int index = 0;
    while (index < 4) {
        unsigned char byte = 0;
        if (user_byte(address + (unsigned int)index, &byte) == 0) {
            return 0;
        }
        value = value | ((unsigned int)byte << (index * 8));
        ++index;
    }
    *out = value;
    return 1;
}

unsigned int gfx_alloc(unsigned int width, unsigned int height) {
    if (arena_initialized == 0) {
        arena_next = GFX_ARENA_START;
        arena_initialized = 1;
    }
    if (width == 0 || height == 0 || width > 1920U || height > 1080U) {
        return 0;
    }
    const unsigned int size = ((width * 4U) * height + 3U) & ~3U;
    if (arena_next + size > GFX_ARENA_END || arena_next + size < arena_next) {
        return 0;
    }
    const unsigned int offset = arena_next;
    arena_next += size;
    return offset;
}

unsigned int gfx_stage_text(const char *user_text, int length) {
    if (length <= 0 || (unsigned int)length > GFX_STAGING_SIZE) {
        return 0;
    }
    volatile unsigned char *staging =
        (volatile unsigned char *)(FRAMEBUFFER_BASE + GFX_STAGING_OFFSET);
    unsigned int address = (unsigned int)user_text;
    int index = 0;
    while (index < length) {
        unsigned char byte = 0;
        if (!user_byte(address + (unsigned int)index, &byte)) {
            return 0;
        }
        staging[index] = byte;
        ++index;
    }
    return GFX_STAGING_OFFSET;
}

static void submit(unsigned int op, const unsigned int *args, int count) {
    volatile struct graphics_registers *device = graphics();
    int index = 0;
    while (index < count && index < 8) {
        device->args[index] = args[index];
        ++index;
    }
    while (index < 8) {
        device->args[index] = 0;
        ++index;
    }
    device->command = op;
    device->doorbell = 1;
}

int gfx_submit_from_user(unsigned int address) {
    struct gfx_command command;
    unsigned int *words = (unsigned int *)&command;
    int index = 0;
    while (index < 15) {
        unsigned int word = 0;
        if (user_word(address + (unsigned int)(index * 4), &word) == 0) {
            return -ERRNO_EFAULT;
        }
        words[index] = word;
        ++index;
    }

    /* Offset zero is the screen, so the surface is always selected explicitly. */
    const unsigned int surface[8] = {
        command.surface, command.surface_width, command.surface_height,
        command.surface_stride, 0, 0, 0, 0};
    submit(GRAPHICS_OP_SET_SURFACE, surface, 4);
    if (command.clip_width > 0 && command.clip_height > 0) {
        const unsigned int clip[8] = {
            (unsigned int)command.clip_x, (unsigned int)command.clip_y,
            (unsigned int)command.clip_width, (unsigned int)command.clip_height,
            0, 0, 0, 0};
        submit(GRAPHICS_OP_SET_CLIP, clip, 4);
    }

    unsigned int args[6];
    index = 0;
    while (index < 6) {
        args[index] = command.args[index];
        ++index;
    }
    if (command.op == GRAPHICS_OP_DRAW_TEXT) {
        const unsigned int offset =
            gfx_stage_text((const char *)args[2], (int)args[3]);
        if (offset == 0) {
            return -ERRNO_EFAULT;
        }
        args[2] = offset;
    }
    if (command.op == GRAPHICS_OP_FLIP) {
        submit(GRAPHICS_OP_FLIP, args, 0);
        return 0;
    }
    submit(command.op, args, 6);
    return 0;
}
