/*
 * nano.c — full-screen text editor (nano-like)
 *
 * Screen layout (80×24 VGA):
 *   Rows  0-21   text content (22 visible rows)
 *   Row   22     status bar   (filename, modified, line/col)
 *   Row   23     hint bar     (^X Exit  ^S Save  ^K Cut  ^U Paste)
 *
 * Key bindings:
 *   ^X   exit (prompts if unsaved)
 *   ^S   save
 *   ^K   cut current line
 *   ^U   paste cut line
 *   Arrows / Home / End   navigate
 *   Backspace / Del       delete
 *   Enter                 insert newline
 */

#include "nano.h"
#include "vga.h"
#include "keyboard.h"
#include "alfs.h"
#include "pmm.h"
#include "path.h"
#include <stdint.h>

/* ── Layout ─────────────────────────────────────────────────────────── */

#define NANO_TEXT_ROWS  22
#define NANO_COLS       80
#define NANO_STATUS_ROW 22
#define NANO_HINT_ROW   23

/* Buffer: one PMM page = 4096 bytes; reserve 1 byte so we can always
   append a NUL without overflowing (alfs_write doesn't need NUL but
   reads into the buffer do). */
#define NANO_BUF_MAX    4095u

/* ── Attribute helpers ──────────────────────────────────────────────── */

#define ATTR(fg, bg)     ((unsigned char)(((bg) << 4) | ((fg) & 0x0F)))
#define CONTENT_ATTR     ATTR(VGA_LIGHT_GRAY, VGA_BLACK)
#define STATUS_ATTR      ATTR(VGA_BLACK,      VGA_LIGHT_GRAY)
#define HINT_ATTR        ATTR(VGA_LIGHT_GRAY, VGA_BLACK)
#define HINT_KEY_ATTR    ATTR(VGA_WHITE,      VGA_BLACK)

/* ── Ctrl-key constant ──────────────────────────────────────────────── */
#define CTRL(c) ((char)((c) & 0x1F))

/* ── Editor state (module-level so helpers don't need extra args) ───── */

static char    *nbuf;           /* text buffer (PMM page, 4096 bytes)  */
static uint32_t nlen;           /* number of bytes in buffer           */
static uint32_t npos;           /* cursor byte offset                  */
static uint32_t nscroll;        /* first visible line (0-based)        */
static int      ndirty;         /* 1 = unsaved changes                 */
static char     nfilename[64];  /* resolved absolute path, or ""       */
static char     ncut[82];       /* cut-line buffer (80 chars + \n + NUL) */
static int      ncut_len;       /* valid bytes in ncut                 */

/* ── Small string helpers ───────────────────────────────────────────── */

static int nano_strlen(const char *s)
{
    int n = 0; while (s[n]) n++; return n;
}

/* ── Buffer navigation helpers ──────────────────────────────────────── */

/* Byte offset of the start of the line containing pos. */
static uint32_t line_start(uint32_t pos)
{
    while (pos > 0 && nbuf[pos - 1] != '\n') pos--;
    return pos;
}

/* Byte offset just past the last non-newline char on the line of pos
   (i.e. points at the '\n' or at nlen). */
static uint32_t line_end(uint32_t pos)
{
    while (pos < nlen && nbuf[pos] != '\n') pos++;
    return pos;
}

/* 0-based line number of pos. */
static uint32_t count_line(uint32_t pos)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < pos; i++)
        if (nbuf[i] == '\n') n++;
    return n;
}

/* Byte offset of the start of line_no (0-based). */
static uint32_t find_line_start(uint32_t line_no)
{
    uint32_t ln = 0, off = 0;
    while (off < nlen && ln < line_no) {
        if (nbuf[off] == '\n') ln++;
        off++;
    }
    return off;
}

/* Column of npos within its line. */
static uint32_t cursor_col(void) { return npos - line_start(npos); }

/* ── Screen drawing ─────────────────────────────────────────────────── */

static void fill_row(int r, char ch, unsigned char a)
{
    for (int c = 0; c < NANO_COLS; c++) vga_put_at(r, c, ch, a);
}

static void puts_at(int r, int c, const char *s, unsigned char a)
{
    for (; *s && c < NANO_COLS; s++, c++) vga_put_at(r, c, *s, a);
}

static void putdec_at(int r, int *c, uint32_t n, unsigned char a)
{
    char tmp[12]; int i = 0;
    if (n == 0) { vga_put_at(r, (*c)++, '0', a); return; }
    while (n) { tmp[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) vga_put_at(r, (*c)++, tmp[i], a);
}

static void draw_status(void)
{
    fill_row(NANO_STATUS_ROW, ' ', STATUS_ATTR);
    /* Filename / new buffer */
    if (nfilename[0])
        puts_at(NANO_STATUS_ROW, 1, nfilename, STATUS_ATTR);
    else
        puts_at(NANO_STATUS_ROW, 1, "New Buffer", STATUS_ATTR);
    /* Modified flag */
    if (ndirty)
        puts_at(NANO_STATUS_ROW, 40, "[Modified]", STATUS_ATTR);
    /* Line / Col (right side) */
    int c = 54;
    puts_at(NANO_STATUS_ROW, c, "Ln ", STATUS_ATTR); c += 3;
    putdec_at(NANO_STATUS_ROW, &c, count_line(npos) + 1, STATUS_ATTR);
    puts_at(NANO_STATUS_ROW, c, " Col ", STATUS_ATTR); c += 5;
    putdec_at(NANO_STATUS_ROW, &c, cursor_col() + 1, STATUS_ATTR);
}

static void draw_hints(void)
{
    fill_row(NANO_HINT_ROW, ' ', HINT_ATTR);
    struct { const char *key; const char *label; } h[] = {
        {"^X", "Exit"}, {"^S", "Save"}, {"^K", "Cut"}, {"^U", "Paste"},
    };
    int c = 1;
    for (int i = 0; i < 4; i++) {
        puts_at(NANO_HINT_ROW, c, h[i].key,   HINT_KEY_ATTR); c += 2;
        vga_put_at(NANO_HINT_ROW, c++, ' ',   HINT_ATTR);
        puts_at(NANO_HINT_ROW, c, h[i].label, HINT_ATTR);
        c += nano_strlen(h[i].label) + 2;
    }
}

/* Full redraw of content rows + status + hints + hardware cursor. */
static void redraw(void)
{
    uint32_t cur_line = count_line(npos);
    uint32_t off      = find_line_start(nscroll);   /* byte at top of screen */

    int cursor_row = -1, cursor_scol = 0;

    for (int row = 0; row < NANO_TEXT_ROWS; row++) {
        fill_row(row, ' ', CONTENT_ATTR);

        uint32_t visible_line = nscroll + (uint32_t)row;

        if (visible_line == cur_line) {
            cursor_row  = row;
            uint32_t cc = cursor_col();
            cursor_scol = (int)(cc < (uint32_t)NANO_COLS ? cc : (uint32_t)(NANO_COLS - 1));
        }

        /* Render characters for this line (no wrap; truncate at col 80). */
        int col = 0;
        while (col < NANO_COLS && off <= nlen) {
            if (off == nlen) break;
            char ch = nbuf[off];
            if (ch == '\n') { off++; break; }
            vga_put_at(row, col++, ch, CONTENT_ATTR);
            off++;
        }
        /* Advance off past any remaining chars on this line (off-screen). */
        while (off < nlen && nbuf[off] != '\n') off++;
        if (off < nlen && nbuf[off] == '\n') off++;
    }

    draw_status();
    draw_hints();

    if (cursor_row >= 0)
        vga_set_cursor_at(cursor_row, cursor_scol);
    else
        vga_set_cursor_at(NANO_TEXT_ROWS - 1, 0);
}

/* Show a temporary message in the status bar (caller calls redraw() after). */
static void status_msg(const char *msg)
{
    fill_row(NANO_STATUS_ROW, ' ', STATUS_ATTR);
    puts_at(NANO_STATUS_ROW, 1, msg, STATUS_ATTR);
}

/* ── Scroll adjustment ──────────────────────────────────────────────── */

static void adjust_scroll(void)
{
    uint32_t cur = count_line(npos);
    if (cur < nscroll)
        nscroll = cur;
    else if (cur >= nscroll + NANO_TEXT_ROWS)
        nscroll = cur - NANO_TEXT_ROWS + 1;
}

/* ── Insertion / deletion ───────────────────────────────────────────── */

static void buf_insert(char c)
{
    if (nlen >= NANO_BUF_MAX) return;
    for (uint32_t i = nlen; i > npos; i--) nbuf[i] = nbuf[i - 1];
    nbuf[npos++] = c;
    nlen++;
    ndirty = 1;
}

static void buf_delete_before(void)   /* Backspace */
{
    if (npos == 0) return;
    for (uint32_t i = npos - 1; i < nlen - 1; i++) nbuf[i] = nbuf[i + 1];
    nlen--;
    npos--;
    ndirty = 1;
}

static void buf_delete_at(void)       /* Delete key */
{
    if (npos >= nlen) return;
    for (uint32_t i = npos; i < nlen - 1; i++) nbuf[i] = nbuf[i + 1];
    nlen--;
    ndirty = 1;
}

/* ── Cut / paste ────────────────────────────────────────────────────── */

static void do_cut(void)
{
    uint32_t ls = line_start(npos);
    uint32_t le = line_end(npos);       /* points at '\n' or nlen */

    /* Copy line content to cut buffer. */
    ncut_len = 0;
    for (uint32_t i = ls; i < le; i++)
        if (ncut_len < 80) ncut[ncut_len++] = nbuf[i];
    ncut[ncut_len] = '\0';

    /* Remove the line including its trailing '\n' if present. */
    uint32_t end = le;
    if (end < nlen && nbuf[end] == '\n') end++;
    uint32_t del = end - ls;
    for (uint32_t i = ls; i + del < nlen; i++) nbuf[i] = nbuf[i + del];
    nlen -= del;
    npos = ls;
    if (npos > nlen) npos = nlen;
    ndirty = 1;
}

static void do_paste(void)
{
    if (ncut_len == 0) return;
    /* Move cursor to start of current line, then insert cut text + '\n'. */
    uint32_t ins_pos = line_start(npos);
    npos = ins_pos;
    for (int i = 0; i < ncut_len; i++) buf_insert(ncut[i]);
    buf_insert('\n');
    npos = ins_pos;   /* leave cursor at beginning of pasted line */
}

/* ── Navigation ─────────────────────────────────────────────────────── */

static void move_up(void)
{
    uint32_t col = cursor_col();
    uint32_t ls  = line_start(npos);
    if (ls == 0) return;                       /* already on first line */
    uint32_t prev_end   = ls - 1;              /* offset of '\n' above  */
    uint32_t prev_start = line_start(prev_end);
    uint32_t prev_len   = prev_end - prev_start;
    npos = prev_start + (col < prev_len ? col : prev_len);
}

static void move_down(void)
{
    uint32_t col        = cursor_col();
    uint32_t le         = line_end(npos);
    if (le >= nlen) return;                    /* already on last line  */
    uint32_t next_start = le + 1;
    uint32_t next_end   = line_end(next_start);
    uint32_t next_len   = next_end - next_start;
    npos = next_start + (col < next_len ? col : next_len);
}

/* ── Save ───────────────────────────────────────────────────────────── */

static int do_save(void)
{
    if (!nfilename[0]) return -1;
    if (alfs_write(nfilename, nbuf, nlen) < 0) return -1;
    ndirty = 0;
    return 0;
}

/* ── Public entry point ─────────────────────────────────────────────── */

void nano_run(const char *filename, const char *base_cwd)
{
    /* Allocate one PMM page as the editing buffer. */
    nbuf = (char *)pmm_alloc();
    if (!nbuf) return;
    for (int i = 0; i < 4096; i++) nbuf[i] = 0;

    /* Initialise state. */
    nlen = 0; npos = 0; nscroll = 0; ndirty = 0;
    ncut_len = 0; ncut[0] = '\0';

    /* Resolve filename and try to load existing content. */
    if (filename && *filename) {
        path_resolve(base_cwd ? base_cwd : "/", filename, nfilename);
        int idx = alfs_find(nfilename);
        if (idx >= 0 && !alfs_is_dir(idx)) {
            uint32_t fsz = alfs_size(idx);
            if (fsz > NANO_BUF_MAX) fsz = NANO_BUF_MAX;
            int got = alfs_read(idx, nbuf, fsz);
            if (got > 0) nlen = (uint32_t)got;
        }
    } else {
        nfilename[0] = '\0';
    }

    /* Clear the full 80×24 display and draw initial state. */
    for (int r = 0; r < 24; r++)
        fill_row(r, ' ', CONTENT_ATTR);
    redraw();

    /* ── Main editing loop ────────────────────────────────────────── */
    for (;;) {
        char c = keyboard_getchar();
        if (!c) continue;

        /* ── Ctrl+X — exit ────────────────────────────────────────── */
        if (c == CTRL('x')) {
            if (ndirty) {
                status_msg("Save changes? (y/n): ");
                vga_set_cursor_at(NANO_STATUS_ROW, 21);
                char ans = 0;
                while (!ans) ans = keyboard_getchar();
                if (ans == 'y' || ans == 'Y') do_save();
                else if (ans != 'n' && ans != 'N') {
                    /* Cancel: restore display and keep editing. */
                    draw_status(); redraw(); continue;
                }
            }
            break;
        }

        /* ── Ctrl+S — save ────────────────────────────────────────── */
        if (c == CTRL('s')) {
            if (do_save() == 0)
                status_msg("Saved.");
            else
                status_msg("Save failed (no filename or disk error).");
            vga_set_cursor_at(NANO_STATUS_ROW, 0);
            /* Brief visual feedback — next keypress will redraw. */
            /* Redraw content so cursor is correct. */
            redraw();
            continue;
        }

        /* ── Ctrl+K — cut line ────────────────────────────────────── */
        if (c == CTRL('k')) {
            do_cut();
            adjust_scroll();
            redraw();
            continue;
        }

        /* ── Ctrl+U — paste cut line ──────────────────────────────── */
        if (c == CTRL('u')) {
            do_paste();
            adjust_scroll();
            redraw();
            continue;
        }

        /* ── Navigation ───────────────────────────────────────────── */
        if (c == KEY_UP)    { move_up();                    adjust_scroll(); redraw(); continue; }
        if (c == KEY_DOWN)  { move_down();                  adjust_scroll(); redraw(); continue; }
        if (c == KEY_LEFT)  { if (npos > 0) npos--;         adjust_scroll(); redraw(); continue; }
        if (c == KEY_RIGHT) { if (npos < nlen) npos++;      adjust_scroll(); redraw(); continue; }
        if (c == KEY_HOME)  { npos = line_start(npos);      adjust_scroll(); redraw(); continue; }
        if (c == KEY_END)   { npos = line_end(npos);        adjust_scroll(); redraw(); continue; }

        /* ── Backspace ────────────────────────────────────────────── */
        if (c == '\b') { buf_delete_before(); adjust_scroll(); redraw(); continue; }

        /* ── Delete ───────────────────────────────────────────────── */
        if (c == KEY_DEL) { buf_delete_at(); adjust_scroll(); redraw(); continue; }

        /* ── Enter ────────────────────────────────────────────────── */
        if (c == '\n') { buf_insert('\n'); adjust_scroll(); redraw(); continue; }

        /* ── Printable characters ─────────────────────────────────── */
        if (c >= ' ' && (unsigned char)c < 0x7F) {
            buf_insert(c);
            adjust_scroll();
            redraw();
            continue;
        }

        /* All other keys (tab, other ctrl codes) are silently ignored. */
    }

    pmm_free((uint32_t)nbuf);
    /* Caller (shell) is responsible for restoring its own UI. */
}
