#pragma once

#include <stdint.h>

/* ── Kernel pipe ─────────────────────────────────────────────────────────────
 *
 * A pipe is a unidirectional byte channel backed by a fixed-size ring buffer.
 * One end is the write end; the other is the read end.  When all write ends
 * are closed, read returns 0 (EOF).  When all read ends are closed, write
 * returns -1 (broken pipe).
 *
 * Pipes are allocated from a static pool; PIPE_MAX limits concurrency.
 * PIPE_BUF must be a power of two.
 */

#define PIPE_MAX  8     /* maximum concurrent pipes                          */
#define PIPE_BUF  512   /* ring buffer size in bytes (must be a power of 2)  */

/*
 * pipe_alloc — allocate a fresh pipe slot.
 *
 * The new pipe starts with nreaders = 1 and nwriters = 1, representing the
 * two ends that the caller is about to hand to two tasks.
 *
 * Returns the pipe index (0..PIPE_MAX-1) or -1 if no slot is free.
 */
int  pipe_alloc(void);

/* Increment open-end reference counts (call when a new fd references the pipe). */
void pipe_open_read(int pi);
void pipe_open_write(int pi);

/* Decrement open-end reference counts (call when an fd referencing the pipe closes).
   When nwriters reaches 0, subsequent reads return EOF.
   When nreaders reaches 0, subsequent writes return -1. */
void pipe_close_read(int pi);
void pipe_close_write(int pi);

/*
 * pipe_read — read up to `len` bytes from pipe `pi` into `buf`.
 *
 * Blocks (yields) until data is available or all writers have closed.
 * Returns bytes read (>0), or 0 at EOF (nwriters == 0 and buffer empty).
 */
int  pipe_read(int pi, void *buf, uint32_t len);

/*
 * pipe_write — write `len` bytes from `buf` into pipe `pi`.
 *
 * Blocks (yields) if the ring buffer is full.
 * Returns bytes written (== len), or -1 if all readers have closed (broken pipe).
 */
int  pipe_write(int pi, const void *buf, uint32_t len);
