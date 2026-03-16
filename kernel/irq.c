#include "irq.h"
#include "idt.h"
#include "io.h"
#include <stdint.h>

/* IRQ stubs defined in isr.asm */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

static void (*irq_stubs[16])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15
};

/* Registered C handlers, one per IRQ line */
static void (*irq_handlers[16])(void);

void irq_install_handler(int irq, void (*handler)(void))
{
    irq_handlers[irq] = handler;
}

/* Mask (disable) one IRQ line */
void irq_mask(int irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    int      bit  = (irq < 8) ? irq  : irq - 8;
    outb(port, inb(port) | (1 << bit));
}

/* Unmask (enable) one IRQ line */
void irq_unmask(int irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    int      bit  = (irq < 8) ? irq  : irq - 8;
    outb(port, inb(port) & ~(1 << bit));
}

/* Called from irq_common in isr.asm after every hardware interrupt */
void irq_dispatch(uint32_t irq)
{
    /*
     * Send EOI *before* calling the handler.
     *
     * When the scheduler switches tasks inside the timer handler,
     * irq_dispatch() never returns on the preempted task's stack.
     * If EOI were sent after the handler, the PIC's ISR bit would stay
     * set and IRQ0 would never fire again, breaking preemption entirely.
     *
     * Safety: the CPU's IF flag is still 0 (cleared on interrupt entry),
     * so no nested interrupt can reach the CPU until irq_common's iret
     * restores EFLAGS — which only happens after the handler returns.
     */
    if (irq >= 8)
        outb(0xA0, 0x20);   /* slave PIC EOI */
    outb(0x20, 0x20);       /* master PIC EOI */

    if (irq_handlers[irq])
        irq_handlers[irq]();
}

/* Install all 16 IRQ gates into the IDT (vectors 32-47) */
void irq_init(void)
{
    for (int i = 0; i < 16; i++)
        idt_set_gate(32 + i, (uint32_t)irq_stubs[i]);
}
