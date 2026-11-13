#pragma once

/* Probes the display, sets 800x600, and resets the host terminal. Returns 0
   when the terminal is live; serial-only otherwise. */
int console_init(void);

void console_putc(char character);
void console_write(const char *text);
void console_write_uint(unsigned int value);
int console_getc(void);
void console_clear(void);

/* Sets the pen for subsequent writes. Indices 0..15; out of range is ignored. */
void console_set_colors(int fg_index, int bg_index);

/* Publishes the host-rendered frame. */
void console_present(void);
