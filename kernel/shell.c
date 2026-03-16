#include "shell.h"
#include "keyboard.h"
#include "vga.h"
#include "alfs.h"
#include "elf.h"
#include "pmm.h"
#include "paging.h"
#include "uvm.h"
#include "task.h"
#include "sched.h"
#include "heap.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

/* ── Ctrl-C support ──────────────────────────────────────────────────────── */

static task_t *foreground = NULL;

static void ctrlc_handler(void)
{
    sched_ctrlc();        /* sends SIGKILL to sched's foreground_task */
    /* Also echo ^C so the terminal doesn't look frozen. */
}

/* ── Configuration ───────────────────────────────────────────────────────── */

#define PROMPT      "> "
#define PROMPT_LEN  2

/* Maximum line length; capped at one VGA row minus the prompt so we never
   wrap onto a second row (wrapping would break the \r-based redraw). */
#define LINE_MAX  (80 - PROMPT_LEN - 1)   /* 77 visible chars */

/* History ring buffer: HIST_MAX entries, each up to HIST_LEN-1 chars. */
#define HIST_MAX  16
#define HIST_LEN  128

/* ── Line buffer ─────────────────────────────────────────────────────────── */

static char line[LINE_MAX + 1]; /* +1 for null terminator when executing */
static int  pos;                /* cursor position within line (0..len)   */
static int  len;                /* number of characters in line           */
static int  prev_len;           /* length of the last rendered line       */

/* ── History ─────────────────────────────────────────────────────────────── */

static char hist[HIST_MAX][HIST_LEN];
static int  hist_head;   /* next slot to write into (wraps HIST_MAX)      */
static int  hist_count;  /* number of valid entries (≤ HIST_MAX)          */
static int  hist_bidx;   /* browse index: -1 = not browsing, else 0..count-1 */
static char saved[LINE_MAX + 1]; /* line saved when browsing started       */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Redraw the current line in place using \r to return to column 0.
 * Erases any previously-rendered tail with spaces, then repositions the
 * hardware cursor at (prompt + pos). */
static void redraw(void)
{
    vga_putchar('\r');
    vga_puts(PROMPT);
    for (int i = 0; i < len; i++)
        vga_putchar(line[i]);
    /* Erase stale characters left over from a shorter previous render. */
    for (int i = len; i < prev_len; i++)
        vga_putchar(' ');
    prev_len = len;
    vga_set_col(PROMPT_LEN + pos);
}

/* ── History helpers ─────────────────────────────────────────────────────── */

static void hist_strncpy(char *dst, const char *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void hist_push(void)
{
    if (len == 0) return;
    hist_strncpy(hist[hist_head], line, HIST_LEN);
    hist_head = (hist_head + 1) % HIST_MAX;
    if (hist_count < HIST_MAX) hist_count++;
}

/* Load history entry at browse index bidx into the line buffer. */
static void hist_load(int bidx)
{
    int slot = (hist_head - hist_count + bidx + HIST_MAX * HIST_MAX) % HIST_MAX;
    const char *src = hist[slot];
    len = 0;
    while (*src && len < LINE_MAX) line[len++] = *src++;
    pos = len;
}

static void hist_up(void)
{
    if (hist_count == 0) return;
    if (hist_bidx == -1) {
        hist_strncpy(saved, line, LINE_MAX + 1);
        hist_bidx = hist_count - 1;
    } else if (hist_bidx > 0) {
        hist_bidx--;
    } else {
        return;   /* already at oldest entry */
    }
    hist_load(hist_bidx);
    redraw();
}

static void hist_down(void)
{
    if (hist_bidx == -1) return;
    if (hist_bidx < hist_count - 1) {
        hist_bidx++;
        hist_load(hist_bidx);
    } else {
        /* Restore the edit line that was active before browsing started. */
        const char *src = saved;
        len = 0;
        while (*src && len < LINE_MAX) line[len++] = *src++;
        pos = len;
        hist_bidx = -1;
    }
    redraw();
}

/* ── String helpers (no libc) ────────────────────────────────────────────── */

static int sh_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ── Built-in commands ───────────────────────────────────────────────────── */

static void cmd_help(void)
{
    vga_puts("Commands:\n");
    vga_puts("  help          show this message\n");
    vga_puts("  clear         clear screen\n");
    vga_puts("  ls            list files on disk\n");
    vga_puts("  run <name>    load and run an ELF from disk\n");
    vga_puts("  rm <name>     delete a file from disk\n");
    vga_puts("  cp <src> <dst> copy a file on disk\n");
    vga_puts("  mem           show free physical memory\n");
    vga_puts("  shutdown      power off the machine\n");
    vga_puts("  reboot        reboot the machine\n");
}

static void cmd_clear(void)
{
    vga_clear();
}

static void cmd_ls(void)
{
    int found = 0;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        char     name[ALFS_NAME_MAX];
        uint32_t size;
        if (alfs_stat(i, name, &size)) {
            vga_puts(name);
            /* pad name to 20 chars for alignment */
            for (int j = 0; name[j]; j++) found |= 0;  /* reuse found below */
            vga_puts("  ");
            vga_printdec(size);
            vga_puts(" B\n");
            found++;
        }
    }
    if (!found)
        vga_puts("(no files)\n");
}

static void cmd_run(const char *name)
{
    if (!name || !*name) {
        vga_puts("usage: run <name>\n");
        return;
    }
    uint32_t *upd   = uvm_create();
    uint32_t  entry = 0;
    if (!upd || elf_load(upd, name, &entry) != 0) {
        vga_puts("run: cannot load '");
        vga_puts(name);
        vga_puts("'\n");
        return;
    }
    uint32_t sp = pmm_alloc();
    if (!sp) {
        vga_puts("run: out of memory\n");
        return;
    }
    uvm_map(upd, 0xBFFFF000, sp, PAGE_WRITE | PAGE_USER);
    task_t *ut = task_create_user(entry, 0xBFFFFFFC, upd);
    if (!ut) {
        vga_puts("run: failed to create task\n");
        return;
    }

    /* Register Ctrl-C handler so the user can kill the child. */
    foreground = ut;
    sched_set_foreground(ut);
    keyboard_on_ctrlc(ctrlc_handler);

    sched_add(ut);

    /* Block until the child exits (or is killed by Ctrl-C). */
    int32_t exit_code = 0;
    sched_wait(&exit_code);

    keyboard_on_ctrlc(NULL);
    sched_set_foreground(NULL);
    foreground = NULL;

    if (exit_code == -9)
        vga_puts("\n^C\n");
}

static void cmd_mem(void)
{
    uint32_t free_pages = pmm_free_pages();
    vga_puts("Free: ");
    vga_printdec(free_pages);
    vga_puts(" pages (");
    vga_printdec(free_pages * 4);
    vga_puts(" KB)\n");
}

static void cmd_rm(const char *name)
{
    if (!name || !*name) { vga_puts("usage: rm <name>\n"); return; }
    if (alfs_delete(name) < 0) {
        vga_puts("rm: '");
        vga_puts(name);
        vga_puts("' not found\n");
    }
}

/* Copy one file to another name using kmalloc for the data buffer. */
static void cmd_cp(const char *src, const char *dst)
{
    if (!src || !*src || !dst || !*dst) {
        vga_puts("usage: cp <src> <dst>\n");
        return;
    }

    int idx = alfs_find(src);
    if (idx < 0) {
        vga_puts("cp: '"); vga_puts(src); vga_puts("' not found\n");
        return;
    }

    uint32_t size = alfs_size(idx);
    /* Allocate a kernel buffer; even 0-byte files need a valid pointer. */
    uint8_t *buf = (uint8_t *)kmalloc(size + 1);
    if (!buf) { vga_puts("cp: out of memory\n"); return; }

    int got = alfs_read(idx, buf, size);
    if (got < 0) {
        kfree(buf);
        vga_puts("cp: read error\n");
        return;
    }

    if (alfs_write(dst, buf, (uint32_t)got) < 0) {
        kfree(buf);
        vga_puts("cp: write error\n");
        return;
    }

    kfree(buf);
}

static void cmd_shutdown(void)
{
    /* QEMU PIIX4 ACPI power-off: write 0x2000 to I/O port 0x604 */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    /* Fallback: halt forever */
    __asm__ volatile ("cli; hlt");
}

static void cmd_reboot(void)
{
    /* Pulse the keyboard controller reset line (8042 pin P20) */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint8_t)0x64));
    /* Fallback: triple-fault via null IDT */
    __asm__ volatile ("cli");
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile ("lidt %0; int $0" : : "m"(null_idt));
}

/* ── Command dispatch ────────────────────────────────────────────────────── */

static void shell_exec(void)
{
    line[len] = '\0';

    /* Skip leading whitespace. */
    char *p = line;
    while (*p == ' ') p++;
    if (!*p) return;

    /* Split into command word and remainder (argument string). */
    char *cmd = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = '\0';
    while (*p == ' ') p++;
    char *arg = p;

    if      (sh_streq(cmd, "help"))     cmd_help();
    else if (sh_streq(cmd, "clear"))    cmd_clear();
    else if (sh_streq(cmd, "ls"))       cmd_ls();
    else if (sh_streq(cmd, "run"))      cmd_run(arg);
    else if (sh_streq(cmd, "rm"))       cmd_rm(arg);
    else if (sh_streq(cmd, "mem"))      cmd_mem();
    else if (sh_streq(cmd, "shutdown")) cmd_shutdown();
    else if (sh_streq(cmd, "reboot"))   cmd_reboot();
    else if (sh_streq(cmd, "cp")) {
        /* Split arg into two words: src and dst. */
        char *s = arg;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = '\0';
        while (*s == ' ') s++;
        cmd_cp(arg, s);
    } else {
        vga_puts("unknown command: '");
        vga_puts(cmd);
        vga_puts("'  (type 'help')\n");
    }
}

/* ── Main shell loop ─────────────────────────────────────────────────────── */

void shell_run(void)
{
    /* Initialise all state. */
    pos = 0; len = 0; prev_len = 0;
    hist_head = 0; hist_count = 0; hist_bidx = -1;

    vga_puts(PROMPT);

    for (;;) {
        char c = keyboard_getchar();
        if (!c) continue;   /* nothing in the ring buffer yet */

        /* ── Navigation / editing ─────────────────────────────────────── */

        if (c == KEY_LEFT) {
            if (pos > 0) { pos--; vga_set_col(PROMPT_LEN + pos); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (pos < len) { pos++; vga_set_col(PROMPT_LEN + pos); }
            continue;
        }
        if (c == KEY_HOME) {
            pos = 0; vga_set_col(PROMPT_LEN);
            continue;
        }
        if (c == KEY_END) {
            pos = len; vga_set_col(PROMPT_LEN + len);
            continue;
        }
        if (c == KEY_UP)   { hist_up();   continue; }
        if (c == KEY_DOWN) { hist_down(); continue; }

        /* ── Backspace: delete char before cursor ─────────────────────── */
        if (c == '\b') {
            if (pos > 0) {
                for (int i = pos - 1; i < len - 1; i++)
                    line[i] = line[i + 1];
                pos--;
                len--;
                redraw();
            }
            continue;
        }

        /* ── Delete: delete char at cursor ───────────────────────────── */
        if (c == KEY_DEL) {
            if (pos < len) {
                for (int i = pos; i < len - 1; i++)
                    line[i] = line[i + 1];
                len--;
                redraw();
            }
            continue;
        }

        /* ── Enter: execute ──────────────────────────────────────────── */
        if (c == '\n') {
            vga_putchar('\n');
            hist_bidx = -1;          /* stop any in-progress browse     */
            hist_push();
            shell_exec();
            pos = 0; len = 0; prev_len = 0;
            vga_puts(PROMPT);
            continue;
        }

        /* ── Printable character: insert at cursor ────────────────────── */
        if (c >= ' ' && (unsigned char)c < 0x7F) {
            if (len < LINE_MAX) {
                /* Shift everything from pos rightward to make room. */
                for (int i = len; i > pos; i--)
                    line[i] = line[i - 1];
                line[pos] = c;
                pos++;
                len++;
                redraw();
            }
            continue;
        }
    }
}
