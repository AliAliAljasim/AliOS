#pragma once

#include <stdint.h>

/* ── Task states ─────────────────────────────────────────────────────────────
 *
 *  READY   — on the run queue, waiting for the CPU
 *  RUNNING — currently executing (only one task at a time on UP)
 *  BLOCKED — waiting for an event (I/O, sleep, lock, …)
 *  ZOMBIE  — exited but not yet reaped by parent
 */
typedef enum {
    TASK_READY   = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_ZOMBIE  = 3,
} task_state_t;

/* ── Per-task file descriptor ────────────────────────────────────────────────
 *
 * Fds 0/1/2 (stdin/stdout/stderr) may be redirected to pipes by the shell.
 * Fds 3+ are opened by sys_open and map to AliFS files or pipes.
 */
#define TASK_FD_MAX  8

/* fd type discriminator */
#define FD_FILE  1   /* backed by an AliFS file  */
#define FD_PIPE  2   /* backed by a kernel pipe  */

typedef struct {
    int      used;        /* 1 = open, 0 = closed                           */
    int      type;        /* FD_FILE or FD_PIPE                             */
    /* FD_FILE fields */
    int      alfs_idx;    /* AliFS directory index                          */
    uint32_t offset;      /* current sequential read position (bytes)       */
    /* FD_PIPE fields */
    int      pipe_idx;    /* index into the kernel pipe pool (pipe.c)       */
    int      pipe_write;  /* 1 = write end, 0 = read end                    */
} task_fd_t;

/* ── Process Control Block ───────────────────────────────────────────────────
 *
 * Field offsets must match the constants in task_switch.asm.
 * A _Static_assert in task.c verifies them at compile time.
 *
 *  Offset  Field
 *  ------  --------------------------------------------------------
 *    0     pid        — unique process ID
 *    4     state      — task_state_t (READY / RUNNING / …)
 *    8     esp        — saved kernel-stack pointer (only valid when not RUNNING)
 *   12     stack      — base of heap-allocated kernel stack (NULL for boot task)
 *   16     page_dir   — physical address of page directory (loaded into CR3)
 *   20     next       — intrusive singly-linked list for the scheduler run queue
 *   24     entry      — entry function, called by task_trampoline (kernel tasks)
 *   28     kstack_top — top of this task's kernel stack; if non-zero, the
 *                       scheduler updates TSS.esp0 before running this task so
 *                       ring-3 → ring-0 transitions land on the right stack
 *   32     user_eip   — initial ring-3 instruction pointer (user tasks only)
 *   36     user_esp   — initial ring-3 stack pointer      (user tasks only)
 */
typedef struct task {
    uint32_t      pid;       /*  0 */
    task_state_t  state;     /*  4 */
    uint32_t      esp;       /*  8  — saved kernel-stack pointer */
    uint8_t      *stack;     /* 12  — heap allocation; NULL for the boot task */
    uint32_t      page_dir;  /* 16  — physical address of page directory */
    struct task  *next;      /* 20  — run-queue link */
    void        (*entry)(void); /* 24 — kernel-task entry (NULL for user tasks) */
    uint32_t      kstack_top;   /* 28 — kernel-stack top for TSS; 0 = kernel task */
    uint32_t      user_eip;     /* 32 — initial ring-3 EIP (user tasks only) */
    uint32_t      user_esp;     /* 36 — initial ring-3 ESP (user tasks only) */
    /* Fields below are beyond the offsets checked by _Static_assert — safe to extend. */
    task_fd_t     fd_table[TASK_FD_MAX]; /* per-task open file descriptors              */
    uint32_t      wakeup_tick;           /* timer tick to unblock at (0 = not sleeping) */
    uint32_t      user_brk;             /* user-space heap break pointer               */
    uint32_t      user_eax;             /* EAX set on first user-mode entry (fork: 0)  */
    uint32_t      parent_pid;           /* PID of the task that created this one       */
    int32_t       exit_code;            /* exit status, set before TASK_ZOMBIE         */
    uint32_t      pending_sigs;         /* signal bitmask (bit N = signal N pending)   */
    int           waiting;              /* 1 while blocked in sched_wait()             */
    char          name[16];            /* human-readable name (for ps, debug)         */
    char          cwd[64];             /* current working directory (absolute path)   */
} task_t;

/* ── Globals ─────────────────────────────────────────────────────────────── */

/* The currently executing task.  Updated by task_switch(). */
extern task_t *current_task;

/* ── API ─────────────────────────────────────────────────────────────────── */

/*
 * Initialise the task subsystem.
 *
 * Creates a task_t for the boot context so that it can be switched away from
 * cleanly.  Must be called after paging_init() (needs paging_physdir()).
 */
void task_init(void);

/*
 * Allocate and initialise a new kernel task.
 *
 *   entry    — function the task will execute when first scheduled
 *   page_dir — physical address of the page directory to load into CR3;
 *              pass paging_physdir() to share the kernel's page directory
 *
 * Returns a pointer to the new task_t (in TASK_READY state), or NULL on OOM.
 * The caller is responsible for adding it to a run queue.
 */
task_t *task_create(void (*entry)(void), uint32_t page_dir);

/*
 * Allocate and initialise a new USER-MODE task.
 *
 *   eip      — virtual address of the first user-mode instruction
 *   user_esp — initial user-mode stack pointer
 *   pd       — virtual address of the user page directory (returned by
 *              uvm_create(); physical = same value via identity map)
 *
 * Returns a pointer to the new task_t (TASK_READY), or NULL on OOM.
 * Add to the run queue with sched_add().
 *
 * The task's TSS.esp0 will be set to kstack_top before it first runs,
 * ensuring ring-3 → ring-0 transitions (syscalls, interrupts) use its
 * private kernel stack rather than whoever's stack is currently in the TSS.
 */
task_t *task_create_user(uint32_t eip, uint32_t user_esp, uint32_t *pd);

/*
 * Low-level context switch (implemented in task_switch.asm).
 *
 * Saves the callee-saved registers of *prev onto its kernel stack, stores
 * ESP in prev->esp, then restores from next->esp and returns into next's
 * saved instruction pointer.  Switches CR3 if the page directories differ.
 * Updates current_task = next.
 *
 * Must be called with interrupts disabled or from within the scheduler.
 */
void task_switch(task_t *prev, task_t *next);
