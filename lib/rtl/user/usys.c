/* Debian95 — portable user-mode syscall wrappers + minimal helpers. Arch-neutral:
 * every arch-specific detail lives in __syscall (usys_<arch>.S). */
#include <debian95/usys.h>

long u_write(const char *buf, unsigned len) { return __syscall(SYS_WRITE, (long)buf, len, 0, 0, 0); }
long u_getpid(void)                          { return __syscall(SYS_GETPID, 0, 0, 0, 0, 0); }
void u_yield(void)                           { __syscall(SYS_YIELD, 0, 0, 0, 0, 0); }
long u_spawn(long arg)                       { return __syscall(SYS_SPAWN, arg, 0, 0, 0, 0); }
long u_wait(long pid)                        { return __syscall(SYS_WAIT, pid, 0, 0, 0, 0); }
long u_readline(char *buf, unsigned max)     { return __syscall(SYS_READLINE, (long)buf, max, 0, 0, 0); }
long u_readfile(const char *name, void *buf, unsigned max)
                                             { return __syscall(SYS_READFILE, (long)name, (long)buf, max, 0, 0); }
long u_listdir(void)                         { return __syscall(SYS_LISTDIR, 0, 0, 0, 0, 0); }
long u_exec(const char *name)                { return __syscall(SYS_EXEC, (long)name, 0, 0, 0, 0); }
long u_ps(void)                              { return __syscall(SYS_PS, 0, 0, 0, 0, 0); }
long u_wget(const char *path, void *buf, unsigned max) { return __syscall(SYS_HTTPGET, (long)path, (long)buf, max, 0, 0); }
long u_writefile(const char *name, const void *data, unsigned len) { return __syscall(SYS_WRITEFILE, (long)name, (long)data, len, 0, 0); }
long u_netinfo(void) { return __syscall(SYS_NETINFO, 0, 0, 0, 0, 0); }

int u_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}
int u_strncmp(const char *a, const char *b, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)((unsigned char)a[i]) - (int)((unsigned char)b[i]);
        if (!a[i]) break;
    }
    return 0;
}
void u_exit(long code)                       { __syscall(SYS_EXIT, code, 0, 0, 0, 0); for (;;) { } }

/* C entry called by the crt0 in usys_<arch>.S. */
void __start_c(long arg) { u_exit(umain(arg)); }

unsigned u_strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
long u_puts(const char *s)       { return u_write(s, u_strlen(s)); }

long u_putu(unsigned long v) {
    char tmp[20]; int i = 0;
    if (v == 0) return u_write("0", 1);
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    char out[20]; int j = 0;
    while (i) out[j++] = tmp[--i];
    return u_write(out, (unsigned)j);
}

/* ---- mem/str ---- */
void *u_memcpy(void *d, const void *s, unsigned n) {
    unsigned char *a = d; const unsigned char *b = s;
    for (unsigned i = 0; i < n; i++) a[i] = b[i];
    return d;
}
void *u_memset(void *d, int c, unsigned n) {
    unsigned char *a = d;
    for (unsigned i = 0; i < n; i++) a[i] = (unsigned char)c;
    return d;
}
char *u_strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }

/* ---- tiny user-mode heap ---- */
#define U_HEAP_SZ 65536
static unsigned char g_heap[U_HEAP_SZ];
static unsigned g_heap_used;
typedef struct ublk { unsigned size; struct ublk *next; } ublk;   /* free list node */
static ublk *g_free;

void *u_malloc(unsigned n) {
    n = (n + 15u) & ~15u;
    for (ublk **pp = &g_free; *pp; pp = &(*pp)->next)         /* reuse a freed block */
        if ((*pp)->size >= n) { ublk *b = *pp; *pp = b->next; return (void *)(b + 1); }
    unsigned need = n + (unsigned)sizeof(ublk);              /* else bump */
    if (g_heap_used + need > U_HEAP_SZ) return 0;
    ublk *b = (ublk *)(g_heap + g_heap_used);
    g_heap_used += need; b->size = n;
    return (void *)(b + 1);
}
void u_free(void *p) {
    if (!p) return;
    ublk *b = (ublk *)p - 1;
    b->next = g_free; g_free = b;
}

/* ---- formatted output ---- */
#include <stdarg.h>
static int fmt_u(char *out, unsigned long v, int base, int upper) {
    char t[24]; int i = 0;
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) t[i++] = '0';
    while (v) { t[i++] = d[v % base]; v /= base; }
    int n = 0; while (i) out[n++] = t[--i];
    return n;
}
long u_printf(const char *fmt, ...) {
    char buf[256]; int o = 0;
    va_list ap; va_start(ap, fmt);
    for (const char *p = fmt; *p && o < (int)sizeof(buf) - 24; p++) {
        if (*p != '%') { buf[o++] = *p; continue; }
        p++;
        int lng = 0; while (*p == 'l') { lng = 1; p++; }
        switch (*p) {
            case 'c': buf[o++] = (char)va_arg(ap, int); break;
            case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
                        while (*s && o < (int)sizeof(buf) - 1) buf[o++] = *s++; } break;
            case 'd': { long v = lng ? va_arg(ap, long) : va_arg(ap, int);
                        if (v < 0) { buf[o++] = '-'; v = -v; }
                        o += fmt_u(buf + o, (unsigned long)v, 10, 0); } break;
            case 'u': { unsigned long v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                        o += fmt_u(buf + o, v, 10, 0); } break;
            case 'x': { unsigned long v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                        o += fmt_u(buf + o, v, 16, 0); } break;
            case '%': buf[o++] = '%'; break;
            default:  buf[o++] = '%'; if (*p) buf[o++] = *p; break;
        }
    }
    va_end(ap);
    return u_write(buf, (unsigned)o);
}
