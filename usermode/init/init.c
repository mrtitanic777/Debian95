/* Debian95 — init: the first real user program, loaded from disk as INIT.ELF.
 * Freestanding, no libc; talks to the kernel only through SYSCALL. */

#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_GETPID  3

static long sys(long n, long a1, long a2, long a3) {
    long r;
    __asm__ volatile ("syscall"
                      : "=a"(r)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                      : "rcx", "r11", "memory");
    return r;
}

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }

void _start(void) {
    const char *msg = "  [ring3/ELF] Hello from INIT.ELF, loaded off disk by the ELF loader!\n";
    sys(SYS_WRITE, (long)msg, slen(msg), 0);

    long pid = sys(SYS_GETPID, 0, 0, 0);
    const char *msg2 = "  [ring3/ELF] my pid came back from a syscall; exiting cleanly.\n";
    sys(SYS_WRITE, (long)msg2, slen(msg2), 0);

    sys(SYS_EXIT, pid ? 0 : 0, 0, 0);
    for (;;) { }
}
