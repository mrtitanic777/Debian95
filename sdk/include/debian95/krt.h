/* Debian95 — portable kernel runtime contract. The arch-agnostic executive
 * (Ob, Io, ...) depends only on these primitives, which each arch provides
 * (x86-64 via lib/rtl + kernel/mm; ARM64 via arch/arm64/klib.c + mem.c). This
 * lets one copy of the executive compile for every architecture, instead of
 * pulling in rtl.h (which carries arch-specific port-I/O inline asm). */
#ifndef DEBIAN95_KRT_H
#define DEBIAN95_KRT_H

#include <debian95/types.h>

void  *kmalloc(usize n);
void   kfree(void *p);

void  *memset(void *dst, int c, usize n);
void  *memcpy(void *dst, const void *src, usize n);
usize  strlen(const char *s);
int    strcmp(const char *a, const char *b);

void   kputc(char c);                          /* one char to the kernel console */
void   kprintf(const char *fmt, ...);          /* %c %s %d %u %x %% */

#endif /* DEBIAN95_KRT_H */
