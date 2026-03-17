#pragma once

/* ── AliOS user-space syscall stubs ──────────────────────────────────────────
 *
 * Include this header in every user-space program instead of any libc header.
 * All functions are inlined to avoid needing a separate C runtime library.
 *
 * Calling convention:  int $0x80
 *   eax = syscall number   ebx = arg1   ecx = arg2   edx = arg3
 *   Return value in eax (negative on error).
 */

#define SYS_WRITE    0
#define SYS_EXIT     1
#define SYS_OPEN     2
#define SYS_READ     3
#define SYS_CLOSE    4
#define SYS_BRK      5
#define SYS_SLEEP    6
#define SYS_EXEC     7
#define SYS_FORK     8
#define SYS_WAIT     9
#define SYS_KILL    10
#define SYS_GETPID  11
#define SYS_GETPPID 12
#define SYS_SETCOLOR 13
#define SYS_PIPE    14
#define SYS_OPEN_W  15

static inline int write(int fd, const void *buf, int len)
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static inline int read(int fd, void *buf, int len)
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_READ), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static inline int open(const char *name)
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_OPEN), "b"(name)
        : "memory");
    return ret;
}

/* Create/overwrite a file and return a writable fd, or -1 on error. */
static inline int creat(const char *name)
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_OPEN_W), "b"(name), "c"(0)
        : "memory");
    return ret;
}

/* Open an existing file for appending (creates it if absent). */
static inline int open_a(const char *name)
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_OPEN_W), "b"(name), "c"(1)
        : "memory");
    return ret;
}

static inline int close(int fd)
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_CLOSE), "b"(fd));
    return ret;
}

static inline int pipe(int fds[2])
{
    int ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(SYS_PIPE), "b"(fds)
        : "memory");
    return ret;
}

static inline void exit(int code)
{
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT), "b"(code));
    __builtin_unreachable();
}

static inline int getpid(void)
{
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(SYS_GETPID));
    return ret;
}

static inline void setcolor(int fg, int bg)
{
    __asm__ volatile ("int $0x80" : : "a"(SYS_SETCOLOR), "b"(fg), "c"(bg));
}

static inline int kill(int pid, int sig)
{
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(SYS_KILL), "b"(pid), "c"(sig));
    return ret;
}

/* Minimal string helpers (no libc). */
static inline int strlen(const char *s)
{
    int n = 0; while (s[n]) n++; return n;
}

static inline int puts(const char *s)
{
    return write(1, s, strlen(s));
}

/* Write a non-negative decimal integer to fd. */
static inline void putnum(int fd, int n)
{
    char buf[12];
    int  i = 0;
    if (n == 0) { write(fd, "0", 1); return; }
    if (n < 0)  { write(fd, "-", 1); n = -n; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    /* reverse in place */
    for (int a = 0, b = i - 1; a < b; a++, b--)
        { char t = buf[a]; buf[a] = buf[b]; buf[b] = t; }
    write(fd, buf, i);
}
