/* Debian95 — freestanding string/memory routines. */
#include <debian95/rtl.h>

void *memset(void *dst, int c, usize n) {
    u8 *d = (u8 *)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, usize n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dst;
}

usize strlen(const char *s) {
    usize n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}
