#include "console.h"

#define SERIAL_RX_READY 1
#define SERIAL_TX_READY 2

void console_putc(char character) {
    unsigned int *status = (unsigned int *)0xFFF05004;
    unsigned int *data = (unsigned int *)0xFFF05000;
    while ((*status & SERIAL_TX_READY) == 0) {
    }
    *data = (unsigned int)character;
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
    unsigned int *status = (unsigned int *)0xFFF05004;
    unsigned int *data = (unsigned int *)0xFFF05000;
    if ((*status & SERIAL_RX_READY) == 0) {
        return -1;
    }
    return (int)(*data & 0xFF);
}

void console_clear(void) {
    console_putc('\f');
}
