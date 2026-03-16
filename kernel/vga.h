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

/* Cursor column access — used by the shell for line editing. */
int  vga_get_col(void);          /* return current column (0-79)              */
void vga_set_col(int c);         /* move hardware cursor to column c, same row */
