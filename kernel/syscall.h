#pragma once

#include <stdint.h>

/* ── Syscall numbers ─────────────────────────────────────────────────────────
 *
 * Convention (Linux i386-compatible):
 *   eax = syscall number
 *   ebx = arg1,  ecx = arg2,  edx = arg3
 *   Return value in eax (negative errno on error).
 *
 * Invoke from user mode:  int 0x80
 */
#define SYS_WRITE   0   /* write(fd, buf, len)       → bytes written or -1       */
#define SYS_EXIT    1   /* exit(code)                → never returns             */
#define SYS_OPEN    2   /* open(name, flags)         → fd (≥3) or -1             */
#define SYS_READ    3   /* read(fd, buf, len)        → bytes read, 0=EOF, -1=err */
#define SYS_CLOSE   4   /* close(fd)                 → 0 or -1                   */
#define SYS_BRK     5   /* brk(new_brk)              → new brk or -1; 0=query    */
#define SYS_SLEEP   6   /* sleep(ms)                 → 0 (blocks for ~ms ms)     */
#define SYS_EXEC    7   /* exec(name)                → replaces image; no return */
#define SYS_FORK    8   /* fork()                    → child pid / 0 in child    */
#define SYS_WAIT    9   /* wait(exit_code_ptr)       → child pid or -1           */
#define SYS_KILL      10  /* kill(pid, sig)            → 0 or -1                   */
#define SYS_GETPID    11  /* getpid()                  → current PID               */
#define SYS_GETPPID   12  /* getppid()                 → parent PID                */
#define SYS_SETCOLOR  13  /* setcolor(fg, bg)          → 0                         */
#define SYS_PIPE      14  /* pipe(fds[2])              → 0 or -1                   */
#define SYS_OPEN_W    15  /* open_w(name, append)      → fd (≥3) or -1; creates file */

/* Install the int 0x80 handler and register vector 128 in the IDT.
   Call after idt_init(). */
void syscall_init(void);
