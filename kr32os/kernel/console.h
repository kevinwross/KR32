#pragma once

/* Probes the display, sets 800x600, and clears the grid. Returns 0 when the
   framebuffer console is live; serial-only otherwise. */
int console_init(void);

void console_putc(char character);
void console_write(const char *text);
void console_write_uint(unsigned int value);
int console_getc(void);
void console_clear(void);

/* Redraws dirty cells, draws the cursor, and flips. */
void console_present(void);
