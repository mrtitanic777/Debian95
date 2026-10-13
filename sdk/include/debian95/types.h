/* Debian95 — fundamental fixed-width types (freestanding). */
#ifndef DEBIAN95_TYPES_H
#define DEBIAN95_TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef short               i16;
typedef int                 i32;
typedef long long           i64;

typedef u64                 uptr;   /* pointer-sized unsigned (x86-64) */
typedef u64                 usize;

#ifndef __cplusplus
#define NULL   ((void*)0)
#define true   1
#define false  0
typedef _Bool bool;
#endif

#endif /* DEBIAN95_TYPES_H */
