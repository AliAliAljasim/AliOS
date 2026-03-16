#pragma once

#include <stdint.h>

/* CPU register dump pushed by isr_common (see isr.asm for exact layout).
   The same struct is passed to every exception and IRQ handler. */
struct regs {
    /* pusha: pushed in reverse order (edi first, eax last) */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    /* pushed by the ISR stub */
    uint32_t int_no, err_code;
    /* pushed by the CPU on exception entry */
    uint32_t eip, cs, eflags;
};

void idt_init(void);
void idt_set_gate(uint8_t n, uint32_t handler);

/* Like idt_set_gate but sets DPL=3 so ring-3 code can invoke 'int n'. */
void idt_set_gate_user(uint8_t n, uint32_t handler);

/* Install a C handler for any IDT vector (0–255).
   The handler receives the full register dump and may modify r->eax to
   set a return value (visible to the caller after iret). */
void isr_install_handler(uint8_t n, void (*handler)(struct regs *));
