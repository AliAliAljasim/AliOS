#include "user.h"

/* cat — copy stdin (or a named file) to stdout.
 *
 *   cat            reads from stdin (fd 0); works as the right side of a pipe
 *   cat <file>     reads the named AliFS file and writes it to stdout
 *
 * Tests: SYS_READ (stdin pipe), SYS_OPEN/READ/CLOSE (file), SYS_WRITE, argv. */
void _start(int argc, char **argv)
{
    char buf[256];
    int  n;

    if (argc > 1) {
        /* Read a named file. */
        int fd = open(argv[1]);
        if (fd < 0) {
            puts("cat: file not found: ");
            puts(argv[1]);
            puts("\n");
            exit(1);
        }
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, n);
        close(fd);
    } else {
        /* Read from stdin — blocks until EOF (pipe write-end closes). */
        while ((n = read(0, buf, sizeof(buf))) > 0)
            write(1, buf, n);
    }

    exit(0);
}
