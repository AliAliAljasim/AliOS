#include "user.h"

#define BUFSZ    4096
#define MAXLINES 200

static char  ibuf[BUFSZ];
static char *lines[MAXLINES];
static int   nlines;

static int readall(int fd)
{
    int n, t = 0;
    while (t < BUFSZ - 1 && (n = read(fd, ibuf + t, BUFSZ - 1 - t)) > 0)
        t += n;
    ibuf[t] = '\0';
    return t;
}

static void mklines(int len)
{
    nlines = 0;
    char *p = ibuf, *e = ibuf + len;
    while (p < e && nlines < MAXLINES) {
        lines[nlines++] = p;
        while (p < e && *p != '\n') p++;
        if (p < e) *p++ = '\0';
    }
}

static int scmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

void _start(int argc, char **argv)
{
    int fd = (argc > 1) ? open(argv[1]) : 0;
    if (fd < 0) { puts("sort: cannot open file\n"); exit(1); }
    int len = readall(fd);
    if (fd) close(fd);

    mklines(len);

    /* Insertion sort */
    for (int i = 1; i < nlines; i++) {
        char *k = lines[i];
        int   j = i - 1;
        while (j >= 0 && scmp(lines[j], k) > 0) { lines[j + 1] = lines[j]; j--; }
        lines[j + 1] = k;
    }

    for (int i = 0; i < nlines; i++) { puts(lines[i]); write(1, "\n", 1); }
    exit(0);
}
