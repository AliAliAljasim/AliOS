#include "vga.h"
#include "serial.h"
#include "io.h"
#include <stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BASE    ((volatile uint16_t *)0xB8000)

static int row;
static int col;
static unsigned char attr;    /* high nibble = bg, low nibble = fg */
static int scroll_top = 0;    /* first row that scrolls            */
static int scroll_bot = VGA_HEIGHT - 1; /* last  row that scrolls   */


static void write_cell(int r, int c, char ch)
{
    VGA_BASE[r * VGA_WIDTH + c] = ((uint16_t)attr << 8) | (uint8_t)ch;
}

static void scroll(void)
{
    for (int r = scroll_top; r < scroll_bot; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_BASE[r * VGA_WIDTH + c] = VGA_BASE[(r + 1) * VGA_WIDTH + c];

    for (int c = 0; c < VGA_WIDTH; c++)
        write_cell(scroll_bot, c, ' ');

    row = scroll_bot;
}

void vga_set_color(unsigned char fg, unsigned char bg)
{
    attr = (bg << 4) | (fg & 0x0F);
}

static void update_cursor(void)
{
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

void vga_clear(void)
{
    for (int r = 0; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            write_cell(r, c, ' ');
    row = 0;
    col = 0;
    update_cursor();
}

void vga_putchar(char ch)
{
    serial_putchar(ch);   /* mirror every character to COM1 */

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

    if (row > scroll_bot)
        scroll();

    update_cursor();
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

int vga_get_row(void)
{
    return row;
}

void vga_set_col(int c)
{
    if (c < 0)          c = 0;
    if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
    col = c;
    update_cursor();
}

/* Restrict the scrollable region to rows top..bot (inclusive).
 * Resets the output cursor to (top, 0). */
void vga_set_scroll_region(int top, int bot)
{
    scroll_top = top;
    scroll_bot = bot;
    row = top;
    col = 0;
    update_cursor();
}

/* Write one character directly to (row, col) with the given attribute byte.
 * Does NOT advance the cursor, does NOT mirror to serial, does NOT scroll. */
void vga_put_at(int r, int c, char ch, unsigned char a)
{
    if (r < 0 || r >= VGA_HEIGHT || c < 0 || c >= VGA_WIDTH) return;
    VGA_BASE[r * VGA_WIDTH + c] = ((uint16_t)a << 8) | (uint8_t)ch;
}

/* Move only the VGA hardware cursor — does NOT change internal row/col. */
void vga_set_cursor_at(int r, int c)
{
    uint16_t pos = (uint16_t)(r * VGA_WIDTH + c);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}
