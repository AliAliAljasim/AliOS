#include "user.h"

void _start(int argc, char **argv)
{
    int  field = 1;
    char delim = '\t';

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'f') {
            const char *p = argv[i][2] ? argv[i] + 2
                                       : (i + 1 < argc ? argv[++i] : "1");
            field = 0;
            while (*p >= '0' && *p <= '9') field = field * 10 + (*p++ - '0');
        } else if (argv[i][0] == '-' && argv[i][1] == 'd') {
            delim = argv[i][2] ? argv[i][2]
                               : (i + 1 < argc ? argv[++i][0] : '\t');
        }
    }

    char rbuf[256];
    int  rpos = 0, rlen = 0;
    char line[512];
    int  li = 0;

    for (;;) {
        if (rpos >= rlen) {
            rlen = read(0, rbuf, sizeof(rbuf));
            rpos = 0;
            if (rlen <= 0) break;
        }
        char c = rbuf[rpos++];

        if (c == '\n' || li >= 511) {
            line[li] = '\0';

            /* Walk to the requested field. */
            int   f = 1;
            char *p = line;
            while (*p && f < field) {
                if (*p == delim) f++;
                p++;
            }
            /* Print the field (up to the next delimiter or end). */
            if (f == field) {
                char *end = p;
                while (*end && *end != delim) end++;
                write(1, p, end - p);
            }
            write(1, "\n", 1);
            li = 0;
        } else {
            line[li++] = c;
        }
    }
    exit(0);
}
