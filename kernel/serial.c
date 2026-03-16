#include "serial.h"
#include "io.h"
#include <stdint.h>

/* COM1 base I/O port */
#define COM1  0x3F8

/* UART register offsets (DLAB=0) */
#define UART_THR   0   /* Transmit Holding Register (write) */
#define UART_IER   1   /* Interrupt Enable Register         */
#define UART_FCR   2   /* FIFO Control Register             */
#define UART_LCR   3   /* Line Control Register             */
#define UART_MCR   4   /* Modem Control Register            */
#define UART_LSR   5   /* Line Status Register              */

/* LSR bits */
#define LSR_THRE   0x20   /* Transmit Holding Register Empty */

/* LCR bits */
#define LCR_DLAB   0x80   /* Divisor Latch Access Bit */
#define LCR_8N1    0x03   /* 8 data bits, no parity, 1 stop bit */

void serial_init(void)
{
    /* Disable interrupts */
    outb(COM1 + UART_IER, 0x00);

    /* Enable DLAB, set divisor for 115200 baud (clock 1.8432 MHz / 16 / 1) */
    outb(COM1 + UART_LCR, LCR_DLAB);
    outb(COM1 + 0, 0x01);   /* Divisor low byte  (115200 baud) */
    outb(COM1 + 1, 0x00);   /* Divisor high byte               */

    /* 8N1, clear DLAB */
    outb(COM1 + UART_LCR, LCR_8N1);

    /* Enable FIFO, clear TX/RX FIFOs, 14-byte threshold */
    outb(COM1 + UART_FCR, 0xC7);

    /* RTS + DTR asserted */
    outb(COM1 + UART_MCR, 0x03);
}

static void wait_tx_ready(void)
{
    while (!(inb(COM1 + UART_LSR) & LSR_THRE))
        ;
}

void serial_putchar(char c)
{
    if (c == '\n') {
        wait_tx_ready();
        outb(COM1 + UART_THR, '\r');
    }
    wait_tx_ready();
    outb(COM1 + UART_THR, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putchar(*s++);
}

void serial_printdec(uint32_t n)
{
    if (n == 0) { serial_putchar('0'); return; }
    char buf[10];
    int  i = 0;
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) serial_putchar(buf[i]);
}
