#include "heap.h"
#include "pmm.h"
#include <stdint.h>

#define PAGE_SIZE  4096

/* ── Block header ────────────────────────────────────────────────────────────
 *
 *  Each allocation is preceded by a 12-byte header (on 32-bit):
 *
 *   [  size (4B)  |  free (4B)  |  next* (4B)  |  <payload>  ]
 *
 *  size  – usable bytes that follow this header
 *  free  – 1 = available, 0 = in use
 *  next  – pointer to the next block header (NULL if last)
 */
typedef struct block_hdr {
    uint32_t         size;
    uint32_t         free;
    struct block_hdr *next;
} block_hdr_t;

#define HDR_SIZE   ((uint32_t)sizeof(block_hdr_t))  /* 12 bytes */
#define MIN_SPLIT  (HDR_SIZE + 8u)  /* min leftover size worth splitting off */

static block_hdr_t *heap_head = NULL;

/* ── Internal: request one 4 KB page from PMM, grow the heap by one page ─── */
static int heap_grow(void)
{
    uint32_t page = pmm_alloc();
    if (!page)
        return 0;  /* OOM */

    block_hdr_t *blk = (block_hdr_t *)(uintptr_t)page;

    if (!heap_head) {
        blk->size = PAGE_SIZE - HDR_SIZE;
        blk->free = 1;
        blk->next = NULL;
        heap_head = blk;
        return 1;
    }

    /* Walk to the tail */
    block_hdr_t *cur = heap_head;
    while (cur->next)
        cur = cur->next;

    /*
     * If the tail block is free AND this new page is physically contiguous
     * with it (i.e. it starts exactly where the tail block's payload ends),
     * extend the tail block instead of inserting a new header.
     *
     * This is critical for kmalloc(KSTACK_SIZE=8192): each heap page holds
     * only 4084 bytes (4096 - 12-byte header), so a single page can never
     * satisfy an 8192-byte request.  By merging contiguous free pages we
     * build a block large enough after 2-3 grow calls.
     *
     * PMM allocates sequentially so consecutive heap_grow calls almost
     * always yield physically adjacent pages.
     */
    char *tail_end = (char *)cur + HDR_SIZE + cur->size;
    if (cur->free && (block_hdr_t *)tail_end == blk) {
        cur->size += PAGE_SIZE;   /* absorb the full new page into the payload */
        return 1;
    }

    /* Tail is in use or non-contiguous — append a fresh block */
    blk->size = PAGE_SIZE - HDR_SIZE;
    blk->free = 1;
    blk->next = NULL;
    cur->next  = blk;
    return 1;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void heap_init(void)
{
    heap_grow();  /* prime with one page; grows further on demand */
}

void *kmalloc(uint32_t size)
{
    if (!size)
        return NULL;

    /* Align payload to 4 bytes so that returned pointers are word-aligned */
    size = (size + 3u) & ~3u;

    for (;;) {
        block_hdr_t *cur = heap_head;
        while (cur) {
            if (cur->free && cur->size >= size) {
                /* Split if the remainder would be large enough to be useful */
                if (cur->size >= size + MIN_SPLIT) {
                    block_hdr_t *split = (block_hdr_t *)((char *)cur + HDR_SIZE + size);
                    split->size = cur->size - size - HDR_SIZE;
                    split->free = 1;
                    split->next = cur->next;
                    cur->next   = split;
                    cur->size   = size;
                }
                cur->free = 0;
                return (char *)cur + HDR_SIZE;
            }
            cur = cur->next;
        }

        /* No fitting block found — grow and retry */
        if (!heap_grow())
            return NULL;  /* out of physical memory */
    }
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    block_hdr_t *blk = (block_hdr_t *)((char *)ptr - HDR_SIZE);
    blk->free = 1;

    /* Coalesce adjacent free blocks (forward pass, repeat until stable) */
    int merged;
    do {
        merged = 0;
        block_hdr_t *cur = heap_head;
        while (cur && cur->next) {
            if (cur->free && cur->next->free) {
                /* Only merge if the blocks are physically contiguous */
                block_hdr_t *expected = (block_hdr_t *)((char *)cur + HDR_SIZE + cur->size);
                if (expected == cur->next) {
                    cur->size += HDR_SIZE + cur->next->size;
                    cur->next  = cur->next->next;
                    merged = 1;
                    continue;  /* re-check cur against its new next */
                }
            }
            cur = cur->next;
        }
    } while (merged);
}
