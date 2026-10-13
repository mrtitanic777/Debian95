/* Debian95 — a small interactive shell, built on the shared user runtime. It
 * reads command lines over the console (SYS_READLINE), and runs entirely in
 * user mode: ls/cat/echo/run/help/exit via the filesystem + exec syscalls.
 * `run <prog>` execs another on-disk ELF and waits for it. */
#include <debian95/usys.h>

static char g_line[128];
static char g_buf[4096];

static void cat(const char *name) {
    long n = u_readfile(name, g_buf, sizeof(g_buf) - 1);
    if (n < 0) { u_puts("cat: not found: "); u_puts(name); u_puts("\n"); return; }
    u_write(g_buf, (unsigned)n);
    if (n == 0 || g_buf[n - 1] != '\n') u_puts("\n");
}

static void run(const char *name) {
    long pid = u_exec(name);
    if (pid < 0) { u_printf("run: cannot exec: %s\n", name); return; }
    long st = u_wait(pid);
    u_printf("[%s exited, pid %ld, status %ld]\n", name, pid, st);
}

/* exercise the shared-rtl heap + printf */
static void ver(void) {
    char *b = (char *)u_malloc(64);
    if (!b) { u_puts("ver: malloc failed\n"); return; }
    u_strcpy(b, "Debian95/ARM64");
    u_printf("%s shell -- pid %ld, shared user rtl (malloc+printf) live\n", b, u_getpid());
    u_free(b);
}

long umain(long arg) {
    (void)arg;
    u_puts("\nDebian95/ARM64 shell -- type 'help'.\n");
    for (;;) {
        u_puts("logan> ");
        long n = u_readline(g_line, sizeof(g_line));
        if (n <= 0) continue;

        if (u_strcmp(g_line, "help") == 0)
            u_puts("commands: ls  ps  net  cat <f>  write <f> <text>  run <p>  wget <path>  echo <t>  ver  help  exit\n");
        else if (u_strcmp(g_line, "ver") == 0)
            ver();
        else if (u_strcmp(g_line, "ls") == 0)
            u_listdir();
        else if (u_strcmp(g_line, "ps") == 0)
            u_ps();
        else if (u_strcmp(g_line, "net") == 0)
            u_netinfo();
        else if (u_strncmp(g_line, "write ", 6) == 0) {
            char *p = g_line + 6;                /* "write NAME text..." */
            char *sp = p; while (*sp && *sp != ' ') sp++;
            if (*sp != ' ') { u_puts("usage: write <file> <text>\n"); }
            else { *sp = 0; const char *text = sp + 1; unsigned n = u_strlen(text);
                if (u_writefile(p, text, n) == 0) u_printf("wrote %u bytes to %s\n", n, p);
                else u_printf("write: failed: %s\n", p); }
        }
        else if (u_strcmp(g_line, "exit") == 0) { u_puts("bye.\n"); break; }
        else if (u_strncmp(g_line, "cat ", 4) == 0)  cat(g_line + 4);
        else if (u_strncmp(g_line, "run ", 4) == 0)  run(g_line + 4);
        else if (u_strncmp(g_line, "wget ", 5) == 0) {
            long n = u_wget(g_line + 5, g_buf, sizeof(g_buf) - 1);
            if (n < 0) u_printf("wget: failed (%s)\n", g_line + 5);
            else { u_printf("[%ld bytes from 10.0.2.2:8080]\n", n); u_write(g_buf, (unsigned)n); u_puts("\n"); }
        }
        else if (u_strncmp(g_line, "echo ", 5) == 0) { u_puts(g_line + 5); u_puts("\n"); }
        else { u_puts("unknown command: "); u_puts(g_line); u_puts("  (try 'help')\n"); }
    }
    return 0;
}
