/* Debian95 — reader: a user program that opens and reads a file through the
 * Native API (NtCreateFile/NtReadFile/NtClose), then prints it. Freestanding. */

#define SYS_WRITE          1
#define SYS_EXIT           2
#define SYS_NT_CREATE_FILE 10
#define SYS_NT_READ_FILE   11
#define SYS_NT_CLOSE       12

static long sys3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
static long sys4(long n, long a, long b, long c, long d) {
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}
static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void puts_(const char *s) { sys3(SYS_WRITE, (long)s, slen(s), 0); }

void _start(void) {
    puts_("  [reader.elf] opening README.TXT via NtCreateFile...\n");

    unsigned handle = 0;
    if (sys3(SYS_NT_CREATE_FILE, (long)"README.TXT", (long)&handle, 0) != 0) {
        puts_("  [reader.elf] NtCreateFile failed\n");
        sys3(SYS_EXIT, 1, 0, 0);
    }

    char buf[256];
    unsigned got = 0;
    sys4(SYS_NT_READ_FILE, handle, (long)buf, sizeof(buf), (long)&got);
    puts_("  [reader.elf] contents: ");
    sys3(SYS_WRITE, (long)buf, got, 0);

    sys3(SYS_NT_CLOSE, handle, 0, 0);
    sys3(SYS_EXIT, 0, 0, 0);
    for (;;) { }
}
