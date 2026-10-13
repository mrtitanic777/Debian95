/* Debian95 — spin: a user program that prints a counter with delays so you can
 * watch it run concurrently with the desktop. Now built on the shared user
 * runtime (debian95/usys.h) — no hand-rolled syscall stubs. */
#include <debian95/usys.h>

long umain(long arg) {
    (void)arg;
    long pid = u_getpid();
    for (int t = 0; t < 8; t++) {
        u_puts("  [spin pid ");
        u_putu((unsigned long)(pid > 0 ? pid : 0));
        u_puts("] tick #");
        u_putu((unsigned long)t);
        u_puts("\n");
        for (volatile long s = 0; s < 40000000L; s++) { }   /* visible delay */
    }
    u_puts("  [spin] done.\n");
    return 0;
}
