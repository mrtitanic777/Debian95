/* Debian95 — shared user-mode syscall runtime (the seed of a portable "ulibc").
 * A user program links the per-arch trampoline (lib/rtl/user/usys_<arch>.S) and
 * the portable wrappers (lib/rtl/user/usys.c) and talks to the kernel only
 * through these calls. The same C source then builds for x86-64 and ARM64. */
#ifndef DEBIAN95_USYS_H
#define DEBIAN95_USYS_H

/* Debian95 native syscall numbers (identical on every arch). */
#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SPAWN   5    /* launch another user process; a0 = entry argument */
#define SYS_WAIT    6    /* block until a0=pid exits; -> its exit code */
#define SYS_READLINE 7   /* read a line from the console; a0=buf a1=max -> length */
#define SYS_READFILE 8   /* read a file by name; a0=name a1=buf a2=max -> bytes, -1 */
#define SYS_LISTDIR  9   /* print the root directory to the console -> 0 */
#define SYS_EXEC    14   /* load+run a named ELF off disk; a0=name -> pid, -1 */
#define SYS_PS      15   /* print the process table (from the Ob \Process dir) */
#define SYS_HTTPGET 16   /* HTTP GET a0=path into a1=buf (max a2) -> bytes, -1 */
#define SYS_WRITEFILE 17 /* write a0=name with a1=data of a2=len -> 0, or -1 */
#define SYS_NETINFO 18   /* print the current IP configuration -> 0 */

/* raw syscall: num in the arch's number reg, up to five register arguments,
 * result returned. Implemented in asm per arch. */
long __syscall(long num, long a1, long a2, long a3, long a4, long a5);

/* The program's entry point. The shared crt0 (_start, in usys_<arch>.S) captures
 * the kernel-supplied entry argument and calls this, then u_exit()s the return. */
long umain(long arg);

/* ---- typed wrappers ---- */
long          u_write(const char *buf, unsigned len);   /* -> bytes written */
void          u_exit(long code) __attribute__((noreturn));
long          u_getpid(void);
void          u_yield(void);
long          u_spawn(long arg);                         /* -> child pid, or -1 */
long          u_wait(long pid);                          /* block for a child -> exit code */
long          u_readline(char *buf, unsigned max);       /* read a console line -> length */
long          u_readfile(const char *name, void *buf, unsigned max); /* -> bytes, or -1 */
long          u_listdir(void);                           /* print the root directory */
long          u_exec(const char *name);                  /* run a named ELF -> pid, or -1 */
long          u_ps(void);                                /* print the process table */
long          u_wget(const char *path, void *buf, unsigned max); /* HTTP GET -> bytes */
long          u_writefile(const char *name, const void *data, unsigned len); /* -> 0, or -1 */
long          u_netinfo(void);                           /* print IP config */
int           u_strcmp(const char *a, const char *b);
int           u_strncmp(const char *a, const char *b, unsigned n);

/* ---- tiny freestanding helpers built on the wrappers ---- */
unsigned      u_strlen(const char *s);
long          u_puts(const char *s);                    /* write a NUL-terminated string */
long          u_putu(unsigned long v);                  /* write an unsigned decimal */

/* ---- mem/str ---- */
void         *u_memcpy(void *d, const void *s, unsigned n);
void         *u_memset(void *d, int c, unsigned n);
char         *u_strcpy(char *d, const char *s);

/* ---- a tiny user-mode heap (static arena, first-fit free list) ---- */
void         *u_malloc(unsigned n);
void          u_free(void *p);

/* ---- formatted output: %c %s %d %u %x %% (with optional 'l' length) ---- */
long          u_printf(const char *fmt, ...);

#endif /* DEBIAN95_USYS_H */
