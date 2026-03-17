#include "user.h"

/* head — print the first N lines of a file or stdin.
 *
 *   head [file]       print first 10 lines
 *   head -n N [file]  print first N lines */

void _start(int argc, char **argv)
{
    int         limit = 10;
    const char *fname = (void *)0;

    /* Parse args: optional -n N, optional filename. */
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
        if (fd < 0) { puts("head: cannot open '"); puts(fname); puts("'\n"); exit(1); }
    }

    int lines = 0;
    char c;
    while (lines < limit) {
        int n = read(fd, &c, 1);
        if (n <= 0) break;
        write(1, &c, 1);
        if (c == '\n') lines++;
    }

    if (fd > 2) close(fd);
    exit(0);
}
