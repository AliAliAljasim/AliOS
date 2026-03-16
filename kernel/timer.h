#pragma once

#include <stdint.h>

void     timer_init(uint32_t hz);
uint32_t timer_ticks(void);

/* Register a function called on every timer tick (IRQ context, IF=0).
   Pass NULL to remove the callback.  Only one slot; last write wins. */
void timer_on_tick(void (*fn)(void));
