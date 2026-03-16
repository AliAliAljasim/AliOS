#include "fault.h"
#include "idt.h"
#include "paging.h"
#include "pmm.h"
#include "task.h"
#include "sched.h"
#include "vga.h"
#include <stdint.h>

/* ── #PF error-code bit definitions (Intel SDM Vol.3 §4.7) ─────────────────
 *
 *  Bit 0  P   0 = non-present page,  1 = protection violation
 *  Bit 1  W   0 = read access,       1 = write access
 *  Bit 2  U   0 = supervisor mode,   1 = user mode
 *  Bit 3  R   1 = reserved bit set in a PTE/PDE
 *  Bit 4  I   1 = instruction fetch (NX violation)
 */
#define PF_PRESENT  (1u << 0)
#define PF_WRITE    (1u << 1)
#define PF_USER     (1u << 2)
#define PF_RESERVED (1u << 3)
#define PF_IFETCH   (1u << 4)

/* Bottom of the kernel's virtual address space */
#define KERNEL_VIRT_BASE 0xC0000000u

/* ── Handler ─────────────────────────────────────────────────────────────── */

static void page_fault(struct regs *r)
{
    /* CR2 holds the virtual address that triggered the fault. */
    uint32_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    uint32_t err = r->err_code;

    /*
     * Demand-allocate a fresh 4 KB page for non-present kernel addresses.
     *
     * Conditions that must ALL be true to attempt recovery:
     *   · P=0  — the page simply isn't mapped yet (not a protection error)
     *   · U=0  — fault happened in supervisor (kernel) mode
     *   · R=0  — no reserved-bit corruption
     *   · cr2 is in kernel virtual space (≥ 0xC0000000)
     *
     * If pmm_alloc() succeeds we map the page and return; the CPU will
     * retry the faulting instruction transparently.
     *
     * This is the foundation for lazy kernel-heap expansion, future vmalloc
     * regions, and on-demand stack growth.
     */
    if (!(err & PF_PRESENT)  &&
        !(err & PF_USER)     &&
        !(err & PF_RESERVED) &&
        cr2 >= KERNEL_VIRT_BASE)
    {
        uint32_t phys = pmm_alloc();
        if (phys) {
            /* Map the faulting page (page-aligned) and return to retry. */
            paging_map(cr2 & ~0xFFFu, phys, PAGE_PRESENT | PAGE_WRITE);
            return;
        }
        /* Fall through: OOM — print panic below */
    }

    /*
     * ── Copy-on-Write resolution ────────────────────────────────────────────
     *
     * Conditions that identify a CoW fault:
     *   P=1  — page IS present (protection violation, not missing)
     *   W=1  — the access was a write
     *   U=1  — the faulting instruction ran in user mode
     *   CoW  — the PTE has PAGE_COW set (bit 9, an AVL bit)
     *
     * Action:
     *   · Walk the current task's page directory to find the PTE.
     *   · If the page's refcount is 1 (task is the sole owner), simply flip
     *     the PTE to writable and clear PAGE_COW — no copy needed.
     *   · Otherwise, allocate a fresh page, copy the content, drop the task's
     *     share of the old page via pmm_free(), and map the new page writable.
     *   · Invalidate the TLB entry and return so the CPU retries the write.
     */
    if ((err & PF_PRESENT) &&
        (err & PF_WRITE)   &&
        (err & PF_USER)    &&
        cr2 < KERNEL_VIRT_BASE &&
        current_task)
    {
        uint32_t *pd   = (uint32_t *)current_task->page_dir;
        uint32_t  pd_i = cr2 >> 22;
        uint32_t  pt_i = (cr2 >> 12) & 0x3FFu;

        if (pd[pd_i] & PAGE_PRESENT) {
            uint32_t *pt  = (uint32_t *)(pd[pd_i] & ~0xFFFu);
            uint32_t  pte = pt[pt_i];

            if ((pte & PAGE_PRESENT) && (pte & PAGE_COW)) {
                uint32_t old_phys = pte & ~0xFFFu;
                uint32_t base_flags = pte & ~(~0xFFFu | PAGE_COW);

                if (pmm_getref(old_phys) == 1) {
                    /* Sole owner — just make the page writable again. */
                    pt[pt_i] = old_phys | (base_flags | PAGE_WRITE);
                } else {
                    /* Shared — copy the page and install the private copy. */
                    uint32_t new_phys = pmm_alloc();
                    if (new_phys) {
                        uint8_t *src = (uint8_t *)old_phys;
                        uint8_t *dst = (uint8_t *)new_phys;
                        for (int k = 0; k < 4096; k++)
                            dst[k] = src[k];

                        pmm_free(old_phys);   /* drop this task's share */
                        pt[pt_i] = new_phys | (base_flags | PAGE_WRITE);
                    }
                    /* If OOM, fall through to the fatal handler below. */
                    if (!new_phys) goto fatal;
                }

                __asm__ volatile ("invlpg (%0)" :: "r"(cr2) : "memory");
                return;
            }
        }
    }

    /* ── Fatal fault — print a full diagnostic and halt ─────────────────── */
    fatal:

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n*** PAGE FAULT ***\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    vga_puts("  CR2: "); vga_printhex(cr2);
    vga_puts("  EIP: "); vga_printhex(r->eip);
    vga_puts("\n");

    /* Cause */
    vga_puts("  Cause: ");
    if (err & PF_RESERVED)
        vga_puts("reserved bit set in PTE");
    else if (err & PF_PRESENT)
        vga_puts("protection violation");
    else
        vga_puts("non-present page");

    /* Access type */
    vga_puts((err & PF_WRITE)  ? ", write"        : ", read");

    /* Privilege level */
    vga_puts((err & PF_USER)   ? ", user mode"    : ", kernel mode");

    /* Instruction fetch? */
    if (err & PF_IFETCH)
        vga_puts(", instruction fetch");

    vga_puts("\n*** HALTED ***\n");

    for (;;) {}
}

/* ── Public init ─────────────────────────────────────────────────────────── */

void fault_init(void)
{
    isr_install_handler(14, page_fault);
}
