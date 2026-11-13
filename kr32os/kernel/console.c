#include "console.h"

#include "fb.h"
#include "mmio.h"

extern const unsigned char kFont8x16[1520];

#define CONSOLE_COLUMNS 100
#define CONSOLE_ROWS 37
#define CONSOLE_SCROLL_LINES 256
#define CONSOLE_CELL_WIDTH 8
#define CONSOLE_CELL_HEIGHT 16

#define COLOR_FOREGROUND 7
#define COLOR_BACKGROUND 0

/* The kernel C front end takes literal array bounds only, so these sizes are
   repeated: grid is 37x100, scrollback is 256x100. Keep them in step with the
   macros below. */
static char grid_chars[37][100];
static unsigned char grid_attributes[37][100];
static char scroll_chars[256][100];
static unsigned char scroll_attributes[256][100];

static int display_ready;
static int columns = 100;
static int rows = 37;
static unsigned int *surface;
static int surface_stride;
static int surface_height;
static int cursor_x;
static int cursor_y;
static int cursor_drawn_x;
static int cursor_drawn_y;
static int cursor_drawn;
static int dirty_left;
static int dirty_top;
static int dirty_right;
static int dirty_bottom;
static unsigned char pen_foreground = 7;
static unsigned char pen_background = 0;
static int scroll_head;
static int scroll_count;

static const unsigned int palette[16] = {
    0x00101820U, 0x00A03030U, 0x0030A030U, 0x00A0A030U,
    0x003040A0U, 0x00A030A0U, 0x0030A0A0U, 0x00E6EDF3U,
    0x00506070U, 0x00F06060U, 0x0060F060U, 0x00F0F060U,
    0x006080F0U, 0x00F060F0U, 0x0060F0F0U, 0x00FFFFFFU,
};

static void clear_dirty(void) {
    dirty_left = columns;
    dirty_top = rows;
    dirty_right = -1;
    dirty_bottom = -1;
}

static void mark_dirty(int column, int row) {
    if (column < 0 || column >= columns || row < 0 || row >= rows) {
        return;
    }
    if (column < dirty_left) dirty_left = column;
    if (column > dirty_right) dirty_right = column;
    if (row < dirty_top) dirty_top = row;
    if (row > dirty_bottom) dirty_bottom = row;
}

static void mark_all_dirty(void) {
    dirty_left = 0;
    dirty_top = 0;
    dirty_right = columns - 1;
    dirty_bottom = rows - 1;
}

static void set_cell(int column, int row, char character, unsigned char attribute) {
    grid_chars[row][column] = character;
    grid_attributes[row][column] = attribute;
    mark_dirty(column, row);
}

static void push_line(int row) {
    int column = 0;
    while (column < columns) {
        scroll_chars[scroll_head][column] = grid_chars[row][column];
        scroll_attributes[scroll_head][column] = grid_attributes[row][column];
        ++column;
    }
    ++scroll_head;
    if (scroll_head >= CONSOLE_SCROLL_LINES) {
        scroll_head = 0;
    }
    if (scroll_count < CONSOLE_SCROLL_LINES) {
        ++scroll_count;
    }
}

static void clear_row(int row) {
    int column = 0;
    while (column < columns) {
        set_cell(column, row, ' ', (unsigned char)(pen_foreground | (pen_background << 4)));
        ++column;
    }
}

static void scroll_up(void) {
    int row = 1;
    while (row < rows) {
        int column = 0;
        while (column < columns) {
            grid_chars[row - 1][column] = grid_chars[row][column];
            grid_attributes[row - 1][column] = grid_attributes[row][column];
            ++column;
        }
        ++row;
    }
    clear_row(rows - 1);
    mark_all_dirty();
    cursor_drawn = 0;
}

static void draw_cell(int column, int row, int inverse) {
    unsigned int attribute = grid_attributes[row][column];
    unsigned int foreground = palette[attribute & 0x0FU];
    unsigned int background = palette[(attribute >> 4) & 0x0FU];
    unsigned char character = (unsigned char)grid_chars[row][column];
    int glyph = 0;
    int pixel_x = column * CONSOLE_CELL_WIDTH;
    int pixel_y = row * CONSOLE_CELL_HEIGHT;
    int line = 0;

    if (inverse) {
        unsigned int swap = foreground;
        foreground = background;
        background = swap;
    }
    if (character >= 0x20 && character <= 0x7E) {
        glyph = character - 0x20;
    }
    while (line < CONSOLE_CELL_HEIGHT) {
        unsigned int bits = kFont8x16[glyph * 16 + line];
        unsigned int *destination =
            surface + (pixel_y + line) * surface_stride + pixel_x;
        int pixel = 0;
        while (pixel < CONSOLE_CELL_WIDTH) {
            destination[pixel] = (bits & (0x80U >> pixel)) ? foreground : background;
            ++pixel;
        }
        ++line;
    }
}

static void draw_dirty(void) {
    int row = dirty_top;
    if (dirty_right < dirty_left || dirty_bottom < dirty_top) {
        return;
    }
    while (row <= dirty_bottom) {
        int column = dirty_left;
        while (column <= dirty_right) {
            draw_cell(column, row, 0);
            ++column;
        }
        ++row;
    }
}

static void fill_surface(unsigned int color) {
    unsigned int *cursor = surface;
    int remaining = surface_stride * surface_height;
    while (remaining >= 4) {
        cursor[0] = color;
        cursor[1] = color;
        cursor[2] = color;
        cursor[3] = color;
        cursor += 4;
        remaining -= 4;
    }
    while (remaining > 0) {
        *cursor = color;
        ++cursor;
        --remaining;
    }
}

void console_present(void) {
    if (!display_ready) {
        return;
    }
    if (cursor_drawn) {
        mark_dirty(cursor_drawn_x, cursor_drawn_y);
    }
    draw_dirty();
    draw_cell(cursor_x, cursor_y, 1);
    cursor_drawn_x = cursor_x;
    cursor_drawn_y = cursor_y;
    cursor_drawn = 1;
    clear_dirty();
    (void)fb_present();
}

int console_init(void) {
    struct fb_info info;
    int row;
    int column;

    if (fb_probe() != 0) {
        return -1;
    }
    if (fb_mode(800, 600) != 0) {
        return -1;
    }
    if (fb_info_get(&info) != 0) {
        return -1;
    }
    columns = (int)(info.width / CONSOLE_CELL_WIDTH);
    rows = (int)(info.height / CONSOLE_CELL_HEIGHT);
    if (columns > CONSOLE_COLUMNS) columns = CONSOLE_COLUMNS;
    if (rows > CONSOLE_ROWS) rows = CONSOLE_ROWS;
    if (columns < 1 || rows < 1) {
        return -1;
    }
    surface = fb_back();
    surface_stride = (int)(info.stride / 4U);
    surface_height = (int)info.height;
    cursor_x = 0;
    cursor_y = 0;
    scroll_head = 0;
    scroll_count = 0;
    display_ready = 1;

    row = 0;
    while (row < rows) {
        column = 0;
        while (column < columns) {
            grid_chars[row][column] = ' ';
            grid_attributes[row][column] =
                (unsigned char)(pen_foreground | (pen_background << 4));
            ++column;
        }
        ++row;
    }
    fill_surface(palette[pen_background]);
    clear_dirty();
    cursor_drawn = 0;
    console_present();
    return 0;
}

static void newline(void) {
    if (cursor_drawn) {
        mark_dirty(cursor_drawn_x, cursor_drawn_y);
    }
    push_line(cursor_y);
    cursor_x = 0;
    ++cursor_y;
    if (cursor_y >= rows) {
        cursor_y = rows - 1;
        scroll_up();
    }
    mark_dirty(cursor_x, cursor_y);
}

void console_putc(char character) {
    volatile struct serial_registers *serial =
        (volatile struct serial_registers *)SERIAL_BASE;

    while ((serial->status & SERIAL_TX_READY) == 0) {
    }
    serial->data = (unsigned int)character;

    if (!display_ready) {
        return;
    }
    if (character == '\n') {
        newline();
        console_present();
        return;
    }
    if (character == '\r') {
        mark_dirty(cursor_x, cursor_y);
        cursor_x = 0;
        return;
    }
    if (character == '\b') {
        if (cursor_x > 0) {
            --cursor_x;
        } else if (cursor_y > 0) {
            --cursor_y;
            cursor_x = columns - 1;
        }
        mark_dirty(cursor_x, cursor_y);
        return;
    }
    if (character == '\f') {
        console_clear();
        return;
    }
    if (character == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= columns) {
            cursor_x = 0;
            ++cursor_y;
            if (cursor_y >= rows) {
                cursor_y = rows - 1;
                scroll_up();
            }
        }
        mark_dirty(cursor_x, cursor_y);
        return;
    }
    if (character < 32 || character > 126) {
        return;
    }

    set_cell(cursor_x, cursor_y, character,
             (unsigned char)(pen_foreground | (pen_background << 4)));
    ++cursor_x;
    if (cursor_x >= columns) {
        newline();
    }
}

void console_write(const char *text) {
    while (*text != '\0') {
        console_putc(*text);
        ++text;
    }
}

void console_write_uint(unsigned int value) {
    char buffer[16];
    int length = 0;
    if (value == 0) {
        console_putc('0');
        return;
    }
    while (value > 0 && length < (int)sizeof(buffer)) {
        buffer[length] = (char)('0' + (value % 10));
        value /= 10;
        ++length;
    }
    while (length > 0) {
        --length;
        console_putc(buffer[length]);
    }
}

int console_getc(void) {
    volatile struct keyboard_registers *keyboard =
        (volatile struct keyboard_registers *)KEYBOARD_BASE;
    volatile struct serial_registers *serial =
        (volatile struct serial_registers *)SERIAL_BASE;

    if ((keyboard->status & KEYBOARD_STATUS_AVAILABLE) != 0) {
        unsigned int event = keyboard->data;
        unsigned int type = (event >> 24) & 0xFFU;
        unsigned int ascii = event & 0xFFU;
        unsigned int scancode = (event >> 16) & 0xFFU;
        if (type != 0) {
            return -1;
        }
        if (ascii != 0) {
            return (int)ascii;
        }
        if (scancode == 0x80U) {
            return '\n';
        }
        if (scancode == 0x82U) {
            return 0x7F;
        }
        return -1;
    }
    if ((serial->status & SERIAL_RX_READY) == 0) {
        return -1;
    }
    return (int)(serial->data & SERIAL_DATA_MASK);
}

void console_clear(void) {
    volatile struct serial_registers *serial =
        (volatile struct serial_registers *)SERIAL_BASE;
    int row;
    int column;

    while ((serial->status & SERIAL_TX_READY) == 0) {
    }
    serial->data = (unsigned int)'\f';

    if (!display_ready) {
        return;
    }
    row = 0;
    while (row < rows) {
        column = 0;
        while (column < columns) {
            grid_chars[row][column] = ' ';
            grid_attributes[row][column] = 0;
            ++column;
        }
        ++row;
    }
    cursor_x = 0;
    cursor_y = 0;
    cursor_drawn = 0;
    clear_dirty();
    fill_surface(palette[pen_background]);
    console_present();
}
