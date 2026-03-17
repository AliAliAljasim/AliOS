#include "pipe.h"
#include "sched.h"
#include <stdint.h>

/* ── Internal pipe state ─────────────────────────────────────────────────── */

typedef struct {
    uint8_t  data[PIPE_BUF];
    uint32_t head;      /* next write position (ever-increasing; mask on use) */
    uint32_t tail;      /* next read  position (ever-increasing; mask on use) */
    int      nreaders;  /* number of open read  ends (0 = pipe slot is free)  */
    int      nwriters;  /* number of open write ends                          */
} pipe_t;

/* Static pool — all-zero on startup → all slots free (nreaders == 0). */
static pipe_t pipes[PIPE_MAX];

/* ── Reference-count helpers ─────────────────────────────────────────────── */
/*
 * These are intentionally without cli/sti: callers are responsible for
 * ensuring they run either with interrupts already disabled (IRQ / sched
 * context) or from a serialised syscall path that wraps them in cli/sti.
 */

int pipe_alloc(void)
{
    for (int i = 0; i < PIPE_MAX; i++) {
        if (pipes[i].nreaders == 0 && pipes[i].nwriters == 0) {
            pipes[i].head     = 0;
            pipes[i].tail     = 0;
            pipes[i].nreaders = 1;
            pipes[i].nwriters = 1;
            return i;
        }
    }
    return -1;
}

void pipe_open_read(int pi)   { if (pi >= 0 && pi < PIPE_MAX) pipes[pi].nreaders++; }
void pipe_open_write(int pi)  { if (pi >= 0 && pi < PIPE_MAX) pipes[pi].nwriters++; }
void pipe_close_read(int pi)  { if (pi >= 0 && pi < PIPE_MAX && pipes[pi].nreaders > 0) pipes[pi].nreaders--; }
void pipe_close_write(int pi) { if (pi >= 0 && pi < PIPE_MAX && pipes[pi].nwriters > 0) pipes[pi].nwriters--; }

/* ── Blocking I/O ─────────────────────────────────────────────────────────── */

int pipe_read(int pi, void *vbuf, uint32_t len)
{
    uint8_t *buf = (uint8_t *)vbuf;
    uint32_t n   = 0;

    while (n < len) {
        __asm__ volatile ("cli");

        uint32_t avail = pipes[pi].head - pipes[pi].tail;
        if (avail == 0) {
            if (pipes[pi].nwriters == 0) {
                /* EOF — all writers gone, buffer empty. */
                __asm__ volatile ("sti");
                break;
            }
            /* Data not yet available; yield and retry. */
            __asm__ volatile ("sti");
            sched_yield();
            continue;
        }

        /* Drain as many bytes as are available (up to what caller wants). */
        uint32_t take = (avail < len - n) ? avail : (len - n);
        for (uint32_t k = 0; k < take; k++) {
            buf[n++] = pipes[pi].data[pipes[pi].tail & (PIPE_BUF - 1)];
            pipes[pi].tail++;
        }
        __asm__ volatile ("sti");
    }

    return (int)n;
}

int pipe_write(int pi, const void *vbuf, uint32_t len)
{
    const uint8_t *buf = (const uint8_t *)vbuf;
    uint32_t       n   = 0;

    while (n < len) {
        __asm__ volatile ("cli");

        if (pipes[pi].nreaders == 0) {
            /* Broken pipe — all readers gone. */
            __asm__ volatile ("sti");
            return -1;
        }

        uint32_t avail = pipes[pi].head - pipes[pi].tail;
        uint32_t space = PIPE_BUF - avail;
        if (space == 0) {
            /* Buffer full; yield and retry. */
            __asm__ volatile ("sti");
            sched_yield();
            continue;
        }

        uint32_t put = (space < len - n) ? space : (len - n);
        for (uint32_t k = 0; k < put; k++) {
            pipes[pi].data[pipes[pi].head & (PIPE_BUF - 1)] = buf[n++];
            pipes[pi].head++;
        }
        __asm__ volatile ("sti");
    }

    return (int)n;
}
