#include "fb.h"

#include "mmio.h"

/* The kernel C front end does not accept a cast in a global initializer or a
   cast immediately followed by ->, so each entry point binds the control page
   to a local pointer first. */
int fb_probe(void) {
    volatile struct framebuffer_registers *registers =
        (volatile struct framebuffer_registers *)0xFFF04000U;
    if (registers->id != FRAMEBUFFER_ID_VALUE) {
        return -1;
    }
    return 0;
}

int fb_mode(int width, int height) {
    volatile struct framebuffer_registers *registers =
        (volatile struct framebuffer_registers *)0xFFF04000U;
    if (width <= 0 || height <= 0) {
        return -1;
    }
    registers->width = (unsigned int)width;
    registers->height = (unsigned int)height;
    registers->format = 1U;
    registers->front = 0U;
    registers->back = 0U;
    registers->control = 1U;
    registers->flip = 1U;
    if ((registers->status & FRAMEBUFFER_STATUS_FLIP_ERROR) != 0) {
        return -1;
    }
    return 0;
}

int fb_info_get(struct fb_info *out) {
    volatile struct framebuffer_registers *registers =
        (volatile struct framebuffer_registers *)0xFFF04000U;
    if (out == 0) {
        return -1;
    }
    if ((registers->status & FRAMEBUFFER_STATUS_FLIP_ERROR) != 0) {
        return -1;
    }
    out->width = registers->width;
    out->height = registers->height;
    out->stride = registers->stride;
    return 0;
}

unsigned int *fb_back(void) {
    volatile struct framebuffer_registers *registers =
        (volatile struct framebuffer_registers *)0xFFF04000U;
    return (unsigned int *)(FRAMEBUFFER_BASE + registers->back);
}

unsigned int *fb_front(void) {
    volatile struct framebuffer_registers *registers =
        (volatile struct framebuffer_registers *)0xFFF04000U;
    return (unsigned int *)(FRAMEBUFFER_BASE + registers->front);
}

int fb_present(void) {
    volatile struct framebuffer_registers *registers =
        (volatile struct framebuffer_registers *)0xFFF04000U;
    registers->flip = 1U;
    if ((registers->status & FRAMEBUFFER_STATUS_FLIP_ERROR) != 0) {
        return -1;
    }
    return 0;
}
