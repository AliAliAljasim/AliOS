#include "user.h"

/*
 * tee — read stdin, write to stdout and to a file simultaneously.
 *
 * Usage: tee [file]
 *   -a   append to file instead of overwriting
 */
void _start(int argc, char **argv)
{
    int   fd     = -1;
    int   append = 0;
    const char *path = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'a' && argv[i][2] == '\0') {
            append = 1;
        } else {
            path = argv[i];
        }
    }

    if (path) {
        fd = append ? open_a(path) : creat(path);
        if (fd < 0) {
            puts("tee: cannot open file\n");
            exit(1);
        }
    }

    char buf[512];
    int  n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
        if (fd >= 0) write(fd, buf, n);
    }

    if (fd >= 0) close(fd);
    exit(0);
}
