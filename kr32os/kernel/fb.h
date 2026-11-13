#pragma once

struct fb_info {
    unsigned int width;
    unsigned int height;
    unsigned int stride;
};

/* 0 when the display answers with its ID word, negative otherwise. */
int fb_probe(void);

/* Sets width x height at offset 0, enables the display, and flips. */
int fb_mode(int width, int height);

int fb_info_get(struct fb_info *out);

unsigned int *fb_back(void);
unsigned int *fb_front(void);

/* Commits the back surface as the displayed frame. */
int fb_present(void);
