#pragma once

#include <stdint.h>

void     pmm_init(void);
uint32_t pmm_alloc(void);        /* returns physical address of a free page, 0 = OOM */
void     pmm_free(uint32_t addr);/* decrement refcount; actually frees when it hits 0 */
void     pmm_addref(uint32_t addr); /* increment refcount (for CoW page sharing) */
uint8_t  pmm_getref(uint32_t addr); /* return current refcount */
uint32_t pmm_free_pages(void);
