#pragma once

#include <stdint.h>

/*
 * User Virtual Memory
 * ───────────────────
 * Every user process has its own page directory.  The kernel half
 * (0xC0000000+) and the identity map (0x00000000–0x07FFFFFF) are shared
 * across all page directories as supervisor-only mappings so the kernel
 * can access hardware and run its own code regardless of which CR3 is
 * currently loaded.
 *
 * Safe virtual address ranges for user code/data/stack:
 *   0x08000000 – 0xBFFFFFFF   (PDE[32..767], not used by kernel)
 *
 * Do NOT use uvm_map() on PDE[0..31] or PDE[768..799] — those page tables
 * are shared with the kernel and must remain supervisor-only.
 *
 * Suggested layout:
 *   0x08000000  user code (TEXT)
 *   0x09000000  user data (DATA/BSS)
 *   0xBFFFF000  user stack (one page, grows down from 0xBFFFFFFF)
 */

/* Allocate and zero a new page directory, then copy the kernel's supervisor
   mappings into it.  Returns the virtual (= physical via identity map) address
   of the new page directory, or NULL on OOM. */
uint32_t *uvm_create(void);

/* Map one 4 KB user page: virtual virt → physical phys with the given flags.
 *
 * flags must include PAGE_USER (bit 2) so ring-3 code can access the page.
 * Adds PAGE_USER to the PDE as well when creating a new page table.
 * Returns 0 on success, -1 on OOM.
 *
 * Caller must NOT map into the identity-map (0x00000000–0x07FFFFFF) or
 * kernel-high (0xC0000000+) regions.
 */
int uvm_map(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags);

/* Free all user page tables and their pages (PDE[0..767]), then free the
   page directory itself.  Does NOT free the kernel-half entries. */
void uvm_free(uint32_t *pd);

/* Clone a user address space: allocate a new page directory, copy kernel
   mappings, and eager-copy all present user pages (PDE[32..767]).
   Returns the new page directory (phys == virt via identity map), or NULL on OOM. */
uint32_t *uvm_clone(uint32_t *src_pd);

/* ── Ring-3 entry ─────────────────────────────────────────────────────────
 *
 * user_enter — transition from ring 0 to ring 3.  Does NOT return.
 *
 * Loads the user data segment selectors, builds a five-element iret frame,
 * and executes iret.  The CPU switches to ring 3, loads the user stack
 * (SS:ESP), and resumes at EIP.
 *
 *   eip — virtual address of first user-mode instruction
 *   esp — initial user-mode stack pointer
 *
 * Must be called with interrupts enabled (sti) or immediately after a task
 * trampoline that enables them, so the new task can be preempted.
 */
void user_enter(uint32_t eip, uint32_t esp);

/* Like user_enter but sets EAX = eax_val before iret.
   Used so fork() children get EAX = 0 as their fork return value. */
void user_enter_eax(uint32_t eip, uint32_t esp, uint32_t eax_val);
