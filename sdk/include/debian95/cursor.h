/* Debian95 — cursor bitmaps (2-bit: 0=transparent 1=black 2=white 3=gray). */
#ifndef DEBIAN95_CURSOR_H
#define DEBIAN95_CURSOR_H

#include <debian95/types.h>

typedef struct {
    u8 w, h;            /* dimensions            */
    u8 hotx, hoty;      /* hotspot within bitmap */
    const u8 *pix;      /* w*h 2-bit codes, row-major */
} Debian95Cursor;

#endif /* DEBIAN95_CURSOR_H */
