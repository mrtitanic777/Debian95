#ifndef _DEBIAN95_SHIM_STDLIB_H
#define _DEBIAN95_SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t); void free(void *); void *realloc(void *, size_t);
int abs(int);
#endif
