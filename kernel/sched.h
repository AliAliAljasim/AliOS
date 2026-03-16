#pragma once

#include "task.h"
#include <stdint.h>

/* sched_init — start the preemptive scheduler and register the timer hook. */
void sched_init(void);

/* sched_add — add a READY task to the run queue. */
void sched_add(task_t *t);

/* sched_yield — voluntarily give up the CPU. */
void sched_yield(void);

/* sched_sleep — block for at least ms milliseconds (resolution: 10 ms at 100 Hz). */
void sched_sleep(uint32_t ms);

/* sched_block — block indefinitely until sched_wake() is called. */
void sched_block(void);

/* sched_wake — unblock a BLOCKED task. Safe from IRQ context. */
void sched_wake(task_t *t);

/* sched_exit — terminate the current task; wakes waiting parent. Does not return. */
void sched_exit(void);

/*
 * sched_wait — block until any direct child exits, then reap it.
 *
 * Writes the child's exit code to *exit_code_out (if non-NULL).
 * Returns the child's PID, or -1 if the caller has no children.
 */
int32_t sched_wait(int32_t *exit_code_out);

/*
 * sched_set_foreground — register the task that Ctrl-C should kill.
 * Pass NULL to clear.
 */
void sched_set_foreground(task_t *t);

/*
 * sched_ctrlc — send SIGKILL to the current foreground task.
 * Called from keyboard IRQ context when Ctrl-C is detected.
 */
void sched_ctrlc(void);

/* ── Trampolines (defined in sched.c, referenced by task.c) ──────────────── */
void task_trampoline(void);
void user_task_trampoline(void);
