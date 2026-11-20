#pragma once

// Guest framebuffer declarations. The built-in C and C++ front ends embed this
// same body from include/kr32/cc/guest_headers.h; a toolchain that reads
// include paths (krc) reads this file. Keep the two copies identical;
// tests/unit/test_guest_headers.cpp enforces it.

typedef struct fb_info {
    unsigned int width;
    unsigned int height;
    unsigned int stride_bytes;
    unsigned int format;
    unsigned int frame;
} fb_info;
int fb_probe(void);
int fb_mode(int width, int height);
int fb_info_get(fb_info *out);
unsigned int *fb_back(void);
unsigned int *fb_front(void);
int fb_present(void);
int fb_present_rect(int x, int y, int width, int height);
