#include "timer.h"
#include "irq.h"
#include "io.h"
#include <stdint.h>

/*
 * 8253/8254 PIT (Programmable Interval Timer)
 *
 * Channel 0 output is wired to IRQ0.
 * Base oscillator frequency: 1193182 Hz.
 * Divisor = 1193182 / desired_hz.
 *
 * Command byte 0x36:
 *   bits 7-6 = 00  → channel 0
 *   bits 5-4 = 11  → access mode: lobyte then hibyte
 *   bits 3-1 = 011 → mode 3 (square wave generator)
 *   bit  0   = 0   → binary counting
 */

#define PIT_BASE_HZ  1193182UL
#define PIT_CMD      0x43
#define PIT_CH0      0x40

static volatile uint32_t ticks;
static void (*tick_hook)(void);

void timer_on_tick(void (*fn)(void))
{
    tick_hook = fn;
}

static void timer_handler(void)
{
    ticks++;
    if (tick_hook)
        tick_hook();
}

uint32_t timer_ticks(void)
{
    return ticks;
}

void timer_init(uint32_t hz)
{
    uint32_t divisor = PIT_BASE_HZ / hz;

    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));         /* low byte  */
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));  /* high byte */

    irq_install_handler(0, timer_handler);
    irq_unmask(0);
}
