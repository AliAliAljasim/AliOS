#include "user.h"

static const char hx[] = "0123456789abcdef";

static void put_byte(unsigned char b)
{
    char s[2] = { hx[b >> 4], hx[b & 0xF] };
    write(1, s, 2);
}

static void put_addr(unsigned int addr)
{
    char s[8];
    for (int i = 7; i >= 0; i--) { s[i] = hx[addr & 0xF]; addr >>= 4; }
    write(1, s, 8);
}

void _start(int argc, char **argv)
{
    int fd = (argc > 1) ? open(argv[1]) : 0;
    if (fd < 0) { puts("hexdump: cannot open file\n"); exit(1); }

    unsigned char row[16];
    unsigned int  addr = 0;
    int n;

    while ((n = read(fd, row, 16)) > 0) {
        put_addr(addr);
        write(1, "  ", 2);

        for (int i = 0; i < 16; i++) {
            if (i < n) { put_byte(row[i]); write(1, " ", 1); }
            else        write(1, "   ", 3);
            if (i == 7) write(1, " ", 1);   /* extra gap at midpoint */
        }

        write(1, " |", 2);
        for (int i = 0; i < n; i++) {
            char c = (row[i] >= 32 && row[i] < 127) ? (char)row[i] : '.';
            write(1, &c, 1);
        }
        write(1, "|\n", 2);

        addr += (unsigned int)n;
    }

    if (fd) close(fd);
    exit(0);
}
