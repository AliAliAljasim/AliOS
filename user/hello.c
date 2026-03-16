/*
 * hello.c — first AliOS user-mode program.
 *
 * Compiled as a freestanding ELF32 and stored on the AliFS disk image.
 * Loaded by the kernel's ELF loader; demonstrates the basic syscall ABI.
 */

static inline void sys_write(int fd, const char *buf, int len)
{
    __asm__ volatile ("int $0x80"
        : : "a"(0), "b"(fd), "c"(buf), "d"(len) : "memory");
}

static inline __attribute__((noreturn)) void sys_exit(int code)
{
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}

void _start(void)
{
    sys_write(1, "Hello from ELF!\n", 16);
    sys_exit(0);
}
