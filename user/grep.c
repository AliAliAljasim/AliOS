#include "user.h"

/* grep — search for a pattern in a file or stdin.
 *
 *   grep <pattern>         read from stdin
 *   grep <pattern> <file>  read from named file
 *
 * Prints every line that contains the pattern as a substring. */

#define BUF_SIZE  4096

static char ibuf[BUF_SIZE];   /* input buffer  */
static char line[512];        /* current line  */

/* Return 1 if pat appears anywhere in str. */
static int contains(const char *pat, int plen, const char *str, int slen)
{
    for (int i = 0; i <= slen - plen; i++) {
        int ok = 1;
        for (int j = 0; j < plen; j++)
            if (str[i + j] != pat[j]) { ok = 0; break; }
        if (ok) return 1;
    }
    return 0;
}

void _start(int argc, char **argv)
{
    if (argc < 2) { puts("usage: grep <pattern> [file]\n"); exit(1); }

    const char *pat  = argv[1];
    int         plen = strlen(pat);

    int fd = 0;
    if (argc >= 3) {
        fd = open(argv[2]);
        if (fd < 0) {
            puts("grep: cannot open '");
            puts(argv[2]);
            puts("'\n");
            exit(1);
        }
    }

    /* Read entire input into ibuf (loop handles pipe chunks). */
    int total = 0, n;
    while (total < BUF_SIZE - 1 && (n = read(fd, ibuf + total, BUF_SIZE - 1 - total)) > 0)
        total += n;
    if (fd > 2) close(fd);

    /* Walk byte-by-byte, emit matching lines. */
    int li = 0;
    for (int i = 0; i <= total; i++) {
        char c = (i < total) ? ibuf[i] : '\n';   /* flush last line */
        if (c == '\n') {
            line[li] = '\0';
            if (contains(pat, plen, line, li)) {
                puts(line);
                write(1, "\n", 1);
            }
            li = 0;
        } else if (li < (int)sizeof(line) - 1) {
            line[li++] = c;
        }
    }

    exit(0);
}
