#pragma once

/* shell_run — interactive command shell; never returns.
 *
 * Features:
 *   - Line editing: left/right cursor, backspace, delete, home, end.
 *   - Command history: up/down arrows cycle through previous commands.
 *   - Built-in commands: help, clear, ls, run <name>, mem.
 *
 * Must be called after sched_init() and with interrupts enabled (sti),
 * because run spawns user tasks that need the scheduler.
 */
void shell_run(void);
