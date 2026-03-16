#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* ── Constants ──────────────────────────────────────────────── */

/* The kernel is linked at virtual 0xC0001000; physical = virtual - this. */
#define KERNEL_OFFSET 0xC0000000u

#define PAGE_SIZE   4096
#define PAGES_MAX   (256 * 1024 * 1024 / PAGE_SIZE)   /* covers 256 MB → 65536 pages */

/* ── E820 memory map (written by bootloader) ────────────────── */
/*
 * Bootloader stores:
 *   0x4FC  uint16_t  number of entries
 *   0x500  entries[] 24 bytes each
 *
 * Entry layout (ACPI E820, 24-byte form):
 *   uint64_t base
 *   uint64_t length
 *   uint32_t type     1 = usable RAM
 *   uint32_t attrib   (extended, may be zero)
 */

#define E820_USABLE  1

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attrib;
} __attribute__((packed));

#define E820_COUNT  (*(volatile uint16_t *)0x4FC)
#define E820_MAP    ((struct e820_entry *)0x500)

/* ── Kernel extent (symbols from kernel.ld) ─────────────────── */

extern char kernel_end[];

/* ── Bitmap (1 bit per 4 KB page; 1 = used, 0 = free) ──────── */

static uint8_t  bitmap[PAGES_MAX / 8];   /* 8 KB */
static uint32_t free_count;

/* ── Reference counts (for Copy-on-Write) ───────────────────── */

static uint8_t page_refs[PAGES_MAX];   /* saturates at 255 */

static void page_set_used(uint32_t p) { bitmap[p >> 3] |=  (1u << (p & 7)); }
static void page_set_free(uint32_t p) { bitmap[p >> 3] &= ~(1u << (p & 7)); }
static int  page_is_free (uint32_t p) { return !(bitmap[p >> 3] & (1u << (p & 7))); }

/* ── Range helpers ──────────────────────────────────────────── */

/* Mark every page that falls entirely within [base, base+len) as free */
static void range_free(uint64_t base, uint64_t len)
{
    /* Round base up and end down to page boundaries */
    uint64_t start = (base + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end   = (base + len)            & ~(uint64_t)(PAGE_SIZE - 1);

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t page = (uint32_t)(addr / PAGE_SIZE);
        if (page < PAGES_MAX && !page_is_free(page)) {
            page_set_free(page);
            free_count++;
        }
    }
}

/* Mark every page touched by [base, end) as used */
static void range_used(uint32_t base, uint32_t end)
{
    uint32_t p = base / PAGE_SIZE;
    uint32_t e = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (; p < e && p < PAGES_MAX; p++) {
        if (page_is_free(p)) {
            page_set_used(p);
            if (free_count) free_count--;
        }
    }
}

/* ── Public API ─────────────────────────────────────────────── */

void pmm_init(void)
{
    /* Start with every page marked used */
    for (uint32_t i = 0; i < sizeof(bitmap); i++)
        bitmap[i] = 0xFF;
    free_count = 0;

    /* Free pages reported as usable by BIOS E820 */
    uint16_t count = E820_COUNT;
    for (uint16_t i = 0; i < count; i++) {
        if (E820_MAP[i].type == E820_USABLE)
            range_free(E820_MAP[i].base, E820_MAP[i].length);
    }

    /* Re-protect the first 1 MB (BIOS, IVT, bootloader, E820 buffer) */
    range_used(0x00000, 0x100000);

    /* Re-protect the kernel itself (0x1000 … kernel_end).
     * kernel_end is a virtual address (0xC000xxxx); subtract KERNEL_OFFSET
     * to get the physical end of the kernel image. */
    range_used(0x1000, (uint32_t)kernel_end - KERNEL_OFFSET);
}

uint32_t pmm_alloc(void)
{
    for (uint32_t p = 0; p < PAGES_MAX; p++) {
        if (page_is_free(p)) {
            page_set_used(p);
            if (free_count) free_count--;
            page_refs[p] = 1;
            return p * PAGE_SIZE;
        }
    }
    return 0;   /* out of memory */
}

void pmm_free(uint32_t addr)
{
    uint32_t p = addr / PAGE_SIZE;
    if (p >= PAGES_MAX || page_is_free(p))
        return;
    /* With reference counting: only truly release the page when the
       last reference is dropped.  pmm_addref() raises the count;
       pmm_free() lowers it.  A freshly-allocated page starts at 1. */
    if (page_refs[p] > 1) {
        page_refs[p]--;
        return;
    }
    page_refs[p] = 0;
    page_set_free(p);
    free_count++;
}

/* Increment the reference count of a page (for CoW sharing). */
void pmm_addref(uint32_t addr)
{
    uint32_t p = addr / PAGE_SIZE;
    if (p < PAGES_MAX && page_refs[p] < 255)
        page_refs[p]++;
}

/* Return the current reference count (0 = free, 1 = exclusive, >1 = shared). */
uint8_t pmm_getref(uint32_t addr)
{
    uint32_t p = addr / PAGE_SIZE;
    return (p < PAGES_MAX) ? page_refs[p] : 0;
}

uint32_t pmm_free_pages(void)
{
    return free_count;
}
