#include "sched.h"
#include "task.h"
#include "timer.h"
#include "gdt.h"
#include "uvm.h"
#include "heap.h"
#include <stdint.h>

/* ── Run queue ───────────────────────────────────────────────────────────────
 *
 * A circular singly-linked list.  current_task is always a member.
 * New tasks are inserted immediately after current_task so they run next.
 */

/* ── Trampolines ─────────────────────────────────────────────────────────────
 *
 * Every new task starts here (task_switch's "ret" lands here).
 * IF=0 on arrival (inherited from the timer IRQ that triggered the switch);
 * the trampoline explicitly re-enables interrupts.
 */
void task_trampoline(void)
{
    __asm__ volatile ("sti");
    current_task->entry();
    sched_exit();
}

void user_task_trampoline(void)
{
    __asm__ volatile ("sti");
    /*
     * user_enter_eax sets EAX = user_eax before iret.
     * For normal tasks user_eax = 0 (harmless).
     * For fork() children user_eax = 0 (the child's fork return value).
     */
    user_enter_eax(current_task->user_eip,
                   current_task->user_esp,
                   current_task->user_eax);
    sched_exit();
}

/* ── schedule ────────────────────────────────────────────────────────────────
 *
 * Called from sched_tick (IF=0) or sched_yield / sched_exit (IF=0).
 * Picks the next READY task in round-robin order and switches to it.
 */
static void schedule(void)
{
    task_t *prev = current_task;
    task_t *next = prev->next;

    while (next != prev &&
           (next->state == TASK_ZOMBIE || next->state == TASK_BLOCKED))
        next = next->next;

    if (next == prev)
        return;

    if (prev->state == TASK_RUNNING)
        prev->state = TASK_READY;
    next->state = TASK_RUNNING;

    if (next->kstack_top)
        tss_set_kernel_stack(next->kstack_top);

    task_switch(prev, next);
}

/* ── sched_tick ──────────────────────────────────────────────────────────────
 *
 * Installed as the PIT timer callback (IF=0, IRQ context).
 *
 * On every tick:
 *   1. Wake tasks whose sleep timer expired.
 *   2. Deliver SIGKILL (signal 9) to any task that has it pending.
 *   3. Wake tasks blocked in sched_wait() if any of their children exited.
 *   4. Preempt via schedule().
 */
static void sched_tick(void)
{
    uint32_t now = timer_ticks();
    task_t  *t   = current_task;

    do {
        /* 1. Sleep wakeup. */
        if (t->state == TASK_BLOCKED && t->wakeup_tick &&
            now >= t->wakeup_tick) {
            t->wakeup_tick = 0;
            t->state       = TASK_READY;
        }

        /* 2. SIGKILL delivery. */
        if ((t->pending_sigs & (1u << 9)) && t->state != TASK_ZOMBIE) {
            t->pending_sigs &= ~(1u << 9);
            t->exit_code = -9;
            t->state     = TASK_ZOMBIE;
            /* Fall through: the waiting-parent check below will wake the parent. */
        }

        t = t->next;
    } while (t != current_task);

    /* 3. Wake tasks waiting for a child that is now ZOMBIE. */
    t = current_task;
    do {
        if (t->state == TASK_BLOCKED && t->waiting) {
            task_t *c = current_task;
            do {
                if (c->state == TASK_ZOMBIE && c->parent_pid == t->pid) {
                    t->waiting = 0;
                    t->state   = TASK_READY;
                    break;
                }
                c = c->next;
            } while (c != current_task);
        }
        t = t->next;
    } while (t != current_task);

    schedule();
}

/* ── Foreground task (for Ctrl-C) ────────────────────────────────────────── */

static volatile task_t *foreground_task = NULL;

void sched_set_foreground(task_t *t)
{
    foreground_task = t;
}

void sched_ctrlc(void)
{
    task_t *t = (task_t *)foreground_task;
    if (t && t->state != TASK_ZOMBIE)
        t->pending_sigs |= (1u << 9);   /* SIGKILL */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void sched_init(void)
{
    current_task->next = current_task;
    timer_on_tick(sched_tick);
}

void sched_add(task_t *t)
{
    __asm__ volatile ("cli");
    t->state       = TASK_READY;
    t->next        = current_task->next;
    current_task->next = t;
    __asm__ volatile ("sti");
}

void sched_yield(void)
{
    __asm__ volatile ("cli");
    schedule();
    __asm__ volatile ("sti");
}

void sched_sleep(uint32_t ms)
{
    if (ms == 0) { sched_yield(); return; }

    __asm__ volatile ("cli");
    uint32_t ticks_needed = (ms + 9) / 10;
    uint32_t wake_at      = timer_ticks() + ticks_needed;
    if (wake_at == 0) wake_at = 1;
    current_task->wakeup_tick = wake_at;
    current_task->state       = TASK_BLOCKED;
    schedule();
    __asm__ volatile ("sti");
}

void sched_block(void)
{
    __asm__ volatile ("cli");
    current_task->state = TASK_BLOCKED;
    schedule();
    __asm__ volatile ("sti");
}

void sched_wake(task_t *t)
{
    __asm__ volatile ("cli");
    if (t->state == TASK_BLOCKED) {
        t->wakeup_tick = 0;
        t->state       = TASK_READY;
    }
    __asm__ volatile ("sti");
}

/*
 * sched_exit — terminate the current task.
 *
 * Marks it ZOMBIE, wakes the parent if it is blocking in sched_wait(),
 * then switches to the next READY task.
 */
void sched_exit(void)
{
    __asm__ volatile ("cli");

    current_task->state = TASK_ZOMBIE;

    /* Wake the parent if it is waiting for any child. */
    if (current_task->parent_pid) {
        task_t *p = current_task->next;
        while (p != current_task) {
            if (p->pid == current_task->parent_pid && p->waiting) {
                p->waiting = 0;
                p->state   = TASK_READY;
                break;
            }
            p = p->next;
        }
    }

    schedule();
    for (;;)
        __asm__ volatile ("sti; hlt; cli");
}

/*
 * sched_wait — block until any direct child has exited, then reap it.
 *
 * Returns the child's PID and writes its exit code to *exit_code_out
 * (if non-NULL).  Returns -1 if the caller has no children at all.
 *
 * The zombie task_t and its kernel stack are freed; the node is spliced
 * out of the circular run queue.
 */
int32_t sched_wait(int32_t *exit_code_out)
{
    for (;;) {
        __asm__ volatile ("cli");

        /* Scan for a zombie child. */
        task_t *prev = current_task;
        task_t *t    = current_task->next;
        while (t != current_task) {
            if (t->state == TASK_ZOMBIE && t->parent_pid == current_task->pid) {
                /* Reap: splice out of the list. */
                prev->next = t->next;

                int32_t  pid  = (int32_t)t->pid;
                int32_t  code = t->exit_code;
                uint8_t *stk  = t->stack;

                __asm__ volatile ("sti");

                if (exit_code_out) *exit_code_out = code;
                if (stk) kfree(stk);
                kfree(t);
                return pid;
            }
            prev = t;
            t    = t->next;
        }

        /* Check whether any live children exist. */
        int has_child = 0;
        t = current_task->next;
        while (t != current_task) {
            if (t->parent_pid == current_task->pid) { has_child = 1; break; }
            t = t->next;
        }

        if (!has_child) {
            __asm__ volatile ("sti");
            return -1;
        }

        /* Block until a child becomes zombie (sched_tick / sched_exit will wake us). */
        current_task->waiting = 1;
        current_task->state   = TASK_BLOCKED;
        schedule();
        __asm__ volatile ("sti");
        /* Loop and re-scan. */
    }
}
