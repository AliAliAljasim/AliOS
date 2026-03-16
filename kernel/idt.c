#include "idt.h"
#include "vga.h"
#include "io.h"
#include <stdint.h>

/* ── IDT structures ─────────────────────────────────────────── */

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;     /* 0x8E = present, ring0, 32-bit interrupt gate */
    uint16_t offset_high;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* ── Per-vector hook table ───────────────────────────────────── */
/*
 * Any module may install a C handler for any IDT vector (0-255).
 * isr_handler() dispatches to the hook first; the generic panic runs
 * only for unhandled CPU exceptions (vectors 0-31 with no hook).
 */
static void (*isr_hooks[256])(struct regs *);

void isr_install_handler(uint8_t n, void (*handler)(struct regs *))
{
    isr_hooks[n] = handler;
}

/* ── ISR stubs (defined in isr.asm) ────────────────────────── */

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

static void (*isr_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

/* ── IDT table ──────────────────────────────────────────────── */

static struct idt_entry idt[256];
static struct idtr      idtr;

void idt_set_gate(uint8_t n, uint32_t handler)
{
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;   /* kernel code segment */
    idt[n].zero        = 0;
    idt[n].type_attr   = 0x8E;  /* P=1, DPL=0, 32-bit interrupt gate */
    idt[n].offset_high = handler >> 16;
}

/* Same as idt_set_gate but DPL=3 — allows ring-3 code to invoke via 'int n'. */
void idt_set_gate_user(uint8_t n, uint32_t handler)
{
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].zero        = 0;
    idt[n].type_attr   = 0xEE;  /* P=1, DPL=3, 32-bit interrupt gate */
    idt[n].offset_high = handler >> 16;
}

/* ── PIC remapping ──────────────────────────────────────────── */
/*
 * The BIOS maps IRQ0-7 to INT 0x08-0x0F, which collides with
 * CPU exception vectors.  Remap master → 0x20, slave → 0x28.
 */
static void pic_remap(void)
{
    outb(0x20, 0x11);   /* ICW1: start init, expect ICW4 */
    outb(0xA0, 0x11);

    outb(0x21, 0x20);   /* ICW2: master base vector = 0x20 */
    outb(0xA1, 0x28);   /* ICW2: slave  base vector = 0x28 */

    outb(0x21, 0x04);   /* ICW3: master has slave on IRQ2 */
    outb(0xA1, 0x02);   /* ICW3: slave cascade identity  */

    outb(0x21, 0x01);   /* ICW4: 8086 mode */
    outb(0xA1, 0x01);

    outb(0x21, 0xFF);   /* mask all IRQs for now */
    outb(0xA1, 0xFF);
}

/* ── Exception handler ──────────────────────────────────────── */

static const char *exception_names[32] = {
    "Division by Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Security Exception",
    "Reserved"
};

void isr_handler(struct regs *r)
{
    /* Dispatch to a registered hook (exceptions, syscalls, etc.). */
    if (r->int_no < 256 && isr_hooks[r->int_no]) {
        isr_hooks[r->int_no](r);
        return;
    }

    /* Generic kernel panic — no hook installed for this exception. */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n*** EXCEPTION: ");
    if (r->int_no < 32)
        vga_puts(exception_names[r->int_no]);
    else
        vga_puts("Unknown");
    vga_puts(" ***\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("  EIP: "); vga_printhex(r->eip);
    vga_puts("  ERR: "); vga_printhex(r->err_code);
    vga_puts("\n*** HALTED ***\n");

    for (;;) {}
}

/* ── Public init ────────────────────────────────────────────── */

void idt_init(void)
{
    pic_remap();

    for (int i = 0; i < 32; i++)
        idt_set_gate(i, (uint32_t)isr_stubs[i]);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)idt;

    __asm__ volatile ("lidt (%0)" : : "r"(&idtr));
}
