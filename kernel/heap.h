#pragma once

#include <stdint.h>
#include <stddef.h>

/* Prime the heap with its first page from the PMM.
   Must be called after pmm_init() and paging_init(). */
void  heap_init(void);

/* Allocate 'size' bytes of kernel heap.  Returns NULL on OOM or if
   size == 0.  Maximum single allocation: PAGE_SIZE - header (~4084 B);
   for page-sized needs use pmm_alloc() directly. */
void *kmalloc(uint32_t size);

/* Return a kmalloc'd pointer back to the heap.  kfree(NULL) is a no-op. */
void  kfree(void *ptr);
