#include "user.h"

static int atoi(const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

void _start(int argc, char **argv)
{
    int start = 1, end = 1;

    if (argc == 2) {
        end = atoi(argv[1]);
    } else if (argc >= 3) {
        start = atoi(argv[1]);
        end   = atoi(argv[2]);
    } else {
        puts("usage: seq <n>  or  seq <start> <end>\n");
        exit(1);
    }

    int step = (end >= start) ? 1 : -1;
    for (int i = start; step > 0 ? i <= end : i >= end; i += step) {
        putnum(1, i);
        write(1, "\n", 1);
    }
    exit(0);
}
