#ifndef _DEBIAN95_SHIM_STRING_H
#define _DEBIAN95_SHIM_STRING_H
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memmove(void *, const void *, size_t);
size_t strlen(const char *);
#endif
