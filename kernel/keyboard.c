#include "keyboard.h"
#include "irq.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

#define KBD_DATA  0x60

/* ── Scancode set 1 → ASCII ─────────────────────────────────────
 * Index = make code (key press).
 * Break code (key release) = make code | 0x80.
 */
static const char sc_unshifted[] = {
/*00*/  0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
/*08*/  '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
/*10*/  'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
/*18*/  'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
/*20*/  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
/*28*/  '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
/*30*/  'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
/*38*/  0,    ' ',
};

static const char sc_shifted[] = {
/*00*/  0,    0,    '!',  '@',  '#',  '$',  '%',  '^',
/*08*/  '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
/*10*/  'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
/*18*/  'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
/*20*/  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
/*28*/  '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
/*30*/  'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
/*38*/  0,    ' ',
};

/* ── Ctrl-C callback ─────────────────────────────────────────── */

static void (*ctrlc_fn)(void) = NULL;

void keyboard_on_ctrlc(void (*fn)(void))
{
    ctrlc_fn = fn;
}

/* ── Ring buffer ────────────────────────────────────────────── */

#define BUF_SIZE 64

static char    buf[BUF_SIZE];
static uint8_t buf_head;   /* write index */
static uint8_t buf_tail;   /* read  index */

static void buf_push(char c)
{
    uint8_t next = (buf_head + 1) % BUF_SIZE;
    if (next != buf_tail) {   /* drop if full */
        buf[buf_head] = c;
        buf_head = next;
    }
}

char keyboard_getchar(void)
{
    if (buf_tail == buf_head)
        return 0;
    char c = buf[buf_tail];
    buf_tail = (buf_tail + 1) % BUF_SIZE;
    return c;
}

/* ── IRQ1 handler ───────────────────────────────────────────── */

static int shift;
static int ctrl;
static int extended;   /* set to 1 when 0xE0 prefix scancode is received */

static void kbd_handler(void)
{
    uint8_t sc = inb(KBD_DATA);

    /* Extended-key prefix: next scancode is from the extended set. */
    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    /* Handle extended scancodes (arrow keys, Delete, Home, End). */
    if (extended) {
        extended = 0;
        if (sc & 0x80) return;   /* break code — ignore key-release */
        switch (sc) {
            case 0x48: buf_push(KEY_UP);    return;
            case 0x50: buf_push(KEY_DOWN);  return;
            case 0x4B: buf_push(KEY_LEFT);  return;
            case 0x4D: buf_push(KEY_RIGHT); return;
            case 0x53: buf_push(KEY_DEL);   return;
            case 0x47: buf_push(KEY_HOME);  return;
            case 0x4F: buf_push(KEY_END);   return;
        }
        return;
    }

    /* Break code: bit 7 is set — key was released */
    if (sc & 0x80) {
        sc &= 0x7F;
        if (sc == 0x2A || sc == 0x36) shift = 0;   /* L/R shift released */
        if (sc == 0x1D)               ctrl  = 0;   /* L Ctrl released    */
        return;
    }

    /* Make code: key was pressed */
    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }  /* L/R shift */
    if (sc == 0x1D) { ctrl  = 1; return; }                 /* L Ctrl    */

    /* Ctrl-C: Ctrl held + 'c' key (scancode 0x2E) */
    if (ctrl && sc == 0x2E) {
        if (ctrlc_fn) ctrlc_fn();
        return;
    }

    const char *table = shift ? sc_shifted : sc_unshifted;
    if (sc < sizeof(sc_unshifted)) {
        char c = table[sc];
        if (c)
            buf_push(c);
    }
}

void keyboard_init(void)
{
    irq_install_handler(1, kbd_handler);
    irq_unmask(1);
}
