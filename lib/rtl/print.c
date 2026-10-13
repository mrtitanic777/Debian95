/* Debian95 — console output: kputc/kputs/kprintf routed to serial + VGA. */
#include <debian95/rtl.h>
#include <stdarg.h>

void kputc(char c) {
    serial_putc(c);
    vga_putc(c);
}

void kputs(const char *s) {
    while (*s) kputc(*s++);
}

static void put_u64(u64 v, unsigned base, int upper) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *d = upper ? up : lo;
    char buf[32];
    int i = 0;
    if (v == 0) { kputc('0'); return; }
    while (v && i < 32) { buf[i++] = d[v % base]; v /= base; }
    while (i--) kputc(buf[i]);
}

static void put_i64(i64 v) {
    if (v < 0) { kputc('-'); put_u64((u64)(-v), 10, 0); }
    else put_u64((u64)v, 10, 0);
}

/* Supports: %c %s %d %u %x %X %p %lu %lx %% */
void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { kputc(*fmt); continue; }
        fmt++;
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        switch (*fmt) {
            case 'c': kputc((char)va_arg(ap, int)); break;
            case 's': kputs(va_arg(ap, const char *)); break;
            case 'd': put_i64(is_long ? va_arg(ap, i64) : va_arg(ap, i32)); break;
            case 'u': put_u64(is_long ? va_arg(ap, u64) : va_arg(ap, u32), 10, 0); break;
            case 'x': put_u64(is_long ? va_arg(ap, u64) : va_arg(ap, u32), 16, 0); break;
            case 'X': put_u64(is_long ? va_arg(ap, u64) : va_arg(ap, u32), 16, 1); break;
            case 'p': kputs("0x"); put_u64((u64)va_arg(ap, void *), 16, 0); break;
            case '%': kputc('%'); break;
            default:  kputc('%'); kputc(*fmt); break;
        }
    }
    va_end(ap);
}
