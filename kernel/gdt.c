#include "gdt.h"
#include <stdint.h>

/* ── GDT entry (segment descriptor), 8 bytes ────────────────────────────── */

struct gdt_entry {
    uint16_t limit_low;    /* limit[15:0]           */
    uint16_t base_low;     /* base[15:0]            */
    uint8_t  base_mid;     /* base[23:16]           */
    uint8_t  access;       /* P, DPL, S, type       */
    uint8_t  flags_limit;  /* flags[7:4] | lim[19:16] */
    uint8_t  base_high;    /* base[31:24]           */
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* ── Static objects (BSS → zero-initialised) ─────────────────────────────── */

static struct gdt_entry gdt[6];
static tss_t            tss;

/* Kernel stack top exported by entry32.asm */
extern char kernel_stack_top[];

/* ── Helper ──────────────────────────────────────────────────────────────── */

/*
 * Encode one GDT entry.
 *
 *  access    – the "access byte" (byte 5 of the descriptor):
 *                bit 7: P   (present)
 *                bits 6-5: DPL  (privilege, 0=kernel 3=user)
 *                bit 4: S   (1=code/data, 0=system/TSS)
 *                bits 3-0: type (1010=exec+read code, 0010=r/w data,
 *                                1001=32-bit available TSS)
 *  flags     – upper nibble is the flags nibble of byte 6:
 *                bit 7: G   (granularity 0=byte 1=4KB)
 *                bit 6: D/B (0=16-bit 1=32-bit)
 *                bit 5: L   (64-bit code segment, keep 0)
 *                bit 4: AVL (available for OS use, keep 0)
 *              lower nibble is ignored (we take limit[19:16] from 'limit').
 */
static void set_entry(int i, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    gdt[i].limit_low   =  limit         & 0xFFFF;
    gdt[i].base_low    =  base          & 0xFFFF;
    gdt[i].base_mid    = (base  >> 16)  & 0xFF;
    gdt[i].access      =  access;
    gdt[i].flags_limit = (flags & 0xF0) | ((limit >> 16) & 0x0F);
    gdt[i].base_high   = (base  >> 24)  & 0xFF;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_init(void)
{
    /*
     * Fill the six descriptors.
     *
     * Flat 32-bit code/data entries:
     *   base  = 0x00000000 (covers all linear space)
     *   limit = 0xFFFFF    (with G=1 → 0xFFFFF × 4 KB = 4 GB)
     *   flags = 0xCF       (G=1 | D/B=1 | — | — | limit[19:16]=0xF)
     *
     * Access bytes:
     *   0x9A = 1_00_1_1010  kernel code  (P, DPL=0, S=1, exec+read)
     *   0x92 = 1_00_1_0010  kernel data  (P, DPL=0, S=1, r/w data)
     *   0xFA = 1_11_1_1010  user code    (P, DPL=3, S=1, exec+read)
     *   0xF2 = 1_11_1_0010  user data    (P, DPL=3, S=1, r/w data)
     *   0x89 = 1_00_0_1001  TSS          (P, DPL=0, S=0, 32-bit avail TSS)
     */
    set_entry(0, 0, 0,            0x00, 0x00);   /* null        */
    set_entry(1, 0, 0xFFFFF,      0x9A, 0xCF);   /* kernel code */
    set_entry(2, 0, 0xFFFFF,      0x92, 0xCF);   /* kernel data */
    set_entry(3, 0, 0xFFFFF,      0xFA, 0xCF);   /* user code   */
    set_entry(4, 0, 0xFFFFF,      0xF2, 0xCF);   /* user data   */

    /*
     * TSS descriptor.
     *
     * The CPU reads esp0/ss0 from this TSS on every ring-3→ring-0 transition
     * (interrupt, exception, syscall) to set up the kernel stack.
     *
     *   base  = linear address of the tss struct (virtual == linear here)
     *   limit = sizeof(tss_t) - 1 = 103 bytes
     *   flags = 0x00  (G=0 so limit is in bytes, not 4KB pages)
     *
     * iomap_base is set to sizeof(tss_t) — one byte past the TSS limit — so
     * there is no I/O permission bitmap.  Any ring-3 port access will #GP.
     */
    tss.ss0        = SEG_KERNEL_DATA;
    tss.esp0       = (uint32_t)kernel_stack_top;
    tss.iomap_base = (uint16_t)sizeof(tss_t);

    set_entry(5, (uint32_t)&tss, sizeof(tss_t) - 1, 0x89, 0x00);

    /* ── Load GDTR ───────────────────────────────────────────────────────── */

    struct gdtr gdtr = {
        .limit = sizeof(gdt) - 1,   /* 47 bytes (6 × 8 − 1) */
        .base  = (uint32_t)gdt,     /* linear (= virtual) address */
    };
    __asm__ volatile ("lgdt (%0)" : : "r"(&gdtr) : "memory");

    /*
     * Reload CS.  You cannot mov directly into CS; the canonical method is
     * a far return: push the new selector, push the return address, lret.
     * The CPU pops EIP first, then CS.
     */
    __asm__ volatile (
        "push $0x08\n"   /* new CS = SEG_KERNEL_CODE          */
        "push $1f\n"     /* EIP to continue from              */
        "lret\n"         /* far ret: EIP ← [ESP], CS ← [ESP+4] */
        "1:\n"
        : : : "memory"
    );

    /* Reload the data segment registers. */
    __asm__ volatile (
        "movw $0x10, %%ax\n"
        "movw %%ax,  %%ds\n"
        "movw %%ax,  %%es\n"
        "movw %%ax,  %%ss\n"
        "movw %%ax,  %%fs\n"
        "movw %%ax,  %%gs\n"
        : : : "eax"
    );

    /* Load the TSS into the Task Register. */
    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)SEG_TSS));
}
