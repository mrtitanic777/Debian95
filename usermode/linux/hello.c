/* Debian95 — a freestanding, static AArch64 *Linux* binary (no libc): it talks to
 * the kernel with raw Linux svc syscalls. Built with a Linux target
 * (clang --target=aarch64-linux-gnu), it is a genuine unmodified Linux ELF that
 * Debian95 runs through its Linux personality. */
static long lsys(long n, long a, long b, long c) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
void _start(void) {
    const char *m = "Hello from a REAL unmodified Linux ELF binary, running on Debian95!\n";
    int n = 0; while (m[n]) n++;
    lsys(64, 1, (long)m, n);   /* write(1, m, n) */
    lsys(93, 0, 0, 0);         /* exit(0)        */
}
