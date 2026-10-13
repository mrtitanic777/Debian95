/* Debian95 — Process/thread subsystem (Ps) + user-mode entry. */
#ifndef DEBIAN95_PS_H
#define DEBIAN95_PS_H

#include <debian95/types.h>

/* system-call numbers (Debian95 native ABI) */
#define SYS_WRITE          1
#define SYS_EXIT           2
#define SYS_GETPID         3
#define SYS_YIELD          4    /* voluntarily give up the rest of the time slice */
#define SYS_SPAWN          5    /* launch another user process; a0 = entry argument */
#define SYS_WAIT           6    /* block until a0=pid exits; -> its exit code */
#define SYS_READLINE       7    /* read a console line; a0=buf a1=max -> length */
#define SYS_READFILE       8    /* read a file by name; a0=name a1=buf a2=max -> bytes */
#define SYS_LISTDIR        9    /* print the root directory */
#define SYS_EXEC          14    /* load+run a named ELF off disk; a0=name -> pid */
#define SYS_PS            15    /* print the process table (Ob \Process directory) */
#define SYS_HTTPGET       16    /* HTTP GET a0=path into a1=buf (max a2) -> bytes */
#define SYS_WRITEFILE     17    /* write a0=name with a1=data of a2=len */
#define SYS_NETINFO       18    /* print the current IP configuration */
#define SYS_NT_CREATE_FILE 10
#define SYS_NT_READ_FILE   11
#define SYS_NT_CLOSE       12
#define SYS_NT_WRITE_FILE  13
/* Win32 shim services — targets of the ring-3 PE import trampolines */
#define SYS_WIN_WRITEFILE   20
#define SYS_WIN_MESSAGEBOX  21
#define SYS_WIN_EXITPROCESS 22
#define SYS_WIN_GETSTDHANDLE 23
#define SYS_WIN_GETPID      24
#define SYS_WIN_SLEEP       25

void syscall_init(void);      /* program EFER.SCE, STAR/LSTAR/FMASK */

void ps_init(void);           /* register the Process object type */

/* Load `name` (an ELF on the boot volume) and run it in ring 3, blocking until
 * it exits; returns its exit code. -1 if it could not be loaded. */
int ps_spawn(const char *name);

/* Called from the SYS_EXIT handler; returns to the ps_spawn that launched it. */
void ps_on_exit(u64 code) __attribute__((noreturn));

/* Route SYS_WRITE output to a sink (e.g. the shell console) in addition to serial. */
void sys_set_write_sink(void (*fn)(const char *s, u32 n));
void console_out(const char *s, u32 n);

/* Load and run a Windows PE (.exe) as a ring-3 process; returns the new pid. */
int pe_run(const char *name);

/* Wrap a ready address space + entry + user stack as a Process and schedule it
 * as a preemptible ring-3 task. Returns the new pid, or -1. */
int ps_launch(u64 space, u64 entry, u64 ustack, const char *name);

extern u64 g_current_pid;

/* arch */
void enter_user_mode(u64 entry, u64 user_rsp);   /* first drop to ring 3 (no return) */
int  kern_setjmp(u64 *buf);
void kern_longjmp(u64 *buf, int val) __attribute__((noreturn));

/* ELF64 loader: map an ET_EXEC's segments into `space`; sets *entry. 0 on ok. */
int elf_load(u64 space, const u8 *buf, u32 len, u64 *entry);

#endif /* DEBIAN95_PS_H */
