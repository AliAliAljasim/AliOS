#pragma once

#include <stdint.h>

#define PAGE_PRESENT  (1u << 0)   /* page is present in memory              */
#define PAGE_WRITE    (1u << 1)   /* page is writable                        */
#define PAGE_USER     (1u << 2)   /* page is accessible in ring 3            */
#define PAGE_COW      (1u << 9)   /* Copy-on-Write (AVL bit; CPU ignores it) */

/* Set up a page directory and identity-map the first 128 MB, then
   enable paging by setting CR0.PG.  Call after pmm_init(). */
void paging_init(void);

/* Map one 4 KB page: virt → phys with the given flags.
   Allocates a new page table from PMM if needed.
   Safe to call after paging_init(). */
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags);

/* Return the physical address of the current page directory (the value in CR3).
   Used by task_create() to record the page directory for a new task. */
uint32_t paging_physdir(void);

/* Copy kernel-half PDEs into a freshly allocated user page directory.
 *
 * Copies two regions from the kernel's page_dir into dst_pd:
 *   PDE[  0.. 31]  identity map  (supervisor-only; keeps kernel hardware
 *                                 access (VGA, PMM) working when running
 *                                 with a user page directory during syscalls)
 *   PDE[768..799]  kernel high   (0xC0000000+; kernel code and data)
 *
 * Both regions are supervisor-only in the source, so user code cannot
 * reach them even after they appear in the user's page directory.
 */
void paging_copy_kernel_mappings(uint32_t *dst_pd);
