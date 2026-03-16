#pragma once

/* Install the #PF (interrupt 14) handler.  Call after idt_init(). */
void fault_init(void);
