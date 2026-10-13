/* Debian95 — linear framebuffer graphics (32 bpp). */
#ifndef DEBIAN95_FB_H
#define DEBIAN95_FB_H

#include <debian95/types.h>
#include <debian95/boot.h>

/* colors are 0x00RRGGBB */
#define RGB(r,g,b)  (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

void fb_init(const LoaderBlock *lb);                 /* x86-64: from the VBE LoaderBlock */
void fb_init_direct(u64 fb_virt, u32 w, u32 h, u32 pitch);  /* arch-neutral core */
int  fb_available(void);
u32  fb_width(void);
u32  fb_height(void);
u32  fb_pitch(void);            /* bytes per scanline */
u64  fb_scanout_phys(void);     /* physical base of the scanout buffer (identity-mapped) */

/* double buffering: draw off-screen, then present in one blit (smooth motion) */
void fb_enable_backbuffer(void);
void fb_present(void);

/* damage tracking: primitives expand a dirty bbox so the platform uploads only
 * the changed region. fb_dirty_get -> 1 + rect if anything is dirty, else 0. */
void fb_dirty_reset(void);
int  fb_dirty_get(int *x, int *y, int *w, int *h);

/* a Linux /dev/fb0 client owns the screen: the WM stops compositing while set */
void fb_set_app_owner(int on);
int  fb_app_owner(void);

void fb_clear(u32 color);
void fb_pixel(u32 x, u32 y, u32 color);
u32  fb_get_pixel(u32 x, u32 y);
/* clip rectangle: fb_pixel/fb_fill_rect/fb_blit (and text, which uses fb_pixel)
 * draw only inside it until fb_clear_clip. Used by scrollable list panes. */
void fb_set_clip(int x, int y, int w, int h);
void fb_clear_clip(void);
void fb_free_backbuffer(void);          /* free the back buffer (before a resize) */
/* front-buffer (scanout) helpers for the damage-limited software cursor */
void fb_front_pixel(int x, int y, u32 c);
void fb_flush_rect(int x, int y, int w, int h);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 color);          /* 1px outline */
void fb_blit(int x, int y, int w, int h, const u32 *pixels);  /* copy an image (clipped) */
void fb_char(u32 x, u32 y, char c, u32 fg, u32 bg);
void fb_text(u32 x, u32 y, const char *s, u32 fg, u32 bg);
void fb_text2x(u32 x, u32 y, const char *s, u32 fg, u32 bg);  /* 2x scaled */
/* transparent variants: only foreground pixels are drawn (background untouched) */
void fb_char_t(u32 x, u32 y, char c, u32 fg);
void fb_text_t(u32 x, u32 y, const char *s, u32 fg);
void fb_char_bold_t(u32 x, u32 y, char c, u32 fg);      /* synthesised bold (Win95 captions) */
void fb_text_bold_t(u32 x, u32 y, const char *s, u32 fg);
u32  fb_measure_bold(const char *s);
u32  fb_measure(const char *s);                        /* pixel width, loaded font */
void fb_text2x_t(u32 x, u32 y, const char *s, u32 fg);
void fb_text2x_builtin(u32 x, u32 y, const char *s, u32 fg);   /* brand wordmark: 8x8 font */

#endif /* DEBIAN95_FB_H */
