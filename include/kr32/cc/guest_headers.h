#pragma once

#include <string_view>

// Guest-facing declarations for the five <kr32/*.h> headers. Both the C and
// the C++ front ends inject these bodies when a program includes the header;
// the C++ front end wraps each in an extern "C" block. Keep the declarations
// in step with docs/graphics.md.

namespace kr32::cc {

inline constexpr std::string_view kGuestFbHeader =
    "typedef struct fb_info {\n"
    "    unsigned int width;\n"
    "    unsigned int height;\n"
    "    unsigned int stride_bytes;\n"
    "    unsigned int format;\n"
    "    unsigned int frame;\n"
    "} fb_info;\n"
    "int fb_probe(void);\n"
    "int fb_mode(int width, int height);\n"
    "int fb_info_get(fb_info *out);\n"
    "unsigned int *fb_back(void);\n"
    "unsigned int *fb_front(void);\n"
    "int fb_present(void);\n"
    "int fb_present_rect(int x, int y, int width, int height);\n";

inline constexpr std::string_view kGuestDisplayHeader =
    "typedef struct display_surface {\n"
    "    unsigned int offset;\n"
    "    int width;\n"
    "    int height;\n"
    "    int stride;\n"
    "} display_surface;\n"
    "int sys_display_present(void);\n";

inline constexpr std::string_view kGuestGfxHeader =
    "typedef struct gfx_surface {\n"
    "    unsigned int offset;\n"
    "    int width;\n"
    "    int height;\n"
    "    int stride;\n"
    "    int clip_x;\n"
    "    int clip_y;\n"
    "    int clip_w;\n"
    "    int clip_h;\n"
    "} gfx_surface;\n"
    "gfx_surface *gfx_create(int width, int height);\n"
    "void gfx_destroy(gfx_surface *surface);\n"
    "int gfx_present(gfx_surface *surface, int x, int y, int w, int h);\n"
    "void gfx_clear(gfx_surface *surface, unsigned int color);\n"
    "void gfx_pixel(gfx_surface *surface, int x, int y, unsigned int color);\n"
    "void gfx_hline(gfx_surface *surface, int x, int y, int width, unsigned int color);\n"
    "void gfx_vline(gfx_surface *surface, int x, int y, int height, unsigned int color);\n"
    "void gfx_fill_rect(gfx_surface *surface, int x, int y, int width, int height, unsigned int color);\n"
    "void gfx_frame_rect(gfx_surface *surface, int x, int y, int width, int height, unsigned int color);\n"
    "void gfx_line(gfx_surface *surface, int x0, int y0, int x1, int y1, unsigned int color);\n"
    "void gfx_set_clip(gfx_surface *surface, int x, int y, int width, int height);\n"
    "void gfx_reset_clip(gfx_surface *surface);\n"
    "int gfx_font_width(void);\n"
    "int gfx_font_height(void);\n"
    "int gfx_text_width(const char *text);\n"
    "int gfx_text(gfx_surface *surface, int x, int y, const char *text, unsigned int fg, unsigned int bg);\n";

inline constexpr std::string_view kGuestConHeader =
    "void con_putc(char character);\n"
    "void con_puts(const char *text);\n"
    "void con_write(const char *text, int count);\n"
    "void con_clear(void);\n"
    "void con_set_colors(int fg_index, int bg_index);\n"
    "void con_set_cursor(int visible);\n"
    "void con_present(void);\n";

inline constexpr std::string_view kGuestInputHeader =
    "typedef struct kr32_event {\n"
    "    unsigned char ascii;\n"
    "    unsigned char modifiers;\n"
    "    unsigned char scancode;\n"
    "    unsigned char type;\n"
    "} kr32_event;\n"
    "int input_pending(void);\n"
    "int input_poll(kr32_event *event);\n";

} // namespace kr32::cc
