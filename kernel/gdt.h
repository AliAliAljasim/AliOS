#pragma once

#include <stdint.h>

/* ── Segment selectors ───────────────────────────────────────────────────────
 *
 *  Index  Selector  Ring  Content
 *  -----  --------  ----  ------------------------------------------
 *    0      0x00     —    Null  (required by the architecture)
 *    1      0x08     0    Kernel code  (execute+read, flat 4 GB)
 *    2      0x10     0    Kernel data  (read+write,   flat 4 GB)
 *    3      0x18     3    User code    (execute+read, flat 4 GB)
 *    4      0x20     3    User data    (read+write,   flat 4 GB)
 *    5      0x28     0    TSS          (32-bit available task-state segment)
 *
 *  When loading a user-mode segment register the selector must include
 *  RPL=3 in the low two bits (0x1B for user code, 0x23 for user data).
 */
#define SEG_KERNEL_CODE  0x08
#define SEG_KERNEL_DATA  0x10
#define SEG_USER_CODE    0x18   /* add | 3 → 0x1B when loading ring-3 regs */
#define SEG_USER_DATA    0x20   /* add | 3 → 0x23 when loading ring-3 regs */
#define SEG_TSS          0x28

/* ── 32-bit Task State Segment ───────────────────────────────────────────────
 *
 * The CPU reads esp0 / ss0 on every ring-3 → ring-0 transition (interrupts,
 * exceptions, syscalls) to set up the kernel-mode stack.  All other fields
 * are zeroed at boot; they are only used for hardware task switching, which
 * we do not implement.
 *
 * Layout matches the Intel SDM Vol.3 §7.2.1 exactly (32-bit TSS, 104 bytes).
 */
typedef struct {
    uint32_t prev_tss;   /*   0  previous TSS link (unused) */
    uint32_t esp0;       /*   4  ring-0 stack pointer ← we set this */
    uint32_t ss0;        /*   8  ring-0 stack segment ← we set this */
    uint32_t esp1;       /*  12  ring-1 (unused)      */
    uint32_t ss1;        /*  16                       */
    uint32_t esp2;       /*  20  ring-2 (unused)      */
    uint32_t ss2;        /*  24                       */
    uint32_t cr3;        /*  28                       */
    uint32_t eip;        /*  32                       */
    uint32_t eflags;     /*  36                       */
    uint32_t eax;        /*  40                       */
    uint32_t ecx;        /*  44                       */
    uint32_t edx;        /*  48                       */
    uint32_t ebx;        /*  52                       */
    uint32_t esp;        /*  56                       */
    uint32_t ebp;        /*  60                       */
    uint32_t esi;        /*  64                       */
    uint32_t edi;        /*  68                       */
    uint32_t es;         /*  72                       */
    uint32_t cs;         /*  76                       */
    uint32_t ss;         /*  80                       */
    uint32_t ds;         /*  84                       */
    uint32_t fs;         /*  88                       */
    uint32_t gs;         /*  92                       */
    uint32_t ldt;        /*  96                       */
    uint16_t trap;       /* 100                       */
    uint16_t iomap_base; /* 102  offset to I/O permission bitmap */
                         /* 104 bytes total           */
} __attribute__((packed)) tss_t;

/* Initialise the kernel GDT, load it, reload all segment registers,
   set up the TSS, and execute ltr.  Call once at startup. */
void gdt_init(void);

/* Update the kernel-mode stack pointer stored in the TSS.
   Call this before switching to a new task so that the CPU finds the right
   ring-0 stack on the next ring-3 → ring-0 transition. */
void tss_set_kernel_stack(uint32_t esp0);
