#include "user.h"

/* echo — print argv[1..] separated by spaces, followed by a newline.
 * Tests: argv/argc passing, SYS_WRITE, SYS_EXIT. */
void _start(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) write(1, " ", 1);
        write(1, argv[i], strlen(argv[i]));
    }
    write(1, "\n", 1);
    exit(0);
}
