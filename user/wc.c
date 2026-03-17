#include "user.h"

/* wc — count lines, words, and characters in a file or stdin.
 *
 *   wc [file]   print "lines words chars [file]" */

#define BUF_SIZE 4096

static char ibuf[BUF_SIZE];

void _start(int argc, char **argv)
{
    const char *fname = (argc > 1) ? argv[1] : (void *)0;

    int fd = 0;
    if (fname) {
        fd = open(fname);
        if (fd < 0) { puts("wc: cannot open '"); puts(fname); puts("'\n"); exit(1); }
    }

    int total = 0, n;
    while (total < BUF_SIZE - 1 && (n = read(fd, ibuf + total, BUF_SIZE - 1 - total)) > 0)
        total += n;
    if (fd > 2) close(fd);

    int lines = 0, words = 0, chars = total;
    int in_word = 0;
    for (int i = 0; i < total; i++) {
        char c = ibuf[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    putnum(1, lines); write(1, " ", 1);
    putnum(1, words); write(1, " ", 1);
    putnum(1, chars);
    if (fname) { write(1, " ", 1); puts(fname); }
    write(1, "\n", 1);

    exit(0);
}
