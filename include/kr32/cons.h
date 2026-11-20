#pragma once

// Guest console declarations. The built-in C and C++ front ends embed the same
// declarations from include/kr32/cc/guest_headers.h; a toolchain that reads
// include paths (krc) reads this file. The former name, <kr32/con.h>, is a
// reserved DOS device name and could not be a tracked file.

void con_putc(char character);
void con_puts(const char *text);
void con_write(const char *text, int count);
void con_clear(void);
void con_set_colors(int fg_index, int bg_index);
void con_set_cursor(int visible);
void con_present(void);
void con_hide(int hidden);
