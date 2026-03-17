#include "user.h"

/* tail — print the last N lines of a file or stdin.
 *
 *   tail [file]       print last 10 lines
 *   tail -n N [file]  print last N lines */

#define BUF_SIZE  4096
#define MAX_LINES 64

static char ibuf[BUF_SIZE];

void _start(int argc, char **argv)
{
    int         limit = 10;
    const char *fname = (void *)0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n' && argv[i][2] == '\0') {
            if (i + 1 < argc) { i++; limit = 0; const char *p = argv[i]; while (*p >= '0' && *p <= '9') limit = limit * 10 + (*p++ - '0'); }
        } else {
            fname = argv[i];
        }
    }

    int fd = 0;
    if (fname) {
        fd = open(fname);
        if (fd < 0) { puts("tail: cannot open '"); puts(fname); puts("'\n"); exit(1); }
    }

    /* Read entire input. */
    int total = 0, n;
    while (total < BUF_SIZE - 1 && (n = read(fd, ibuf + total, BUF_SIZE - 1 - total)) > 0)
        total += n;
    if (fd > 2) close(fd);

    /* Find the start offset of the last `limit` lines. */
    int newlines = 0;
    int start    = total;
    for (int i = total - 1; i >= 0; i--) {
        if (ibuf[i] == '\n') {
            newlines++;
            if (newlines == limit) { start = i + 1; break; }
        }
        if (i == 0) start = 0;
    }

    write(1, ibuf + start, total - start);
    /* Ensure trailing newline. */
    if (total > 0 && ibuf[total - 1] != '\n')
        write(1, "\n", 1);

    exit(0);
}
