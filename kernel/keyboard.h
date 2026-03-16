#pragma once

/* Special-key pseudo-characters returned by keyboard_getchar().
 * These values are in the C0 control range and are not valid ASCII
 * printables, so the shell can distinguish them from normal input. */
#define KEY_UP    '\x01'
#define KEY_DOWN  '\x02'
#define KEY_LEFT  '\x03'
#define KEY_RIGHT '\x04'
#define KEY_HOME  '\x05'
#define KEY_END   '\x06'
#define KEY_DEL   '\x7F'   /* Delete (distinct from Backspace = '\b' = 0x08) */

void keyboard_init(void);
char keyboard_getchar(void);  /* returns 0 if buffer is empty */

/* Register a callback invoked (from IRQ context) when Ctrl-C is pressed.
   Pass NULL to unregister.  Used by the shell to kill the foreground task. */
void keyboard_on_ctrlc(void (*fn)(void));
