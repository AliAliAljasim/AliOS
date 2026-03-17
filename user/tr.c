#include "user.h"

void _start(int argc, char **argv)
{
    int         del  = 0;
    int         ai   = 1;
    const char *set1 = 0;
    const char *set2 = "";

    if (ai < argc && argv[ai][0] == '-' && argv[ai][1] == 'd' && !argv[ai][2])
        { del = 1; ai++; }

    if (ai >= argc) { puts("usage: tr [-d] <set1> [set2]\n"); exit(1); }
    set1 = argv[ai++];
    if (!del && ai < argc) set2 = argv[ai];

    int s1len = strlen(set1);
    int s2len = strlen(set2);

    char ibuf[512], obuf[512];
    int  n;

    while ((n = read(0, ibuf, sizeof(ibuf))) > 0) {
        int oi = 0;
        for (int i = 0; i < n; i++) {
            unsigned char c = (unsigned char)ibuf[i];
            int fi = -1;
            for (int k = 0; k < s1len; k++)
                if ((unsigned char)set1[k] == c) { fi = k; break; }
            if (fi >= 0) {
                if (!del) {
                    /* Map to set2; clamp to last char if set2 is shorter. */
                    char tc = s2len ? (fi < s2len ? set2[fi] : set2[s2len - 1])
                                    : (char)c;
                    obuf[oi++] = tc;
                }
                /* del mode: skip the character */
            } else {
                obuf[oi++] = (char)c;
            }
        }
        if (oi > 0) write(1, obuf, oi);
    }
    exit(0);
}
