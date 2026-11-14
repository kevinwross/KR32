#pragma once

/* Kernel-side access to the graphics command device and its VRAM arena. */

struct gfx_command {
    unsigned int op;
    unsigned int surface;        /* VRAM offset; 0 selects the screen back buffer */
    unsigned int surface_width;
    unsigned int surface_height;
    unsigned int surface_stride; /* bytes per row */
    int clip_x;
    int clip_y;
    int clip_width;
    int clip_height;             /* zero or negative selects the whole surface */
    unsigned int args[6];
};

/* Allocates a width x height surface from the VRAM arena and returns its
   offset, or zero when it does not fit. Buffers are never reused. */
unsigned int gfx_alloc(unsigned int width, unsigned int height);

/* Copies the 60-byte command block from user memory, validates it, and
   submits it to the graphics device. Returns 0 or a negative errno. */
int gfx_submit_from_user(unsigned int address);

/* Copies a DRAW_TEXT string into the staging region and returns its VRAM
   offset, or zero when the pointer does not validate. */
unsigned int gfx_stage_text(const char *user_text, int length);
