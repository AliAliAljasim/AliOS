#include "vga.h"
#include "io.h"
#include <stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BASE    ((volatile uint16_t *)0xB8000)

static int row;
static int col;
static unsigned char attr;  /* high nibble = bg, low nibble = fg */


static void write_cell(int r, int c, char ch)
{
    VGA_BASE[r * VGA_WIDTH + c] = ((uint16_t)attr << 8) | (uint8_t)ch;
}

static void scroll(void)
{
    for (int r = 0; r < VGA_HEIGHT - 1; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_BASE[r * VGA_WIDTH + c] = VGA_BASE[(r + 1) * VGA_WIDTH + c];

    for (int c = 0; c < VGA_WIDTH; c++)
        write_cell(VGA_HEIGHT - 1, c, ' ');

    row = VGA_HEIGHT - 1;
}

void vga_set_color(unsigned char fg, unsigned char bg)
{
    attr = (bg << 4) | (fg & 0x0F);
}

void vga_clear(void)
{
    for (int r = 0; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            write_cell(r, c, ' ');
    row = 0;
    col = 0;
}

void vga_putchar(char ch)
{
    if (ch == '\n') {
        col = 0;
        row++;
    } else if (ch == '\r') {
        col = 0;
    } else if (ch == '\b') {
        if (col > 0) {
            col--;
            write_cell(row, col, ' ');
        }
    } else {
        write_cell(row, col, ch);
        if (++col >= VGA_WIDTH) {
            col = 0;
            row++;
        }
    }

    if (row >= VGA_HEIGHT)
        scroll();

    /* Move the hardware cursor to match our software position */
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

void vga_puts(const char *s)
{
    while (*s)
        vga_putchar(*s++);
}

void vga_printhex(uint32_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        vga_putchar(hex[(n >> i) & 0xF]);
}

void vga_printdec(uint32_t n)
{
    if (n == 0) { vga_putchar('0'); return; }
    char buf[10];
    int  i = 0;
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) vga_putchar(buf[i]);
}

void vga_init(void)
{
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}

int vga_get_col(void)
{
    return col;
}

void vga_set_col(int c)
{
    if (c < 0)          c = 0;
    if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
    col = c;
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}
