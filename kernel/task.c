#include "task.h"
#include "heap.h"
#include "paging.h"
#include <stdint.h>

/* task_trampoline is defined in sched.c.  First code run by a kernel task:
   enables interrupts, calls task->entry(), then calls sched_exit(). */
extern void task_trampoline(void);

/* user_task_trampoline is defined in sched.c.  First code run by a user task:
   enables interrupts, then calls user_enter(eip, esp) → iret to ring 3. */
extern void user_task_trampoline(void);

#define KERNEL_OFFSET 0xC0000000u

/* ── Compile-time layout verification ───────────────────────────────────────
 *
 * task_switch.asm uses hardcoded byte offsets into task_t.  These asserts
 * catch any accidental drift between the C struct and the NASM constants.
 */
_Static_assert(__builtin_offsetof(task_t, esp)      ==  8,
    "task_t.esp offset mismatch — update TASK_ESP in task_switch.asm");
_Static_assert(__builtin_offsetof(task_t, page_dir) == 16,
    "task_t.page_dir offset mismatch — update TASK_PAGEDIR in task_switch.asm");

/* ── Constants ──────────────────────────────────────────────────────────── */

#define KSTACK_SIZE  8192u   /* 8 KB kernel stack per task */

/* ── Globals ─────────────────────────────────────────────────────────────── */

task_t *current_task = NULL;

static task_t    boot_task;          /* PCB for the initial boot context  */
static uint32_t  next_pid = 1;       /* monotonically increasing PID counter */

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * task_init — bootstrap the task subsystem.
 *
 * The boot context (kmain) is already running; wrap it in a task_t so that
 * task_switch() can save its state when we switch away from it.
 *
 * Fields that cannot be known at init time (esp, stack size) are zero or NULL;
 * task_switch() will populate esp the first time it switches away from this task.
 */
void task_init(void)
{
    boot_task.pid      = 0;
    boot_task.state    = TASK_RUNNING;
    boot_task.esp      = 0;      /* will be written by task_switch on first switch-out */
    boot_task.stack    = NULL;   /* boot stack lives in BSS, not heap-allocated */
    boot_task.page_dir = paging_physdir();
    boot_task.next     = NULL;

    current_task = &boot_task;
}

/*
 * task_create — allocate and initialise a new task.
 *
 * Sets up an initial kernel stack frame that makes the task appear to
 * task_switch() as if it had previously called task_switch() itself.
 *
 * task_switch's "ret" will land in task_trampoline (defined in sched.c),
 * which: (1) re-enables interrupts (IF was cleared by the timer IRQ that
 * triggered the first switch), (2) calls task->entry(), (3) calls
 * sched_exit() if entry returns.
 *
 * Initial stack layout (grows downward, highest address first):
 *
 *   [sp+16]  task_trampoline  ← task_switch's "ret" jumps here
 *   [sp+12]  0                ← ebp
 *   [sp+ 8]  0                ← esi
 *   [sp+ 4]  0                ← edi
 *   [sp+ 0]  0                ← ebx   ← task->esp points here
 */
task_t *task_create(void (*entry)(void), uint32_t page_dir)
{
    task_t *t = kmalloc(sizeof(task_t));
    if (!t) return NULL;

    uint8_t *stack = kmalloc(KSTACK_SIZE);
    if (!stack) { kfree(t); return NULL; }

    /* Build the initial register frame at the top of the stack. */
    uint32_t *sp = (uint32_t *)(stack + KSTACK_SIZE);
    *--sp = (uint32_t)task_trampoline; /* ret addr for task_switch         */
    *--sp = 0;                         /* ebp = 0                          */
    *--sp = 0;                         /* esi = 0                          */
    *--sp = 0;                         /* edi = 0                          */
    *--sp = 0;                         /* ebx = 0                          */

    t->pid        = next_pid++;
    t->state      = TASK_READY;
    t->esp        = (uint32_t)sp;
    t->stack      = stack;
    t->page_dir   = page_dir;
    t->next       = NULL;
    t->entry      = entry;
    t->kstack_top = 0;    /* kernel task — no TSS update needed */
    t->user_eip   = 0;
    t->user_esp   = 0;
    for (int i = 0; i < TASK_FD_MAX; i++) t->fd_table[i].used = 0;
    t->wakeup_tick  = 0;
    t->user_brk     = 0;
    t->user_eax     = 0;
    t->parent_pid   = current_task ? current_task->pid : 0;
    t->exit_code    = 0;
    t->pending_sigs = 0;
    t->waiting      = 0;

    return t;
}

/*
 * task_create_user — allocate and initialise a new user-mode task.
 *
 * Identical initial kernel stack frame to task_create(), but the "ret"
 * address points to user_task_trampoline instead of task_trampoline.
 * The trampoline calls user_enter(user_eip, user_esp) which executes
 * iret and never returns to ring 0 (until the task does a syscall or
 * is preempted by a timer interrupt).
 *
 *   eip      — virtual user-mode entry point
 *   user_esp — initial user-mode stack pointer
 *   pd       — page directory returned by uvm_create()
 *              (virtual address == physical address via identity map)
 */
task_t *task_create_user(uint32_t eip, uint32_t user_esp, uint32_t *pd)
{
    task_t *t = kmalloc(sizeof(task_t));
    if (!t) return NULL;

    uint8_t *kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) { kfree(t); return NULL; }

    /* Initial kernel stack: task_switch ret → user_task_trampoline */
    uint32_t *sp = (uint32_t *)(kstack + KSTACK_SIZE);
    *--sp = (uint32_t)user_task_trampoline;
    *--sp = 0;   /* ebp */
    *--sp = 0;   /* esi */
    *--sp = 0;   /* edi */
    *--sp = 0;   /* ebx */

    t->pid        = next_pid++;
    t->state      = TASK_READY;
    t->esp        = (uint32_t)sp;
    t->stack      = kstack;
    /*
     * pd is a virtual address == physical address (PMM page, identity-mapped).
     * CR3 needs the physical address.
     */
    t->page_dir   = (uint32_t)pd;
    t->next       = NULL;
    t->entry      = NULL;
    t->kstack_top = (uint32_t)(kstack + KSTACK_SIZE);
    t->user_eip   = eip;
    t->user_esp   = user_esp;
    for (int i = 0; i < TASK_FD_MAX; i++) t->fd_table[i].used = 0;
    t->wakeup_tick  = 0;
    t->user_brk     = 0x10000000u;
    t->user_eax     = 0;
    t->parent_pid   = current_task ? current_task->pid : 0;
    t->exit_code    = 0;
    t->pending_sigs = 0;
    t->waiting      = 0;

    return t;
}
