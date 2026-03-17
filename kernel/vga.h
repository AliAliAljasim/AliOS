#pragma once

#include <stdint.h>

/* VGA color indices (foreground and background) */
#define VGA_BLACK         0
#define VGA_BLUE          1
#define VGA_GREEN         2
#define VGA_CYAN          3
#define VGA_RED           4
#define VGA_MAGENTA       5
#define VGA_BROWN         6
#define VGA_LIGHT_GRAY    7
#define VGA_DARK_GRAY     8
#define VGA_LIGHT_BLUE    9
#define VGA_LIGHT_GREEN   10
#define VGA_LIGHT_CYAN    11
#define VGA_LIGHT_RED     12
#define VGA_PINK          13
#define VGA_YELLOW        14
#define VGA_WHITE         15

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_set_color(unsigned char fg, unsigned char bg);
void vga_printhex(uint32_t n);
void vga_printdec(uint32_t n);

/* Cursor / region access — used by the shell. */
int  vga_get_col(void);
int  vga_get_row(void);
void vga_set_col(int c);         /* move hardware cursor to column c, same row */

/* UI primitives — write outside the scroll region without side-effects. */
void vga_set_scroll_region(int top, int bot);          /* restrict scroll to rows top..bot (inclusive) */
void vga_put_at(int row, int col, char ch,             /* direct write, no serial mirror, no cursor move */
                unsigned char attr);
void vga_set_cursor_at(int row, int col);              /* move HW cursor only, no internal state change  */
