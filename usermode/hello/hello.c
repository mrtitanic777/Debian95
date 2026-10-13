/* Debian95 — a portable EL0/ring-3 test program built on the shared user runtime
 * (debian95/usys.h), so this exact source runs on x86-64 and ARM64. The crt0 in
 * the runtime calls umain(arg) with the kernel-supplied entry argument.
 *
 * Demonstrates the full user process lifecycle from user space: the root
 * instance (arg==0) spawns two children (arg==1) via SYS_SPAWN, prints a few
 * lines, then blocks on each child with SYS_WAIT and reports the exit codes it
 * collected. Each child prints its pid three times (yielding so they interleave)
 * and exits with its pid as the status. */
#include <debian95/usys.h>

long umain(long arg) {
    long pid = u_getpid();

    if (arg == 0) {                     /* the root */
        long c1 = u_spawn(1);
        long c2 = u_spawn(1);
        u_puts("  [root pid ");  u_putu((unsigned long)pid);
        u_puts("] spawned children ");  u_putu((unsigned long)c1);
        u_puts(" and ");  u_putu((unsigned long)c2);  u_puts("\n");

        for (int i = 0; i < 2; i++) {
            u_puts("  [root  pid ");  u_putu((unsigned long)pid);  u_puts("] working\n");
            u_yield();
        }

        long s1 = u_wait(c1);
        long s2 = u_wait(c2);
        u_puts("  [root pid ");  u_putu((unsigned long)pid);
        u_puts("] collected: child ");  u_putu((unsigned long)c1);
        u_puts(" -> status ");  u_putu((unsigned long)s1);
        u_puts(", child ");  u_putu((unsigned long)c2);
        u_puts(" -> status ");  u_putu((unsigned long)s2);  u_puts("\n");
        return 0;
    }

    for (int i = 0; i < 3; i++) {       /* a child */
        u_puts("  [child pid ");  u_putu((unsigned long)pid);  u_puts("] via shared rtl\n");
        u_yield();
    }
    return pid;                         /* exit status = my pid */
}
