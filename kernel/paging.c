#include "paging.h"
#include "pmm.h"
#include <stdint.h>

/*
 * Difference between the kernel's virtual and physical base addresses.
 *   physical = virtual - KERNEL_OFFSET
 */
#define KERNEL_OFFSET 0xC0000000u

/*
 * The permanent page directory.
 * Its *physical* address (= (uint32_t)page_dir - KERNEL_OFFSET) is loaded
 * into CR3 by paging_init().
 */
static uint32_t page_dir[1024] __attribute__((aligned(4096)));

void paging_init(void)
{
    /* Zero the page directory (all PDEs non-present). */
    for (int i = 0; i < 1024; i++)
        page_dir[i] = 0;

    /*
     * Build two independent sets of 32 page tables:
     *
     *   PDE[  0 ..  31]: identity map  0x00000000–0x07FFFFFF → same physical
     *   PDE[768 .. 799]: kernel-high   0xC0000000–0xC7FFFFFF → 0x00000000–0x07FFFFFF
     *
     * Using *separate* tables (not aliased) is important: paging_map() can
     * then modify a kernel-high PTE without accidentally changing the
     * matching identity-map entry.
     *
     * Entry32.asm already enabled paging with boot_page_dir (two PSE 4 MB
     * entries). PMM-allocated page tables land at physical 0x100000–0x13FFFF
     * — all reachable as virtual addresses via the boot identity map. ✓
     */

    /* ── Identity map (PDE[0..31]) ──────────────────────────────────────── */
    for (int pd_i = 0; pd_i < 32; pd_i++) {
        uint32_t  pt_phys = pmm_alloc();        /* physical address         */
        uint32_t *pt      = (uint32_t *)pt_phys; /* valid via boot id-map ✓ */
        if (!pt_phys)
            return;

        for (int pt_j = 0; pt_j < 1024; pt_j++) {
            uint32_t phys = ((uint32_t)pd_i << 22) | ((uint32_t)pt_j << 12);
            pt[pt_j] = phys | PAGE_PRESENT | PAGE_WRITE;
        }
        page_dir[pd_i] = pt_phys | PAGE_PRESENT | PAGE_WRITE;
    }

    /* ── Kernel high map (PDE[768..799]) ────────────────────────────────── */
    for (int pd_i = 0; pd_i < 32; pd_i++) {
        uint32_t  pt_phys = pmm_alloc();
        uint32_t *pt      = (uint32_t *)pt_phys;
        if (!pt_phys)
            return;

        for (int pt_j = 0; pt_j < 1024; pt_j++) {
            /* Same physical frame as the identity table for this region */
            uint32_t phys = ((uint32_t)pd_i << 22) | ((uint32_t)pt_j << 12);
            pt[pt_j] = phys | PAGE_PRESENT | PAGE_WRITE;
        }
        /* 768 = 0xC0000000 >> 22 */
        page_dir[768 + pd_i] = pt_phys | PAGE_PRESENT | PAGE_WRITE;
    }

    /*
     * Switch CR3 to the new page directory.
     *
     * CR3 requires the *physical* address. page_dir is a BSS symbol whose
     * virtual address is 0xC000xxxx; subtract KERNEL_OFFSET to get physical.
     *
     * Writing CR3 flushes the entire TLB and makes the new mappings live
     * immediately. Paging remains enabled — we are only changing directories,
     * not re-enabling paging.
     */
    __asm__ volatile (
        "mov %0, %%cr3"
        :: "r"((uint32_t)page_dir - KERNEL_OFFSET)
        : "memory"
    );
}

uint32_t paging_physdir(void)
{
    return (uint32_t)page_dir - KERNEL_OFFSET;
}

void paging_copy_kernel_mappings(uint32_t *dst_pd)
{
    /* Identity map (PDE[0..31], supervisor-only): kernel hardware access. */
    for (int i = 0; i < 32; i++)
        dst_pd[i] = page_dir[i];
    /* Kernel high (PDE[768..799], supervisor-only): kernel code + data. */
    for (int i = 768; i < 800; i++)
        dst_pd[i] = page_dir[i];
}

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t pd_i = virt >> 22;
    uint32_t pt_i = (virt >> 12) & 0x3FF;
    uint32_t *pt;

    if (page_dir[pd_i] & PAGE_PRESENT) {
        /*
         * The PDE stores the page-table's *physical* address.
         * For page tables at physical 0x100000+ the identity map makes
         * that physical address valid as a virtual pointer too. ✓
         */
        pt = (uint32_t *)(page_dir[pd_i] & ~0xFFFu);
    } else {
        pt = (uint32_t *)pmm_alloc();
        if (!pt)
            return;
        for (int i = 0; i < 1024; i++)
            pt[i] = 0;
        page_dir[pd_i] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE;
    }

    pt[pt_i] = (phys & ~0xFFFu) | (flags | PAGE_PRESENT);
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}
