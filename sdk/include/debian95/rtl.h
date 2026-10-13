/* Debian95 — kernel runtime library (freestanding): I/O ports, string, console. */
#ifndef DEBIAN95_RTL_H
#define DEBIAN95_RTL_H

#include <debian95/types.h>

/* ---- x86 port I/O (x86-64 only; lets rtl.h's string/console decls be included
 *      by arch-neutral code that compiles for ARM64 too) ---- */
#ifdef __x86_64__
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(u16 port, u16 v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(u16 port, u32 v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}
#endif /* __x86_64__ */

/* ---- string / memory ---- */
void *memset(void *dst, int c, usize n);
void *memcpy(void *dst, const void *src, usize n);
usize strlen(const char *s);
int   strcmp(const char *a, const char *b);

/* ---- serial (COM1) ---- */
void serial_init(void);
void serial_putc(char c);

/* ---- VGA text (0xB8000) ---- */
void vga_init(void);
void vga_putc(char c);
void vga_set_color(u8 fg, u8 bg);

/* ---- console: writes to both serial and VGA ---- */
void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);

#endif /* DEBIAN95_RTL_H */
