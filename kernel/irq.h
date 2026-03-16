#pragma once

#include <stdint.h>

void irq_init(void);
void irq_install_handler(int irq, void (*handler)(void));
void irq_mask(int irq);
void irq_unmask(int irq);

/* called from irq_common in isr.asm */
void irq_dispatch(uint32_t irq);
