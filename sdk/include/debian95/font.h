/* Debian95 — pluggable bitmap font system. A built-in 8x8 font is the default;
 * a .LGF font (proportional-capable) can be loaded from disk to replace it.
 *
 * .LGF v1 layout (little-endian):
 *   0:  'L','G','F','1'
 *   4:  u16 height
 *   6:  u16 first_char
 *   8:  u16 count
 *   10: u16 max_width          (<= 16)
 *   12: u8  flags              (bit0 = proportional)
 *   13: u8  reserved
 *   14: u16 reserved
 *   16: glyph table: count entries, each { u8 advance; u8 pad; u16 rows[height] }
 *       (row bit 0 = leftmost pixel)  stride = 2 + height*2
 */
#ifndef DEBIAN95_FONT_H
#define DEBIAN95_FONT_H

#include <debian95/types.h>

void font_use_builtin(void);
int  font_load_lgf(const void *buf, u32 len);       /* load slot 0 (UI); keeps a pointer into buf */
int  font_load_console(const void *buf, u32 len);   /* load slot 1 (console) */
void font_select(int slot);                          /* 0 = UI (default), 1 = console */
/* Load a native Windows .FON into `slot`, picking the strike nearest target_h. */
int  font_load_fon(const void *buf, u32 len, int slot, int target_h);

u16  font_height(void);
u16  font_max_width(void);
/* glyph bitmap for char c: returns pointer to `height` u16 rows and sets the
 * advance width; NULL if c has no glyph. */
const u16 *font_glyph(char c, u8 *advance);
/* built-in 8x8 glyph regardless of the loaded font (for the brand wordmark) */
const u16 *font_builtin_glyph(char c, u8 *advance);
u32  font_measure(const char *s);               /* pixel width of a string */

#endif /* DEBIAN95_FONT_H */
