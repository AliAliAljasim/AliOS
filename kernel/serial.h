#pragma once

#include <stdint.h>

/* COM1 UART at I/O port 0x3F8, 115200 baud, 8N1.
 * Output only — no input buffering.
 * serial_putchar / serial_puts / serial_printdec mirror the vga_* API so
 * debug prints can be sent to both VGA and the host terminal simultaneously. */

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *s);
void serial_printdec(uint32_t n);
