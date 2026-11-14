/* User-side graphics, console, and input wrappers. Every call becomes one
   syscall; the host does the drawing. Linked into programs that run under
   KR32OS. */

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

typedef struct gfx_command {
    unsigned int op;
    unsigned int surface;
    unsigned int surface_width;
    unsigned int surface_height;
    unsigned int surface_stride;
    int clip_x;
    int clip_y;
    int clip_width;
    int clip_height;
    unsigned int args[6];
} gfx_command;

typedef struct kr32_event_user {
    unsigned char ascii;
    unsigned char modifiers;
    unsigned char scancode;
    unsigned char type;
} kr32_event;

#define GFX_OP_SET_CLIP 1U
#define GFX_OP_FILL_RECT 3U
#define GFX_OP_COPY_RECT 4U
#define GFX_OP_DRAW_LINE 5U
#define GFX_OP_DRAW_TEXT 6U
#define GFX_OP_FLIP 7U
#define GFX_OP_SET_SURFACE 8U
#define GFX_OP_COPY_RECT_STRIDED 9U

#define GFX_SCREEN_OFFSET 0U
#define GFX_SCREEN_WIDTH 800
#define GFX_SCREEN_HEIGHT 600

int sys_gfx(struct gfx_command *command);
unsigned int sys_gfx_alloc(unsigned int width, unsigned int height);
int sys_con(int op, int value);
int sys_input_poll(void *event);
int sys_input_pending(void);
int sys_write(int descriptor, const void *buffer, int length);

static struct gfx_surface surface_pool[8];
static unsigned char surface_used[8];

static int text_length(const char *text) {
    int length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static int submit(struct gfx_surface *surface, unsigned int op,
                  unsigned int a0, unsigned int a1, unsigned int a2,
                  unsigned int a3, unsigned int a4, unsigned int a5) {
    struct gfx_command command;
    command.op = op;
    command.surface = surface != 0 ? surface->offset : 0;
    command.surface_width = surface != 0 ? (unsigned int)surface->width : 0;
    command.surface_height = surface != 0 ? (unsigned int)surface->height : 0;
    command.surface_stride = surface != 0 ? (unsigned int)surface->stride : 0;
    command.clip_x = surface != 0 ? surface->clip_x : 0;
    command.clip_y = surface != 0 ? surface->clip_y : 0;
    command.clip_width = surface != 0 ? surface->clip_w : 0;
    command.clip_height = surface != 0 ? surface->clip_h : 0;
    command.args[0] = a0;
    command.args[1] = a1;
    command.args[2] = a2;
    command.args[3] = a3;
    command.args[4] = a4;
    command.args[5] = a5;
    return sys_gfx(&command);
}

gfx_surface *gfx_create(int width, int height) {
    if (width <= 0 || height <= 0 || width > 1920 || height > 1080) {
        return 0;
    }
    unsigned int offset = sys_gfx_alloc((unsigned int)width, (unsigned int)height);
    if (offset == 0) {
        return 0;
    }
    int slot = 0;
    while (slot < 8 && surface_used[slot] != 0) {
        ++slot;
    }
    if (slot == 8) {
        return 0;
    }
    surface_used[slot] = 1;
    gfx_surface *surface = surface_pool;
    surface += slot;
    surface->offset = offset;
    surface->width = width;
    surface->height = height;
    surface->stride = width * 4;
    surface->clip_x = 0;
    surface->clip_y = 0;
    surface->clip_w = width;
    surface->clip_h = height;
    return surface;
}

void gfx_destroy(gfx_surface *surface) {
    if (surface == 0) {
        return;
    }
    int slot = 0;
    while (slot < 8) {
        gfx_surface *candidate = surface_pool + slot;
        if (candidate == surface) {
            break;
        }
        ++slot;
    }
    if (slot < 8) {
        surface_used[slot] = 0;
    }
}

int gfx_present(gfx_surface *surface, int x, int y, int w, int h) {
    if (surface == 0) {
        return -1;
    }
    if (surface->offset == GFX_SCREEN_OFFSET) {
        return submit(surface, GFX_OP_FLIP, 0, 0, 0, 0, 0, 0);
    }
    if (w <= 0 || h <= 0) {
        return -1;
    }
    /* One strided copy puts the rectangle on the screen, then a flip. */
    gfx_surface screen;
    screen.offset = GFX_SCREEN_OFFSET;
    screen.width = GFX_SCREEN_WIDTH;
    screen.height = GFX_SCREEN_HEIGHT;
    screen.stride = GFX_SCREEN_WIDTH * 4;
    screen.clip_x = 0;
    screen.clip_y = 0;
    screen.clip_w = GFX_SCREEN_WIDTH;
    screen.clip_h = GFX_SCREEN_HEIGHT;
    const unsigned int source = surface->offset +
                                (unsigned int)(y * surface->stride) +
                                (unsigned int)(x * 4);
    if (submit(&screen, GFX_OP_COPY_RECT_STRIDED, source,
               (unsigned int)surface->stride, (unsigned int)x, (unsigned int)y,
               (unsigned int)w, (unsigned int)h) != 0) {
        return -1;
    }
    return submit(&screen, GFX_OP_FLIP, 0, 0, 0, 0, 0, 0);
}

void gfx_clear(gfx_surface *surface, unsigned int color) {
    if (surface == 0) {
        return;
    }
    (void)submit(surface, GFX_OP_FILL_RECT, 0, 0, (unsigned int)surface->width,
                 (unsigned int)surface->height, color, 0);
}

void gfx_pixel(gfx_surface *surface, int x, int y, unsigned int color) {
    (void)submit(surface, GFX_OP_FILL_RECT, (unsigned int)x, (unsigned int)y, 1, 1,
                 color, 0);
}

void gfx_hline(gfx_surface *surface, int x, int y, int width,
               unsigned int color) {
    if (width <= 0) {
        return;
    }
    (void)submit(surface, GFX_OP_FILL_RECT, (unsigned int)x, (unsigned int)y,
                 (unsigned int)width, 1, color, 0);
}

void gfx_vline(gfx_surface *surface, int x, int y, int height,
               unsigned int color) {
    if (height <= 0) {
        return;
    }
    (void)submit(surface, GFX_OP_FILL_RECT, (unsigned int)x, (unsigned int)y, 1,
                 (unsigned int)height, color, 0);
}

void gfx_fill_rect(gfx_surface *surface, int x, int y, int width, int height,
                   unsigned int color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    (void)submit(surface, GFX_OP_FILL_RECT, (unsigned int)x, (unsigned int)y,
                 (unsigned int)width, (unsigned int)height, color, 0);
}

void gfx_frame_rect(gfx_surface *surface, int x, int y, int width, int height,
                    unsigned int color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    gfx_hline(surface, x, y, width, color);
    gfx_hline(surface, x, y + height - 1, width, color);
    gfx_vline(surface, x, y, height, color);
    gfx_vline(surface, x + width - 1, y, height, color);
}

void gfx_line(gfx_surface *surface, int x0, int y0, int x1, int y1,
              unsigned int color) {
    (void)submit(surface, GFX_OP_DRAW_LINE, (unsigned int)x0, (unsigned int)y0,
                 (unsigned int)x1, (unsigned int)y1, color, 0);
}

void gfx_blit(gfx_surface *dst, int dx, int dy, gfx_surface *src, int sx, int sy,
              int w, int h) {
    if (dst == 0 || src == 0 || w <= 0 || h <= 0) {
        return;
    }
    const unsigned int source = src->offset +
                                (unsigned int)(sy * src->stride) +
                                (unsigned int)(sx * 4);
    (void)submit(dst, GFX_OP_COPY_RECT_STRIDED, source,
                 (unsigned int)src->stride, (unsigned int)dx, (unsigned int)dy,
                 (unsigned int)w, (unsigned int)h);
}

void gfx_set_clip(gfx_surface *surface, int x, int y, int width, int height) {
    if (surface == 0) {
        return;
    }
    surface->clip_x = x;
    surface->clip_y = y;
    surface->clip_w = width;
    surface->clip_h = height;
}

void gfx_reset_clip(gfx_surface *surface) {
    if (surface == 0) {
        return;
    }
    surface->clip_x = 0;
    surface->clip_y = 0;
    surface->clip_w = surface->width;
    surface->clip_h = surface->height;
}

int gfx_font_width(void) {
    return 10;
}

int gfx_font_height(void) {
    return 20;
}

int gfx_text_width(const char *text) {
    return text_length(text) * 10;
}

int gfx_text(gfx_surface *surface, int x, int y, const char *text,
             unsigned int fg, unsigned int bg) {
    const int length = text_length(text);
    if (length == 0) {
        return 0;
    }
    if (submit(surface, GFX_OP_DRAW_TEXT, (unsigned int)x, (unsigned int)y,
               (unsigned int)text, (unsigned int)length, fg, bg) != 0) {
        return 0;
    }
    return length * 10;
}

void con_putc(char character) {
    (void)sys_write(1, &character, 1);
}

void con_puts(const char *text) {
    int index = 0;
    while (text[index] != '\0') {
        con_putc(text[index]);
        ++index;
    }
}

void con_write(const char *text, int count) {
    int index = 0;
    while (index < count) {
        con_putc(text[index]);
        ++index;
    }
}

void con_clear(void) {
    (void)sys_con(3, 0);
}

void con_set_colors(int fg_index, int bg_index) {
    if (fg_index < 0 || fg_index > 15 || bg_index < 0 || bg_index > 15) {
        return;
    }
    (void)sys_con(1, (fg_index & 0x0F) | ((bg_index & 0x0F) << 4));
}

void con_set_cursor(int visible) {
    (void)sys_con(2, visible != 0);
}

void con_hide(int hidden) {
    (void)sys_con(5, hidden != 0);
}

void con_present(void) {
    (void)sys_con(4, 0);
}

int input_pending(void) {
    return sys_input_pending();
}

int input_poll(kr32_event *event) {
    if (event == 0) {
        return 0;
    }
    return sys_input_poll(event);
}
