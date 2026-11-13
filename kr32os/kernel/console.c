#include "console.h"

#include "fb.h"
#include "mmio.h"

/* The console is the host terminal (page 8): the guest writes characters and
   the host owns the grid, font, palette, scrollback, and rendering. */

static int display_ready;

int console_init(void) {
    struct fb_info info;
    volatile struct terminal_registers *registers =
        (volatile struct terminal_registers *)TERMINAL_BASE;

    if (fb_probe() != 0) {
        return -1;
    }
    if (fb_mode(800, 600) != 0) {
        return -1;
    }
    if (fb_info_get(&info) != 0) {
        return -1;
    }
    if (registers->id != TERMINAL_ID_VALUE) {
        return -1;
    }
    registers->command = TERMINAL_COMMAND_RESET;
    registers->color = 0x07U;
    display_ready = 1;
    (void)fb_present();
    return 0;
}

void console_putc(char character) {
    volatile struct serial_registers *serial =
        (volatile struct serial_registers *)SERIAL_BASE;
    volatile struct terminal_registers *registers =
        (volatile struct terminal_registers *)TERMINAL_BASE;

    while ((serial->status & SERIAL_TX_READY) == 0) {
    }
    serial->data = (unsigned int)character;

    if (!display_ready) {
        return;
    }
    registers->data = (unsigned int)(unsigned char)character;
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
    volatile struct terminal_registers *registers =
        (volatile struct terminal_registers *)TERMINAL_BASE;

    while ((serial->status & SERIAL_TX_READY) == 0) {
    }
    serial->data = (unsigned int)'\f';

    if (!display_ready) {
        return;
    }
    registers->command = TERMINAL_COMMAND_CLEAR;
}

void console_present(void) {
    if (!display_ready) {
        return;
    }
    (void)fb_present();
}

void console_set_colors(int fg_index, int bg_index) {
    volatile struct terminal_registers *registers =
        (volatile struct terminal_registers *)TERMINAL_BASE;

    if (fg_index < 0 || fg_index > 15 || bg_index < 0 || bg_index > 15) {
        return;
    }
    if (!display_ready) {
        return;
    }
    registers->color =
        (unsigned int)((fg_index & 0x0F) | ((bg_index & 0x0F) << 4));
}
