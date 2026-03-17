#include "user.h"

static char prev[512];
static char cur[512];

void _start(int argc, char **argv)
{
    int fd = (argc > 1) ? open(argv[1]) : 0;
    if (fd < 0) { puts("uniq: cannot open file\n"); exit(1); }

    prev[0] = '\0';
    int ci = 0;
    char rbuf[256];
    int  rpos = 0, rlen = 0;

    for (;;) {
        if (rpos >= rlen) {
            rlen = read(fd, rbuf, sizeof(rbuf));
            rpos = 0;
            if (rlen <= 0) break;
        }
        char c = rbuf[rpos++];

        if (c == '\n' || ci >= 511) {
            cur[ci] = '\0';
            /* Print only if different from previous line. */
            int same = 1;
            for (int i = 0; cur[i] || prev[i]; i++)
                if (cur[i] != prev[i]) { same = 0; break; }
            if (!same) {
                puts(cur); write(1, "\n", 1);
                for (int i = 0; i <= ci; i++) prev[i] = cur[i];
            }
            ci = 0;
        } else {
            cur[ci++] = c;
        }
    }

    /* Flush last line if no trailing newline. */
    if (ci > 0) {
        cur[ci] = '\0';
        int same = 1;
        for (int i = 0; cur[i] || prev[i]; i++)
            if (cur[i] != prev[i]) { same = 0; break; }
        if (!same) { puts(cur); write(1, "\n", 1); }
    }

    if (fd) close(fd);
    exit(0);
}
