#pragma once

// Guest 2D drawing and text declarations. Carries the same declarations as
// kGuestGfxHeader in include/kr32/cc/guest_headers.h; see fb.h for why both
// copies exist.

typedef struct gfx_surface {
    unsigned int offset;
    int width;
    int height;
    int stride;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
} gfx_surface;
gfx_surface *gfx_create(int width, int height);
void gfx_destroy(gfx_surface *surface);
int gfx_present(gfx_surface *surface, int x, int y, int w, int h);
void gfx_clear(gfx_surface *surface, unsigned int color);
void gfx_pixel(gfx_surface *surface, int x, int y, unsigned int color);
void gfx_hline(gfx_surface *surface, int x, int y, int width, unsigned int color);
void gfx_vline(gfx_surface *surface, int x, int y, int height, unsigned int color);
void gfx_fill_rect(gfx_surface *surface, int x, int y, int width, int height, unsigned int color);
void gfx_frame_rect(gfx_surface *surface, int x, int y, int width, int height, unsigned int color);
void gfx_line(gfx_surface *surface, int x0, int y0, int x1, int y1, unsigned int color);
void gfx_blit(gfx_surface *dst, int dx, int dy, gfx_surface *src, int sx, int sy, int w, int h);
void gfx_set_clip(gfx_surface *surface, int x, int y, int width, int height);
void gfx_reset_clip(gfx_surface *surface);
int gfx_font_width(void);
int gfx_font_height(void);
int gfx_text_width(const char *text);
int gfx_text(gfx_surface *surface, int x, int y, const char *text, unsigned int fg, unsigned int bg);
