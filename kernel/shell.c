#include "shell.h"
#include "keyboard.h"
#include "vga.h"
#include "alfs.h"
#include "elf.h"
#include "nano.h"
#include "pmm.h"
#include "paging.h"
#include "uvm.h"
#include "task.h"
#include "sched.h"
#include "heap.h"
#include "io.h"
#include "timer.h"
#include "pipe.h"
#include "path.h"
#include <stdint.h>
#include <stddef.h>

/* ── Current working directory ───────────────────────────────────────────── */

static char cwd[64] = "/";

/* ── Ctrl-C support ──────────────────────────────────────────────────────── */

static task_t *foreground = NULL;

static void ctrlc_handler(void)
{
    sched_ctrlc();        /* sends SIGKILL to sched's foreground_task */
    /* Also echo ^C so the terminal doesn't look frozen. */
}

/* ── UI layout ───────────────────────────────────────────────────────────── */

#define UI_STATUS_ROW   0     /* coloured status bar                         */
#define UI_SCROLL_TOP   1     /* first row of the scrollable output area     */
#define UI_SCROLL_BOT   22    /* last  row of the scrollable output area     */
#define UI_SEP_ROW      23    /* thin separator line                         */
#define UI_INPUT_ROW    24    /* fixed input / prompt row                    */

/* Pack a VGA attribute byte from fg and bg colour indices. */
#define ATTR(fg, bg)  (unsigned char)(((bg) << 4) | ((fg) & 0x0F))

#define STATUS_ATTR  ATTR(VGA_WHITE,      VGA_BLUE)
#define SEP_ATTR     ATTR(VGA_WHITE,      VGA_DARK_GRAY)
#define INPUT_ATTR   ATTR(VGA_WHITE,      VGA_BLACK)
#define OUTPUT_ATTR  ATTR(VGA_LIGHT_GRAY, VGA_BLACK)

/* ── Configuration ───────────────────────────────────────────────────────── */

#define PROMPT      "> "
#define PROMPT_LEN  2

/* Maximum line length — fits in the input row after the prompt. */
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

/* ── UI helpers ──────────────────────────────────────────────────────────── */

static void ui_fill_row(int r, char ch, unsigned char a)
{
    for (int c = 0; c < 80; c++)
        vga_put_at(r, c, ch, a);
}

static void ui_puts_at(int r, int c, const char *s, unsigned char a)
{
    for (; *s && c < 80; s++, c++)
        vga_put_at(r, c, *s, a);
}

/* Write a decimal number into row r starting at *col, advancing *col. */
static void ui_putdec_at(int r, int *c, uint32_t n, unsigned char a)
{
    char buf[12];
    int  i = 0;
    if (n == 0) { vga_put_at(r, (*c)++, '0', a); return; }
    while (n) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) vga_put_at(r, (*c)++, buf[i], a);
}

static void ui_update_status(void)
{
    ui_fill_row(UI_STATUS_ROW, ' ', STATUS_ATTR);
    ui_puts_at(UI_STATUS_ROW, 2, "AliOS  ", STATUS_ATTR);
    ui_puts_at(UI_STATUS_ROW, 9, cwd, STATUS_ATTR);

    /* Right-side memory counter. */
    int c = 63;
    ui_puts_at(UI_STATUS_ROW, c, "Mem: ", STATUS_ATTR); c += 5;
    ui_putdec_at(UI_STATUS_ROW, &c, pmm_free_pages() / 256, STATUS_ATTR);
    ui_puts_at(UI_STATUS_ROW, c, " MB free", STATUS_ATTR);
}

static void ui_draw_separator(void)
{
    ui_fill_row(UI_SEP_ROW, '\xC4', SEP_ATTR);   /* CP437 horizontal line */
    ui_puts_at(UI_SEP_ROW, 1, " AliOS Shell ", SEP_ATTR);
}

/* Redraw the input row and position the hardware cursor there. */
static void redraw(void)
{
    ui_fill_row(UI_INPUT_ROW, ' ', INPUT_ATTR);
    vga_put_at(UI_INPUT_ROW, 0, '>', INPUT_ATTR);
    vga_put_at(UI_INPUT_ROW, 1, ' ', INPUT_ATTR);
    for (int i = 0; i < len; i++)
        vga_put_at(UI_INPUT_ROW, PROMPT_LEN + i, line[i], INPUT_ATTR);
    prev_len = len;
    vga_set_cursor_at(UI_INPUT_ROW, PROMPT_LEN + pos);
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

static int sh_hasprefix(const char *s, const char *pre)
{
    while (*pre) { if (*s++ != *pre++) return 0; }
    return 1;
}

static int sh_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void sh_strlcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Simple glob matcher: supports * (any sequence) and ? (any single char). */
static int sh_glob(const char *pat, const char *str)
{
    while (*pat && *str) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            while (*str) { if (sh_glob(pat, str)) return 1; str++; }
            return 0;
        }
        if (*pat == '?' || *pat == *str) { pat++; str++; }
        else return 0;
    }
    while (*pat == '*') pat++;
    return !*pat && !*str;
}

/* ── I/O redirection ─────────────────────────────────────────────────────── */

/* Scan a mutable command string for >, >>, < operators.
 * Filenames are copied into static buffers; operators + filenames are
 * blanked with spaces so the remaining string can be tokenised normally. */
static char redir_in_buf[64];
static char redir_out_buf[64];

static void strip_redirects(char *s,
                             const char **in_f,
                             const char **out_f,
                             int         *out_append)
{
    *in_f = NULL; *out_f = NULL; *out_append = 0;
    redir_in_buf[0] = redir_out_buf[0] = '\0';

    int slen = sh_strlen(s);
    for (int i = 0; i < slen; ) {
        int op = 0;   /* 1=stdin  2=stdout-overwrite  3=stdout-append */
        if (i + 1 < slen && s[i] == '>' && s[i + 1] == '>') {
            s[i] = s[i + 1] = ' '; i += 2; op = 3;
        } else if (s[i] == '>') {
            s[i] = ' '; i++; op = 2;
        } else if (s[i] == '<') {
            s[i] = ' '; i++; op = 1;
        } else {
            i++; continue;
        }
        while (i < slen && s[i] == ' ') i++;   /* skip whitespace */
        int start = i;
        while (i < slen && s[i] != ' ') i++;   /* find end of filename */
        int flen = i - start;
        if (flen <= 0) continue;
        /* Copy filename to buffer and blank it in source. */
        char *dst = (op == 1) ? redir_in_buf : redir_out_buf;
        int   cap = (flen < 63) ? flen : 63;
        for (int j = 0; j < cap; j++) { dst[j] = s[start + j]; s[start + j] = ' '; }
        dst[cap] = '\0';
        if (op == 1) { *in_f  = redir_in_buf; }
        else         { *out_f = redir_out_buf; *out_append = (op == 3); }
    }
}

/* Open a file for writing on fd 0/1/2 of task t (kernel-side, no syscall). */
static int redir_setup_write(task_t *t, int fd, const char *path, int append)
{
    char resolved[64];
    path_resolve(cwd, path, resolved);

    uint32_t start_off = 0;
    if (append) {
        int idx = alfs_find(resolved);
        if (idx >= 0) start_off = alfs_size(idx);
        else append = 0;
    }
    if (!append) {
        if (alfs_write(resolved, "", 0) < 0) return -1;
    }
    int idx = alfs_find(resolved);
    if (idx < 0) return -1;

    t->fd_table[fd].used     = 1;
    t->fd_table[fd].type     = FD_FILE;
    t->fd_table[fd].alfs_idx = idx;
    t->fd_table[fd].offset   = start_off;
    return 0;
}

static int redir_setup_read(task_t *t, int fd, const char *path)
{
    char resolved[64];
    path_resolve(cwd, path, resolved);
    int idx = alfs_find(resolved);
    if (idx < 0 || alfs_is_dir(idx)) return -1;

    t->fd_table[fd].used     = 1;
    t->fd_table[fd].type     = FD_FILE;
    t->fd_table[fd].alfs_idx = idx;
    t->fd_table[fd].offset   = 0;
    return 0;
}

/* ── Tab completion ──────────────────────────────────────────────────────── */

#define TAB_MAX 24
static char tab_matches[TAB_MAX][ALFS_NAME_MAX];
static int  tab_count;

static void tab_complete(void)
{
    /* Find start of the word being completed. */
    int ws = pos;
    while (ws > 0 && line[ws - 1] != ' ') ws--;
    int prefix_len = pos - ws;
    char prefix[80];
    for (int i = 0; i < prefix_len; i++) prefix[i] = line[ws + i];
    prefix[prefix_len] = '\0';

    /* First token? */
    int is_cmd = 1;
    for (int i = 0; i < ws; i++)
        if (line[i] != ' ') { is_cmd = 0; break; }

    /* Determine the directory to search and the typed leaf length. */
    char dir[64];
    int  leaf_len;

    if (is_cmd) {
        dir[0] = '\0';          /* sentinel: search builtins + /bin */
        leaf_len = prefix_len;
    } else {
        int slash = -1;
        for (int i = prefix_len - 1; i >= 0; i--)
            if (prefix[i] == '/') { slash = i; break; }
        if (slash < 0) {
            path_resolve(cwd, ".", dir);
            leaf_len = prefix_len;
        } else {
            char dpart[64];
            int dl = (slash + 1 < 63) ? slash + 1 : 63;
            for (int i = 0; i < dl; i++) dpart[i] = prefix[i];
            dpart[dl] = '\0';
            path_resolve(cwd, dpart, dir);
            leaf_len = prefix_len - slash - 1;
        }
    }
    const char *leaf_prefix = prefix + (prefix_len - leaf_len);

    /* Collect matches. */
    tab_count = 0;

    if (is_cmd) {
        static const char *builtins[] = {
            "help","clear","ls","cd","pwd","mkdir","rmdir","touch","rm","cp",
            "run","ps","kill","mem","df","uname","uptime","nano","shutdown","reboot",NULL
        };
        for (int i = 0; builtins[i] && tab_count < TAB_MAX; i++)
            if (sh_hasprefix(builtins[i], leaf_prefix))
                sh_strlcpy(tab_matches[tab_count++], builtins[i], ALFS_NAME_MAX);
        /* /bin executables */
        for (int i = 0; i < ALFS_MAX_FILES && tab_count < TAB_MAX; i++) {
            char name[ALFS_NAME_MAX]; uint32_t sz;
            if (!alfs_stat(i, name, &sz) || alfs_is_dir(i)) continue;
            char par[64]; path_parent(name, par);
            if (!sh_streq(par, "/bin")) continue;
            const char *lf = path_basename(name);
            if (!sh_hasprefix(lf, leaf_prefix)) continue;
            /* Don't duplicate a builtin already in the list. */
            int dup = 0;
            for (int j = 0; j < tab_count; j++)
                if (sh_streq(tab_matches[j], lf)) { dup = 1; break; }
            if (!dup) sh_strlcpy(tab_matches[tab_count++], lf, ALFS_NAME_MAX);
        }
    } else {
        for (int i = 0; i < ALFS_MAX_FILES && tab_count < TAB_MAX; i++) {
            char name[ALFS_NAME_MAX]; uint32_t sz;
            if (!alfs_stat(i, name, &sz)) continue;
            char par[64]; path_parent(name, par);
            if (!sh_streq(par, dir)) continue;
            const char *lf = path_basename(name);
            if (!sh_hasprefix(lf, leaf_prefix)) continue;
            int end = 0;
            while (lf[end] && end < ALFS_NAME_MAX - 2)
                { tab_matches[tab_count][end] = lf[end]; end++; }
            if (alfs_is_dir(i)) tab_matches[tab_count][end++] = '/';
            tab_matches[tab_count][end] = '\0';
            tab_count++;
        }
    }

    if (tab_count == 0) return;

    /* Find longest common prefix among all matches (starting from leaf_len). */
    int common = sh_strlen(tab_matches[0]);
    for (int i = 1; i < tab_count; i++) {
        int j = leaf_len;
        while (j < common && tab_matches[0][j] == tab_matches[i][j]) j++;
        common = j;
    }

    /* Insert the common suffix (if any). */
    int insert_len = common - leaf_len;
    if (insert_len > 0) {
        int add_space = (tab_count == 1 &&
                         tab_matches[0][sh_strlen(tab_matches[0]) - 1] != '/') ? 1 : 0;
        int total = insert_len + add_space;
        if (len + total > LINE_MAX) goto show_list;
        for (int i = len; i >= pos; i--) line[i + total] = line[i];
        for (int i = 0; i < insert_len; i++) line[pos + i] = tab_matches[0][leaf_len + i];
        if (add_space) line[pos + insert_len] = ' ';
        pos += total;
        len += total;
        redraw();
    }

show_list:
    /* Show all candidates if still ambiguous. */
    if (tab_count > 1) {
        vga_puts("\n");
        for (int i = 0; i < tab_count; i++) {
            vga_puts(tab_matches[i]);
            vga_puts("  ");
        }
        vga_puts("\n");
        redraw();
    }
}

/* ── Built-in commands ───────────────────────────────────────────────────── */

/* ── Shared helper: split a mutable string into tokens ───────────────────── */

static int tokenise(char *s, const char **out, int max_tokens)
{
    int n = 0;
    while (*s && n < max_tokens) {
        while (*s == ' ') s++;
        if (!*s) break;
        out[n++] = s;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = '\0';
    }
    return n;
}

/* Copy at most 15 chars of src into task name field. */
static void set_task_name(task_t *t, const char *src)
{
    int i = 0;
    while (src[i] && i < 15) { t->name[i] = src[i]; i++; }
    t->name[i] = '\0';
}

/* ── Built-in commands ───────────────────────────────────────────────────── */

static void cmd_help(void)
{
    vga_puts("Commands:\n");
    vga_puts("  help               show this message\n");
    vga_puts("  clear              clear screen\n");
    vga_puts("  ls [path]          list directory contents\n");
    vga_puts("  cd <path>          change directory\n");
    vga_puts("  pwd                print working directory\n");
    vga_puts("  mkdir <name>       create a directory\n");
    vga_puts("  rmdir <name>       remove an empty directory\n");
    vga_puts("  touch <name>       create an empty file\n");
    vga_puts("  rm <name>          delete a file\n");
    vga_puts("  cp <src> <dst>     copy a file\n");
    vga_puts("  mv <src> <dst>     move / rename a file\n");
    vga_puts("  run <name> [args]  run an ELF program\n");
    vga_puts("  ps                 list all tasks\n");
    vga_puts("  top                real-time process viewer\n");
    vga_puts("  htop               enhanced process viewer\n");
    vga_puts("  kill <pid>         send SIGKILL to a process\n");
    vga_puts("  killall <name>     kill all processes by name\n");
    vga_puts("  mem                show free physical memory\n");
    vga_puts("  df                 show disk usage\n");
    vga_puts("  uname              print system information\n");
    vga_puts("  uptime             show seconds since boot\n");
    vga_puts("  stat <file>        show file size and type\n");
    vga_puts("  history            show command history\n");
    vga_puts("  date               show current date and time (RTC)\n");
    vga_puts("  sleep <secs>       pause for N seconds\n");
    vga_puts("  find [path] [-name pat]  search for files\n");
    vga_puts("  which <cmd>        show path or 'built-in'\n");
    vga_puts("  nano [file]        open text editor\n");
    vga_puts("  shutdown           power off\n");
    vga_puts("  reboot             reboot\n");
}

static void cmd_pwd(void)
{
    vga_puts(cwd);
    vga_puts("\n");
}

static void cmd_cd(const char *arg)
{
    if (!arg || !*arg || sh_streq(arg, "/")) {
        cwd[0] = '/'; cwd[1] = '\0';
        current_task->cwd[0] = '/'; current_task->cwd[1] = '\0';
        return;
    }
    char newpath[64];
    path_resolve(cwd, arg, newpath);
    /* Root is always valid. */
    if (newpath[0] == '/' && newpath[1] == '\0') {
        cwd[0] = '/'; cwd[1] = '\0';
        current_task->cwd[0] = '/'; current_task->cwd[1] = '\0';
        return;
    }
    int idx = alfs_find(newpath);
    if (idx < 0 || !alfs_is_dir(idx)) {
        vga_puts("cd: not a directory: "); vga_puts(newpath); vga_puts("\n");
        return;
    }
    for (int i = 0; i < 64; i++) cwd[i] = newpath[i];
    for (int i = 0; i < 64; i++) current_task->cwd[i] = newpath[i];
}

static void cmd_mkdir(const char *arg)
{
    if (!arg || !*arg) { vga_puts("usage: mkdir <name>\n"); return; }
    char path[64]; path_resolve(cwd, arg, path);
    if (alfs_find(path) >= 0) {
        vga_puts("mkdir: already exists: "); vga_puts(path); vga_puts("\n");
        return;
    }
    if (alfs_mkdir(path) < 0)
        vga_puts("mkdir: failed\n");
}

static void cmd_rmdir(const char *arg)
{
    if (!arg || !*arg) { vga_puts("usage: rmdir <name>\n"); return; }
    char path[64]; path_resolve(cwd, arg, path);
    int idx = alfs_find(path);
    if (idx < 0 || !alfs_is_dir(idx)) {
        vga_puts("rmdir: not a directory: "); vga_puts(path); vga_puts("\n");
        return;
    }
    /* Refuse if any entry has this path as parent. */
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        char name[ALFS_NAME_MAX]; uint32_t sz;
        if (!alfs_stat(i, name, &sz)) continue;
        char par[64]; path_parent(name, par);
        if (sh_streq(par, path)) {
            vga_puts("rmdir: directory not empty\n"); return;
        }
    }
    if (alfs_delete(path) < 0)
        vga_puts("rmdir: failed\n");
}

static void cmd_uname(void)
{
    vga_puts("AliOS i386\n");
}

static void cmd_touch(const char *name)
{
    if (!name || !*name) { vga_puts("usage: touch <name>\n"); return; }
    char path[64]; path_resolve(cwd, name, path);
    if (alfs_find(path) >= 0) return;   /* already exists */
    if (alfs_write(path, "", 0) < 0)
        vga_puts("touch: write error\n");
}

static void cmd_df(void)
{
    int      files = 0;
    uint32_t used  = 0;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        char     n[ALFS_NAME_MAX];
        uint32_t sz;
        if (alfs_stat(i, n, &sz)) { files++; used += sz; }
    }
    vga_puts("Files: "); vga_printdec(files);
    vga_puts(" / ");     vga_printdec(ALFS_MAX_FILES); vga_puts("\n");
    vga_puts("Used:  "); vga_printdec(used / 1024);    vga_puts(" KB\n");
    vga_puts("Disk:  64 KB total\n");
}

static void cmd_kill(const char *arg)
{
    if (!arg || !*arg) { vga_puts("usage: kill <pid>\n"); return; }
    uint32_t pid = 0;
    while (*arg >= '0' && *arg <= '9') pid = pid * 10 + (uint32_t)(*arg++ - '0');
    task_t *t = current_task->next;
    while (t != current_task) {
        if (t->pid == pid) { t->pending_sigs |= (1u << 9); return; }
        t = t->next;
    }
    vga_puts("kill: no such process\n");
}

static void cmd_uptime(void)
{
    uint32_t secs = timer_ticks() / 100;
    vga_puts("Uptime: ");
    vga_printdec(secs);
    vga_puts(" seconds\n");
}

static void cmd_ps(void)
{
    static const char *state_strs[] = { "ready  ", "running", "blocked", "zombie " };
    vga_puts("PID  STATE    NAME\n");
    task_t *t = current_task;
    do {
        vga_printdec(t->pid);
        vga_puts("    ");
        int s = (int)t->state;
        vga_puts((s >= 0 && s <= 3) ? state_strs[s] : "?      ");
        vga_puts("  ");
        vga_puts(t->name[0] ? t->name : "(unnamed)");
        vga_puts("\n");
        t = t->next;
    } while (t != current_task);
}

static void cmd_clear(void)
{
    for (int r = UI_SCROLL_TOP; r <= UI_SCROLL_BOT; r++)
        ui_fill_row(r, ' ', OUTPUT_ATTR);
    vga_set_scroll_region(UI_SCROLL_TOP, UI_SCROLL_BOT);
}

static void cmd_ls(const char *arg)
{
    char dirpath[64];
    if (!arg || !*arg)
        path_resolve(cwd, ".", dirpath);
    else
        path_resolve(cwd, arg, dirpath);

    int found = 0;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        char     name[ALFS_NAME_MAX];
        uint32_t size;
        if (!alfs_stat(i, name, &size)) continue;

        /* Only show entries whose parent is dirpath. */
        char par[64]; path_parent(name, par);
        if (!sh_streq(par, dirpath)) continue;

        const char *leaf = path_basename(name);
        vga_puts(leaf);
        if (alfs_is_dir(i)) {
            vga_puts("/\n");
        } else {
            /* Pad to column 20. */
            int col = 0; for (const char *p = leaf; *p; p++) col++;
            for (; col < 20; col++) vga_putchar(' ');
            vga_printdec(size);
            vga_puts(" B\n");
        }
        found++;
    }
    if (!found)
        vga_puts("(empty)\n");
}

static void cmd_run(char *arg)
{
    if (!arg || !*arg) { vga_puts("usage: run <name> [args...]\n"); return; }

    /* Strip redirections before tokenising. */
    const char *stdin_file = NULL, *stdout_file = NULL;
    int         stdout_append = 0;
    strip_redirects(arg, &stdin_file, &stdout_file, &stdout_append);

    const char *argv[16];
    int argc = tokenise(arg, argv, 16);
    if (argc == 0) { vga_puts("usage: run <name> [args...]\n"); return; }

    /* Resolve the program name.  If not found, also try /bin/<name>. */
    char resolved[64];
    path_resolve(cwd, argv[0], resolved);
    if (alfs_find(resolved) < 0) {
        char binpath[64];
        path_resolve("/bin", argv[0], binpath);
        if (alfs_find(binpath) >= 0)
            for (int i = 0; i < 64; i++) resolved[i] = binpath[i];
    }
    argv[0] = resolved;

    uint32_t *upd  = uvm_create();
    uint32_t  entry = 0;
    if (!upd || elf_load(upd, argv[0], &entry) != 0) {
        if (upd) uvm_free(upd);
        vga_puts("run: cannot load '"); vga_puts(argv[0]); vga_puts("'\n");
        return;
    }
    uint32_t sp_phys = pmm_alloc();
    if (!sp_phys) { uvm_free(upd); vga_puts("run: out of memory\n"); return; }
    uint8_t *sp_page = (uint8_t *)sp_phys;
    for (int i = 0; i < 4096; i++) sp_page[i] = 0;
    uvm_map(upd, 0xBFFFF000, sp_phys, PAGE_WRITE | PAGE_USER);

    uint32_t esp = elf_push_args(sp_page, argc, argv);
    task_t  *ut  = task_create_user(entry, esp, upd);
    if (!ut) { uvm_free(upd); vga_puts("run: failed to create task\n"); return; }
    set_task_name(ut, path_basename(argv[0]));
    for (int i = 0; i < 64; i++) ut->cwd[i] = cwd[i];

    /* Apply redirections. */
    if (stdin_file  && redir_setup_read (ut, 0, stdin_file)  < 0)
        { vga_puts("run: cannot open '"); vga_puts(stdin_file);  vga_puts("'\n"); }
    if (stdout_file && redir_setup_write(ut, 1, stdout_file, stdout_append) < 0)
        { vga_puts("run: cannot open '"); vga_puts(stdout_file); vga_puts("'\n"); }

    foreground = ut;
    sched_set_foreground(ut);
    keyboard_on_ctrlc(ctrlc_handler);
    sched_add(ut);

    int32_t exit_code = 0;
    sched_wait(&exit_code);

    keyboard_on_ctrlc(NULL);
    sched_set_foreground(NULL);
    foreground = NULL;

    if (exit_code == -9) vga_puts("\n^C\n");
}

/* ── Pipe: run two ELFs connected by a kernel pipe ──────────────────────── */

static void cmd_pipe(char *left, char *right)
{
    /* Strip redirections from each side independently. */
    const char *l_in  = NULL, *l_out = NULL; int l_app = 0;
    const char *r_in  = NULL, *r_out = NULL; int r_app = 0;
    strip_redirects(left,  &l_in, &l_out, &l_app);
    strip_redirects(right, &r_in, &r_out, &r_app);

    const char *largs[16]; int lc = tokenise(left,  largs, 16);
    const char *rargs[16]; int rc = tokenise(right, rargs, 16);

    if (lc == 0) { vga_puts("pipe: empty left command\n");  return; }
    if (rc == 0) { vga_puts("pipe: empty right command\n"); return; }

    /* Resolve program names (try /bin/ fallback). */
    static char lres[64], rres[64];
    path_resolve(cwd, largs[0], lres);
    if (alfs_find(lres) < 0) { char t[64]; path_resolve("/bin", largs[0], t); if (alfs_find(t) >= 0) for (int i=0;i<64;i++) lres[i]=t[i]; }
    path_resolve(cwd, rargs[0], rres);
    if (alfs_find(rres) < 0) { char t[64]; path_resolve("/bin", rargs[0], t); if (alfs_find(t) >= 0) for (int i=0;i<64;i++) rres[i]=t[i]; }
    largs[0] = lres; rargs[0] = rres;

    int pi = pipe_alloc();
    if (pi < 0) { vga_puts("pipe: no free pipe slots\n"); return; }

    /* ── Left (writer) ─────────────────────────────────── */
    uint32_t *lupd = uvm_create(); uint32_t lentry = 0;
    if (!lupd || elf_load(lupd, largs[0], &lentry) != 0) {
        if (lupd) uvm_free(lupd);
        vga_puts("pipe: cannot load '"); vga_puts(largs[0]); vga_puts("'\n");
        pipe_close_read(pi); pipe_close_write(pi); return;
    }
    uint32_t lsp = pmm_alloc();
    if (!lsp) {
        uvm_free(lupd);
        vga_puts("pipe: out of memory\n");
        pipe_close_read(pi); pipe_close_write(pi); return;
    }
    { uint8_t *p = (uint8_t *)lsp; for (int i = 0; i < 4096; i++) p[i] = 0; }
    uvm_map(lupd, 0xBFFFF000, lsp, PAGE_WRITE | PAGE_USER);
    uint32_t lesp = elf_push_args((uint8_t *)lsp, lc, largs);
    task_t *lt = task_create_user(lentry, lesp, lupd);
    if (!lt) { uvm_free(lupd); pipe_close_read(pi); pipe_close_write(pi); return; }
    set_task_name(lt, largs[0]);
    /* Redirect stdout → pipe write end.
     * pipe_alloc set nwriters=1; that count is now "owned" by this fd. */
    lt->fd_table[1].used = 1; lt->fd_table[1].type = FD_PIPE;
    lt->fd_table[1].pipe_idx = pi; lt->fd_table[1].pipe_write = 1;

    /* ── Right (reader) ────────────────────────────────── */
    uint32_t *rupd = uvm_create(); uint32_t rentry = 0;
    if (!rupd || elf_load(rupd, rargs[0], &rentry) != 0) {
        if (rupd) uvm_free(rupd);
        vga_puts("pipe: cannot load '"); vga_puts(rargs[0]); vga_puts("'\n");
        /* lt not yet scheduled; abandon it */
        uvm_free(lupd); kfree(lt->stack); kfree(lt);
        pipe_close_read(pi); pipe_close_write(pi); return;
    }
    uint32_t rsp = pmm_alloc();
    if (!rsp) {
        uvm_free(rupd); uvm_free(lupd); kfree(lt->stack); kfree(lt);
        vga_puts("pipe: out of memory\n");
        pipe_close_read(pi); pipe_close_write(pi); return;
    }
    { uint8_t *p = (uint8_t *)rsp; for (int i = 0; i < 4096; i++) p[i] = 0; }
    uvm_map(rupd, 0xBFFFF000, rsp, PAGE_WRITE | PAGE_USER);
    uint32_t resp = elf_push_args((uint8_t *)rsp, rc, rargs);
    task_t *rt = task_create_user(rentry, resp, rupd);
    if (!rt) {
        uvm_free(rupd); uvm_free(lupd); kfree(lt->stack); kfree(lt);
        pipe_close_read(pi); pipe_close_write(pi); return;
    }
    set_task_name(rt, rargs[0]);
    /* Redirect stdin → pipe read end. */
    rt->fd_table[0].used = 1; rt->fd_table[0].type = FD_PIPE;
    rt->fd_table[0].pipe_idx = pi; rt->fd_table[0].pipe_write = 0;

    /* Apply file redirections on top of pipe setup. */
    if (l_in)  redir_setup_read (lt, 0, l_in);
    if (l_out) redir_setup_write(lt, 1, l_out, l_app);
    if (r_in)  redir_setup_read (rt, 0, r_in);
    if (r_out) redir_setup_write(rt, 1, r_out, r_app);

    /* Both tasks are children of the shell (current_task, pid 0). */
    sched_set_foreground(rt);
    keyboard_on_ctrlc(ctrlc_handler);

    sched_add(rt);   /* add reader first so it is ready to receive */
    sched_add(lt);

    int32_t code = 0;
    sched_wait(&code);   /* wait for first child to finish */
    sched_wait(&code);   /* wait for second child to finish */

    keyboard_on_ctrlc(NULL);
    sched_set_foreground(NULL);
    foreground = NULL;
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
    char path[64]; path_resolve(cwd, name, path);
    int idx = alfs_find(path);
    if (idx < 0) { vga_puts("rm: not found: "); vga_puts(path); vga_puts("\n"); return; }
    if (alfs_is_dir(idx)) { vga_puts("rm: is a directory (use rmdir)\n"); return; }
    if (alfs_delete(path) < 0)
        vga_puts("rm: delete failed\n");
}

/* Copy one file to another name using kmalloc for the data buffer. */
static void cmd_cp(const char *src, const char *dst)
{
    if (!src || !*src || !dst || !*dst) {
        vga_puts("usage: cp <src> <dst>\n");
        return;
    }

    char srcp[64], dstp[64];
    path_resolve(cwd, src, srcp);
    path_resolve(cwd, dst, dstp);

    int idx = alfs_find(srcp);
    if (idx < 0) {
        vga_puts("cp: '"); vga_puts(srcp); vga_puts("' not found\n");
        return;
    }
    src = srcp; dst = dstp;

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

/* ── top/htop helper ────────────────────────────────────────────────────── */

/* Attributes used by both top and htop. */
#define TOP_BG_ATTR   ATTR(VGA_LIGHT_GRAY, VGA_BLACK)
#define TOP_HDR_ATTR  ATTR(VGA_BLACK,      VGA_CYAN)
#define TOP_COL_ATTR  ATTR(VGA_BLACK,      VGA_LIGHT_GRAY)
#define TOP_RUN_ATTR  ATTR(VGA_WHITE,      VGA_GREEN)
#define TOP_ZOM_ATTR  ATTR(VGA_WHITE,      VGA_RED)
#define TOP_BLK_ATTR  ATTR(VGA_YELLOW,     VGA_BLACK)
#define TOP_INFO_ATTR ATTR(VGA_CYAN,       VGA_BLACK)

static void top_puts_at(int r, int c, const char *s, unsigned char a, int width)
{
    int col = c;
    for (; *s && col < c + width && col < 80; s++, col++)
        vga_put_at(r, col, *s, a);
    for (; col < c + width && col < 80; col++)
        vga_put_at(r, col, ' ', a);
}

static void top_putdec_at(int r, int *c, uint32_t n, unsigned char a)
{
    char tmp[12]; int i = 0;
    if (n == 0) { vga_put_at(r, (*c)++, '0', a); return; }
    while (n) { tmp[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) vga_put_at(r, (*c)++, tmp[i], a);
}

static void top_fill(int r, unsigned char a)
{
    for (int c = 0; c < 80; c++) vga_put_at(r, c, ' ', a);
}

static void top_draw(int htop_style)
{
    uint32_t ticks = timer_ticks();
    uint32_t secs  = ticks / 100;

    /* ── Title row ─────────────────────────────────────────────────── */
    top_fill(0, TOP_HDR_ATTR);
    top_puts_at(0, 1,  htop_style ? "htop" : "top", TOP_HDR_ATTR, 6);
    top_puts_at(0, 7,  "AliOS process viewer", TOP_HDR_ATTR, 22);
    /* Right: uptime */
    {
        int c = 52;
        top_puts_at(0, c, "up ", TOP_HDR_ATTR, 3); c += 3;
        top_putdec_at(0, &c, secs / 60, TOP_HDR_ATTR);
        vga_put_at(0, c++, 'm', TOP_HDR_ATTR);
        top_putdec_at(0, &c, secs % 60, TOP_HDR_ATTR);
        top_puts_at(0, c, "s", TOP_HDR_ATTR, 3);
    }

    /* ── System stats row ──────────────────────────────────────────── */
    top_fill(1, TOP_INFO_ATTR);
    {
        int c = 1;
        top_puts_at(1, c, "Mem free: ", TOP_INFO_ATTR, 10); c += 10;
        top_putdec_at(1, &c, pmm_free_pages() * 4, TOP_INFO_ATTR);
        top_puts_at(1, c, " KB", TOP_INFO_ATTR, 4); c += 4;
        top_puts_at(1, c, "   Tasks: ", TOP_INFO_ATTR, 10); c += 10;
        /* Count tasks in ring */
        int ntasks = 0;
        task_t *t = current_task;
        do { ntasks++; t = t->next; } while (t != current_task);
        top_putdec_at(1, &c, (uint32_t)ntasks, TOP_INFO_ATTR);
        top_puts_at(1, c, "   Press q to quit", TOP_INFO_ATTR, 20);
    }

    /* ── Column header ─────────────────────────────────────────────── */
    top_fill(2, TOP_COL_ATTR);
    top_puts_at(2,  1, "PID",   TOP_COL_ATTR, 6);
    top_puts_at(2,  7, "STATE", TOP_COL_ATTR, 9);
    top_puts_at(2, 16, "RING",  TOP_COL_ATTR, 6);
    top_puts_at(2, 22, "NAME",  TOP_COL_ATTR, 18);
    top_puts_at(2, 40, "CWD",   TOP_COL_ATTR, 40);

    /* ── Task rows ─────────────────────────────────────────────────── */
    int row = 3;
    task_t *t = current_task;
    do {
        if (row >= 23) break;

        unsigned char row_attr = TOP_BG_ATTR;
        const char *state_str  = "?      ";
        switch (t->state) {
            case TASK_READY:   state_str = "ready  "; row_attr = TOP_BG_ATTR;  break;
            case TASK_RUNNING: state_str = "running"; row_attr = TOP_RUN_ATTR; break;
            case TASK_BLOCKED: state_str = "blocked"; row_attr = TOP_BLK_ATTR; break;
            case TASK_ZOMBIE:  state_str = "zombie "; row_attr = TOP_ZOM_ATTR; break;
        }

        top_fill(row, row_attr);
        int c = 1;
        top_putdec_at(row, &c, t->pid, row_attr);
        top_puts_at(row,  7, state_str,                       row_attr, 9);
        top_puts_at(row, 16, t->kstack_top ? "user" : "krnl", row_attr, 6);
        top_puts_at(row, 22, t->name[0] ? t->name : "(unnamed)", row_attr, 18);
        top_puts_at(row, 40, t->cwd[0]  ? t->cwd  : "/",        row_attr, 40);

        row++;
        t = t->next;
    } while (t != current_task);

    /* Clear remaining rows */
    for (; row < 23; row++) top_fill(row, TOP_BG_ATTR);

    /* ── Hint row ──────────────────────────────────────────────────── */
    top_fill(23, TOP_COL_ATTR);
    top_puts_at(23, 1, "q", ATTR(VGA_WHITE, VGA_LIGHT_GRAY), 2);
    top_puts_at(23, 3, " Quit", TOP_COL_ATTR, 8);
}

static void cmd_top_impl(int htop_style)
{
    /* Clear screen and do initial draw. */
    for (int r = 0; r < 24; r++) top_fill(r, TOP_BG_ATTR);
    top_draw(htop_style);

    uint32_t last = timer_ticks();
    for (;;) {
        char c = keyboard_getchar();
        if (c == 'q' || c == 'Q' || c == '\x03') break;

        uint32_t now = timer_ticks();
        if (now - last >= 100) {    /* refresh every second */
            top_draw(htop_style);
            last = now;
        }
    }

    /* Restore shell UI. */
    vga_clear();
    vga_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    vga_set_scroll_region(UI_SCROLL_TOP, UI_SCROLL_BOT);
    ui_update_status();
    ui_draw_separator();
}

static void cmd_top(void)   { cmd_top_impl(0); }
static void cmd_htop(void)  { cmd_top_impl(1); }

static void cmd_killall(const char *name)
{
    if (!name || !*name) { vga_puts("usage: killall <name>\n"); return; }
    int killed = 0;
    task_t *t = current_task->next;
    while (t != current_task) {
        if (sh_streq(t->name, name)) {
            t->pending_sigs |= (1u << 9);
            killed++;
        }
        t = t->next;
    }
    if (!killed) { vga_puts("killall: no matching process\n"); return; }
    vga_puts("Sent signal to ");
    vga_printdec((uint32_t)killed);
    vga_puts(" process(es)\n");
}

static void cmd_find(char *arg)
{
    /* Parse: find [path] [-name pattern] */
    const char *search_root = "/";
    const char *pattern     = NULL;

    const char *toks[8];
    int ntoks = tokenise(arg, toks, 8);

    int i = 0;
    /* First token may be a path (doesn't start with '-'). */
    if (i < ntoks && toks[i][0] != '-') {
        search_root = toks[i++];
    }
    /* Resolve search root. */
    char root[64];
    path_resolve(cwd, search_root, root);

    while (i < ntoks) {
        if (sh_streq(toks[i], "-name") && i + 1 < ntoks) {
            pattern = toks[i + 1];
            i += 2;
        } else {
            vga_puts("find: unknown option: "); vga_puts(toks[i]); vga_puts("\n");
            return;
        }
    }

    int found = 0;
    for (int j = 0; j < ALFS_MAX_FILES; j++) {
        char name[ALFS_NAME_MAX]; uint32_t sz;
        if (!alfs_stat(j, name, &sz)) continue;

        /* Entry must be under root (root == "/" matches everything). */
        if (!(root[0] == '/' && root[1] == '\0')) {
            /* name must start with root + "/" */
            if (!sh_hasprefix(name, root)) continue;
            /* Avoid partial-component matches: /bin matches /bin/echo but not /binary */
            int rlen = sh_strlen(root);
            if (name[rlen] != '/' && name[rlen] != '\0') continue;
        }

        /* Apply -name filter against the leaf component. */
        if (pattern) {
            const char *leaf = path_basename(name);
            if (!sh_glob(pattern, leaf)) continue;
        }

        vga_puts(name);
        if (alfs_is_dir(j)) vga_puts("/");
        vga_puts("\n");
        found++;
    }
    if (!found) vga_puts("(no matches)\n");
}

static void cmd_which(const char *name)
{
    if (!name || !*name) { vga_puts("usage: which <command>\n"); return; }

    /* Check built-ins first. */
    static const char *builtins[] = {
        "help","clear","ls","cd","pwd","mkdir","rmdir","touch","rm","cp",
        "run","ps","top","htop","kill","killall","mem","df","uname","uptime",
        "nano","find","which","shutdown","reboot", NULL
    };
    for (int i = 0; builtins[i]; i++) {
        if (sh_streq(builtins[i], name)) {
            vga_puts(name); vga_puts(": shell built-in\n");
            return;
        }
    }

    /* Check /bin/<name>. */
    char path[64];
    path_resolve("/bin", name, path);
    if (alfs_find(path) >= 0 && !alfs_is_dir(alfs_find(path))) {
        vga_puts(path); vga_puts("\n");
        return;
    }

    vga_puts("which: "); vga_puts(name); vga_puts(": not found\n");
}

static void cmd_nano(const char *arg)
{
    /* Hand control to the full-screen editor. */
    nano_run(arg && *arg ? arg : NULL, cwd);
    /* Restore shell UI after editor exits. */
    vga_clear();
    vga_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    vga_set_scroll_region(UI_SCROLL_TOP, UI_SCROLL_BOT);
    ui_update_status();
    ui_draw_separator();
}

static void cmd_mv(const char *src, const char *dst)
{
    if (!src || !*src || !dst || !*dst) { vga_puts("usage: mv <src> <dst>\n"); return; }
    char srcp[64], dstp[64];
    path_resolve(cwd, src, srcp);
    path_resolve(cwd, dst, dstp);

    int idx = alfs_find(srcp);
    if (idx < 0) { vga_puts("mv: not found: "); vga_puts(srcp); vga_puts("\n"); return; }
    if (alfs_is_dir(idx)) { vga_puts("mv: cannot move directories\n"); return; }

    uint32_t size = alfs_size(idx);
    uint8_t *buf  = (uint8_t *)kmalloc(size + 1);
    if (!buf) { vga_puts("mv: out of memory\n"); return; }

    int got = alfs_read(idx, buf, size);
    if (got < 0) { kfree(buf); vga_puts("mv: read error\n"); return; }
    if (alfs_write(dstp, buf, (uint32_t)got) < 0) { kfree(buf); vga_puts("mv: write error\n"); return; }
    kfree(buf);
    alfs_delete(srcp);
}

static void cmd_stat(const char *arg)
{
    if (!arg || !*arg) { vga_puts("usage: stat <file>\n"); return; }
    char path[64];
    path_resolve(cwd, arg, path);
    int idx = alfs_find(path);
    if (idx < 0) { vga_puts("stat: not found: "); vga_puts(path); vga_puts("\n"); return; }

    vga_puts("  Path: "); vga_puts(path); vga_puts("\n");
    vga_puts("  Type: "); vga_puts(alfs_is_dir(idx) ? "directory" : "file"); vga_puts("\n");
    if (!alfs_is_dir(idx)) {
        vga_puts("  Size: "); vga_printdec(alfs_size(idx)); vga_puts(" B\n");
    }
}

static void cmd_history(void)
{
    if (hist_count == 0) { vga_puts("(no history)\n"); return; }
    for (int i = 0; i < hist_count; i++) {
        int slot = (hist_head - hist_count + i + HIST_MAX * HIST_MAX) % HIST_MAX;
        vga_printdec((uint32_t)(i + 1));
        vga_puts("  ");
        vga_puts(hist[slot]);
        vga_puts("\n");
    }
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

static void cmd_date(void)
{
    /* Spin until the RTC update-in-progress flag clears. */
    while (cmos_read(0x0A) & 0x80);

    uint8_t sec  = bcd2bin(cmos_read(0x00));
    uint8_t min  = bcd2bin(cmos_read(0x02));
    uint8_t hour = bcd2bin(cmos_read(0x04));
    uint8_t mday = bcd2bin(cmos_read(0x07));
    uint8_t mon  = bcd2bin(cmos_read(0x08));
    uint8_t year = bcd2bin(cmos_read(0x09));

    /* Print YYYY-MM-DD HH:MM:SS UTC */
    vga_puts("20"); vga_printdec(year); vga_putchar('-');
    if (mon  < 10) vga_putchar('0'); vga_printdec(mon);  vga_putchar('-');
    if (mday < 10) vga_putchar('0'); vga_printdec(mday); vga_putchar(' ');
    if (hour < 10) vga_putchar('0'); vga_printdec(hour); vga_putchar(':');
    if (min  < 10) vga_putchar('0'); vga_printdec(min);  vga_putchar(':');
    if (sec  < 10) vga_putchar('0'); vga_printdec(sec);
    vga_puts(" UTC\n");
}

static void cmd_sleep(const char *arg)
{
    if (!arg || !*arg) { vga_puts("usage: sleep <seconds>\n"); return; }
    uint32_t secs = 0;
    while (*arg >= '0' && *arg <= '9') secs = secs * 10 + (uint32_t)(*arg++ - '0');
    if (secs > 3600) secs = 3600;   /* cap at 1 hour */
    sched_sleep(secs * 1000u);
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

    /* ── Pipe detection: split "left | right" before tokenising ─────────── */
    char *pipe_sep = NULL;
    for (char *q = p; *q; q++) {
        if (*q == '|') { pipe_sep = q; break; }
    }
    if (pipe_sep) {
        /* Trim trailing whitespace from left side. */
        char *lt = pipe_sep - 1;
        while (lt >= p && *lt == ' ') *lt-- = '\0';
        *pipe_sep = '\0';
        char *right = pipe_sep + 1;
        while (*right == ' ') right++;
        cmd_pipe(p, right);
        return;
    }

    /* Split into command word and remainder (argument string). */
    char *cmd = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = '\0';
    while (*p == ' ') p++;
    char *arg = p;

    if      (sh_streq(cmd, "help"))     cmd_help();
    else if (sh_streq(cmd, "clear"))    cmd_clear();
    else if (sh_streq(cmd, "ls"))       cmd_ls(arg);
    else if (sh_streq(cmd, "cd"))       cmd_cd(arg);
    else if (sh_streq(cmd, "pwd"))      cmd_pwd();
    else if (sh_streq(cmd, "mkdir"))    cmd_mkdir(arg);
    else if (sh_streq(cmd, "rmdir"))    cmd_rmdir(arg);
    else if (sh_streq(cmd, "touch"))    cmd_touch(arg);
    else if (sh_streq(cmd, "run"))      cmd_run(arg);
    else if (sh_streq(cmd, "rm"))       cmd_rm(arg);
    else if (sh_streq(cmd, "ps"))       cmd_ps();
    else if (sh_streq(cmd, "top"))      cmd_top();
    else if (sh_streq(cmd, "htop"))     cmd_htop();
    else if (sh_streq(cmd, "kill"))     cmd_kill(arg);
    else if (sh_streq(cmd, "killall"))  cmd_killall(arg);
    else if (sh_streq(cmd, "mem"))      cmd_mem();
    else if (sh_streq(cmd, "df"))       cmd_df();
    else if (sh_streq(cmd, "uname"))    cmd_uname();
    else if (sh_streq(cmd, "uptime"))   cmd_uptime();
    else if (sh_streq(cmd, "stat"))     cmd_stat(arg);
    else if (sh_streq(cmd, "history"))  cmd_history();
    else if (sh_streq(cmd, "date"))     cmd_date();
    else if (sh_streq(cmd, "sleep"))    cmd_sleep(arg);
    else if (sh_streq(cmd, "find"))     cmd_find(arg);
    else if (sh_streq(cmd, "which"))    cmd_which(arg);
    else if (sh_streq(cmd, "nano"))     cmd_nano(arg);
    else if (sh_streq(cmd, "shutdown")) cmd_shutdown();
    else if (sh_streq(cmd, "reboot"))   cmd_reboot();
    else if (sh_streq(cmd, "cp")) {
        char *s = arg;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = '\0';
        while (*s == ' ') s++;
        cmd_cp(arg, s);
    } else if (sh_streq(cmd, "mv")) {
        char *s = arg;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = '\0';
        while (*s == ' ') s++;
        cmd_mv(arg, s);
    } else {
        vga_puts("unknown command: '");
        vga_puts(cmd);
        vga_puts("'  (type 'help')\n");
    }
}

/* ── Main shell loop ─────────────────────────────────────────────────────── */

void shell_run(void)
{
    /* Initialise line state. */
    pos = 0; len = 0; prev_len = 0;
    hist_head = 0; hist_count = 0; hist_bidx = -1;

    /* Draw the full UI frame. */
    vga_clear();
    vga_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
    vga_set_scroll_region(UI_SCROLL_TOP, UI_SCROLL_BOT);
    ui_update_status();
    ui_draw_separator();
    redraw();

    for (;;) {
        char c = keyboard_getchar();
        if (!c) continue;

        /* ── Navigation / editing ─────────────────────────────────────── */

        if (c == KEY_LEFT) {
            if (pos > 0) { pos--; redraw(); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (pos < len) { pos++; redraw(); }
            continue;
        }
        if (c == KEY_HOME) { pos = 0;   redraw(); continue; }
        if (c == KEY_END)  { pos = len; redraw(); continue; }
        if (c == KEY_UP)   { hist_up();   continue; }
        if (c == KEY_DOWN) { hist_down(); continue; }

        /* ── Backspace ────────────────────────────────────────────────── */
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

        /* ── Delete ───────────────────────────────────────────────────── */
        if (c == KEY_DEL) {
            if (pos < len) {
                for (int i = pos; i < len - 1; i++)
                    line[i] = line[i + 1];
                len--;
                redraw();
            }
            continue;
        }

        /* ── Enter: echo command in scroll area, then execute ─────────── */
        if (c == '\n') {
            /* Echo the entered command into the scrollable output area. */
            vga_set_color(VGA_LIGHT_GRAY, VGA_BLACK);
            vga_puts(PROMPT);
            line[len] = '\0';
            vga_puts(line);
            vga_putchar('\n');

            hist_bidx = -1;
            hist_push();
            shell_exec();

            /* Refresh status (memory may have changed) and separator. */
            ui_update_status();
            ui_draw_separator();
            pos = 0; len = 0; prev_len = 0;
            redraw();
            continue;
        }

        /* ── Tab: complete current word ──────────────────────────────── */
        if (c == '\t') { tab_complete(); continue; }

        /* ── Printable character: insert at cursor ────────────────────── */
        if (c >= ' ' && (unsigned char)c < 0x7F) {
            if (len < LINE_MAX) {
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
