#include "uvm.h"
#include "paging.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* ── uvm_create ──────────────────────────────────────────────────────────────
 *
 * Allocates a 4 KB-aligned physical page for the new page directory.
 * PMM pages are in physical RAM < 128 MB, accessible at the same virtual
 * address via the kernel's identity map (virt == phys for low memory).
 */
uint32_t *uvm_create(void)
{
    uint32_t phys = pmm_alloc();
    if (!phys) return NULL;

    uint32_t *pd = (uint32_t *)phys;   /* identity map: virt == phys */

    /* Zero all 1024 PDEs. */
    for (int i = 0; i < 1024; i++)
        pd[i] = 0;

    /* Copy supervisor-only kernel mappings so the kernel keeps working
       (hardware access, kernel code) when this page directory is active. */
    paging_copy_kernel_mappings(pd);

    return pd;
}

/* ── uvm_map ─────────────────────────────────────────────────────────────────
 *
 * Creates (or reuses) a page table for the 4 MB region containing virt and
 * inserts a PTE for the exact 4 KB page.
 *
 * The PDE is created with PAGE_USER so the CPU's access checks on the PDE
 * level do not block user-mode accesses.  Individual page protection is then
 * controlled by the flags passed in the PTE.
 */
int uvm_map(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t pd_i = virt >> 22;
    uint32_t pt_i = (virt >> 12) & 0x3FFu;
    uint32_t *pt;

    if (pd[pd_i] & PAGE_PRESENT) {
        pt = (uint32_t *)(pd[pd_i] & ~0xFFFu);
    } else {
        uint32_t pt_phys = pmm_alloc();
        if (!pt_phys) return -1;
        pt = (uint32_t *)pt_phys;
        for (int i = 0; i < 1024; i++)
            pt[i] = 0;
        /* PDE needs PAGE_USER so ring-3 accesses reach the PTE at all. */
        pd[pd_i] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    pt[pt_i] = (phys & ~0xFFFu) | (flags | PAGE_PRESENT);
    return 0;
}

/* ── uvm_clone ───────────────────────────────────────────────────────────────
 *
 * Copy-on-Write fork: share physical pages between parent and child instead
 * of copying them eagerly.
 *
 * For each present user PTE (PDE[32..767]):
 *   · The physical page is mapped into the child's address space at the same
 *     virtual address with the same flags.
 *   · If the page was writable, both the parent's and child's PTEs have
 *     PAGE_WRITE cleared and PAGE_COW set.  The first write to the page in
 *     either task triggers a page fault which copies the page at that point.
 *   · Read-only pages are shared without any CoW marker — they are never
 *     written so no copy is ever needed.
 *   · pmm_addref() is called for every shared physical page so that
 *     pmm_free() (called by uvm_free()) only returns the page to the
 *     allocator when the last mapping is torn down.
 *
 * After marking the parent's writable PTEs read-only, the parent's TLB must
 * be flushed (CR3 reload) so stale writable TLB entries don't bypass the
 * protection.
 */
uint32_t *uvm_clone(uint32_t *src_pd)
{
    uint32_t *dst_pd = uvm_create();
    if (!dst_pd) return NULL;

    int parent_modified = 0;   /* did we downgrade any parent PTE? */

    for (int i = 32; i < 768; i++) {
        if (!(src_pd[i] & PAGE_PRESENT))
            continue;

        uint32_t *src_pt = (uint32_t *)(src_pd[i] & ~0xFFFu);

        for (int j = 0; j < 1024; j++) {
            uint32_t pte = src_pt[j];
            if (!(pte & PAGE_PRESENT))
                continue;

            uint32_t phys  = pte & ~0xFFFu;
            uint32_t flags = pte & (PAGE_WRITE | PAGE_USER | PAGE_COW);
            uint32_t virt  = ((uint32_t)i << 22) | ((uint32_t)j << 12);

            if (flags & PAGE_WRITE) {
                /* Downgrade parent PTE to read-only + CoW. */
                src_pt[j] = phys | PAGE_PRESENT | PAGE_USER | PAGE_COW;
                /* Child gets the same: read-only + CoW. */
                flags = PAGE_USER | PAGE_COW;
                parent_modified = 1;
            }

            /* Share the physical page with the child; bump its refcount. */
            pmm_addref(phys);
            if (uvm_map(dst_pd, virt, phys, flags) < 0) {
                uvm_free(dst_pd);
                return NULL;
            }
        }
    }

    /* Flush the parent's TLB — stale writable entries must not survive. */
    if (parent_modified) {
        uint32_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
    }

    return dst_pd;
}

/* ── uvm_free ────────────────────────────────────────────────────────────────
 *
 * Walks PDE[0..767] (user half only — skips kernel-half PDEs which are
 * shared and must not be freed here).  For each present PDE, frees every
 * present page the PTE points to, then frees the page table itself.
 * Finally frees the page directory.
 */
void uvm_free(uint32_t *pd)
{
    for (int i = 0; i < 768; i++) {
        if (!(pd[i] & PAGE_PRESENT))
            continue;
        uint32_t *pt = (uint32_t *)(pd[i] & ~0xFFFu);
        for (int j = 0; j < 1024; j++) {
            if (pt[j] & PAGE_PRESENT)
                pmm_free(pt[j] & ~0xFFFu);
        }
        pmm_free((uint32_t)pt);
    }
    pmm_free((uint32_t)pd);
}
