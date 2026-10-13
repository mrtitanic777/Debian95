/* Debian95 — linear framebuffer driver (32 bpp). Arch-neutral: the arch supplies
 * an already-mapped framebuffer virtual address via fb_init_direct (x86-64 from
 * the VBE LoaderBlock, ARM64 from ramfb), then all drawing is shared. */
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/krt.h>

static volatile u8 *g_fb;      /* the real screen */
static volatile u8 *g_draw;    /* current draw target (screen or back buffer) */
static volatile u8 *g_back;    /* off-screen buffer, if enabled */
static u32 g_w, g_h, g_pitch;

/* ---- damage tracking ------------------------------------------------------
 * Every primitive expands a dirty bounding box (half-open [x0,x1) x [y0,y1)).
 * The present path uploads only this region to the screen instead of the whole
 * ~4MB frame, so a plain mouse move touches a few hundred pixels, not millions.
 * Empty when x1<=x0. */
static int g_dx0, g_dy0, g_dx1, g_dy1;
void fb_dirty_reset(void) { g_dx0 = 0x7fffffff; g_dy0 = 0x7fffffff; g_dx1 = -0x7fffffff; g_dy1 = -0x7fffffff; }
static void dirty_add(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > (int)g_w) x1 = (int)g_w; if (y1 > (int)g_h) y1 = (int)g_h;
    if (x1 <= x0 || y1 <= y0) return;
    if (x0 < g_dx0) g_dx0 = x0; if (y0 < g_dy0) g_dy0 = y0;
    if (x1 > g_dx1) g_dx1 = x1; if (y1 > g_dy1) g_dy1 = y1;
}
/* Return 1 and fill *x,*y,*w,*h with the clamped dirty rect; 0 if nothing dirty. */
int fb_dirty_get(int *x, int *y, int *w, int *h) {
    if (g_dx1 <= g_dx0 || g_dy1 <= g_dy0) return 0;
    *x = g_dx0; *y = g_dy0; *w = g_dx1 - g_dx0; *h = g_dy1 - g_dy0; return 1;
}

/* Point the driver at an already-mapped 32bpp framebuffer. */
void fb_init_direct(u64 fb_virt, u32 w, u32 h, u32 pitch) {
    g_fb = (volatile u8 *)fb_virt;
    g_draw = g_fb;
    g_w = w; g_h = h; g_pitch = pitch;
    /* Seed the damage box to the whole screen: a freshly (re)created texture has
     * undefined contents, so the first present after init/resize must upload it
     * all before any damage-limited (cursor-only) frame can run. */
    g_dx0 = 0; g_dy0 = 0; g_dx1 = (int)w; g_dy1 = (int)h;
}

void fb_enable_backbuffer(void) {
    if (!g_fb || g_back) return;
    g_back = (volatile u8 *)kmalloc((usize)g_pitch * g_h);
    if (g_back) g_draw = g_back;
}
/* free the back buffer (before a resolution change re-sizes the framebuffer) */
void fb_free_backbuffer(void) {
    if (g_back) { kfree((void *)g_back); g_back = 0; }
    g_draw = g_fb;
}

static volatile int g_app_owner;   /* a fbdev app owns the screen: WM pauses compositing */

void fb_present(void) {
    if (g_app_owner) return;                 /* the Linux fbdev app draws straight to g_fb */
    if (!g_back) return;
    memcpy((void *)g_fb, (void *)g_back, (usize)g_pitch * g_h);
}

/* ---- front-buffer (scanout) helpers for the damage-limited software cursor ----
 * The cursor is composited straight onto the scanout so a plain mouse move only
 * touches the cursor's small rectangles instead of re-copying the whole frame
 * (which flickers on QEMU's deferred-flush framebuffer). */
void fb_front_pixel(int x, int y, u32 c) {
    if (!g_fb || x < 0 || y < 0 || x >= (int)g_w || y >= (int)g_h) return;
    *(volatile u32 *)(g_fb + (u64)y * g_pitch + (u64)x * 4) = c;
    dirty_add(x, y, x + 1, y + 1);
}
/* copy a rectangle from the back buffer to the scanout (erases the cursor) */
void fb_flush_rect(int x, int y, int w, int h) {
    if (!g_fb || !g_back) return;
    dirty_add(x, y, x + w, y + h);
    for (int j = 0; j < h; j++) { int py = y + j; if (py < 0 || py >= (int)g_h) continue;
        volatile u32 *d = (volatile u32 *)(g_fb + (u64)py * g_pitch);
        volatile u32 *s = (volatile u32 *)(g_back + (u64)py * g_pitch);
        for (int i = 0; i < w; i++) { int px = x + i; if (px < 0 || px >= (int)g_w) continue; d[px] = s[px]; }
    }
}

int fb_available(void) { return g_fb != 0; }
u32 fb_width(void)  { return g_w; }
u32 fb_height(void) { return g_h; }
u32 fb_pitch(void)  { return g_pitch; }

/* Physical base of the scanout buffer. On both arches the framebuffer is mapped
 * identity (virt==phys: x86 VBE LFB below 4GiB, ARM ramfb in low RAM), so the
 * mapped virtual address doubles as the physical address a Linux app can mmap. */
u64 fb_scanout_phys(void) { return (u64)g_fb; }

/* let a Linux /dev/fb0 client take/release the screen (pauses/resumes the WM). */
void fb_set_app_owner(int on) { g_app_owner = on ? 1 : 0; }
int  fb_app_owner(void)       { return g_app_owner; }

/* optional clip rectangle honoured by fb_pixel/fb_fill_rect/fb_blit */
static int g_clip_on, g_clx0, g_cly0, g_clx1, g_cly1;
void fb_set_clip(int x, int y, int w, int h) { g_clip_on = 1; g_clx0 = x; g_cly0 = y; g_clx1 = x + w; g_cly1 = y + h; }
void fb_clear_clip(void) { g_clip_on = 0; }
static int clip_out(int x, int y) { return g_clip_on && (x < g_clx0 || y < g_cly0 || x >= g_clx1 || y >= g_cly1); }

void fb_pixel(u32 x, u32 y, u32 color) {
    if (!g_fb || x >= g_w || y >= g_h) return;
    if (clip_out((int)x, (int)y)) return;
    *(volatile u32 *)(g_draw + (u64)y * g_pitch + (u64)x * 4) = color;
    dirty_add((int)x, (int)y, (int)x + 1, (int)y + 1);
}

u32 fb_get_pixel(u32 x, u32 y) {
    if (!g_fb || x >= g_w || y >= g_h) return 0;
    return *(volatile u32 *)(g_draw + (u64)y * g_pitch + (u64)x * 4) & 0x00FFFFFF;
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!g_fb) return;
    int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w), y1 = (int)(y + h);
    if (g_clip_on) { if (x0 < g_clx0) x0 = g_clx0; if (y0 < g_cly0) y0 = g_cly0;
                     if (x1 > g_clx1) x1 = g_clx1; if (y1 > g_cly1) y1 = g_cly1; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    for (int py = y0; py < y1 && py < (int)g_h; py++) {
        volatile u32 *row = (volatile u32 *)(g_draw + (u64)py * g_pitch);
        for (int px = x0; px < x1 && px < (int)g_w; px++) row[px] = color;
    }
    dirty_add(x0, y0, x1, y1);
}

void fb_clear(u32 color) { fb_fill_rect(0, 0, g_w, g_h, color); }

void fb_blit(int x, int y, int w, int h, const u32 *px) {
    if (!g_fb) return;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= (int)g_h) continue;
        volatile u32 *row = (volatile u32 *)(g_draw + (u64)py * g_pitch);
        const u32 *src = px + (u64)j * w;
        for (int i = 0; i < w; i++) {
            int pxx = x + i;
            if (pxx >= 0 && pxx < (int)g_w && !clip_out(pxx, py)) row[pxx] = src[i];
        }
    }
    dirty_add(x, y, x + w, y + h);
}

void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    fb_fill_rect(x, y, w, 1, color);
    fb_fill_rect(x, y + h - 1, w, 1, color);
    fb_fill_rect(x, y, 1, h, color);
    fb_fill_rect(x + w - 1, y, 1, h, color);
}

/* All glyphs come from the active font (built-in 8x8 or a loaded .LGF). */

void fb_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    if (!g_fb) return;
    u8 adv; const u16 *rows = font_glyph(c, &adv);
    u16 h = font_height(), mw = font_max_width();
    fb_fill_rect(x, y, adv, h, bg);
    if (!rows) return;
    for (u16 r = 0; r < h; r++)
        for (u16 col = 0; col < mw; col++)
            if ((rows[r] >> col) & 1) fb_pixel(x + col, y + r, fg);
}

void fb_text(u32 x, u32 y, const char *s, u32 fg, u32 bg) {
    for (; *s; s++) { u8 adv; font_glyph(*s, &adv); fb_char(x, y, *s, fg, bg); x += adv; }
}

void fb_char_t(u32 x, u32 y, char c, u32 fg) {
    if (!g_fb) return;
    u8 adv; const u16 *rows = font_glyph(c, &adv);
    if (!rows) return;
    u16 h = font_height(), mw = font_max_width();
    for (u16 r = 0; r < h; r++)
        for (u16 col = 0; col < mw; col++)
            if ((rows[r] >> col) & 1) fb_pixel(x + col, y + r, fg);
}

void fb_text_t(u32 x, u32 y, const char *s, u32 fg) {
    for (; *s; s++) { u8 adv; font_glyph(*s, &adv); fb_char_t(x, y, *s, fg); x += adv; }
}

/* Bold, transparent: GDI synthesises bold for a bitmap font by OR-ing each glyph
 * row with itself shifted one pixel right, and widening the advance by 1. This is
 * exactly how Win95 draws its caption/Start-button text (MS Sans Serif Bold). */
void fb_char_bold_t(u32 x, u32 y, char c, u32 fg) {
    if (!g_fb) return;
    u8 adv; const u16 *rows = font_glyph(c, &adv);
    if (!rows) return;
    u16 h = font_height(), mw = font_max_width();
    for (u16 r = 0; r < h; r++) {
        u32 bits = (u32)rows[r] | ((u32)rows[r] << 1);
        for (u16 col = 0; col <= mw; col++)
            if ((bits >> col) & 1) fb_pixel(x + col, y + r, fg);
    }
}

void fb_text_bold_t(u32 x, u32 y, const char *s, u32 fg) {
    for (; *s; s++) { u8 adv; font_glyph(*s, &adv); fb_char_bold_t(x, y, *s, fg); x += adv + 1; }
}

/* pixel width of a bold string (advance widened by 1 per glyph) */
u32 fb_measure_bold(const char *s) {
    u32 w = 0; for (; *s; s++) { u8 adv; font_glyph(*s, &adv); w += adv + 1; } return w;
}

/* pixel width of a plain string in the loaded proportional font */
u32 fb_measure(const char *s) {
    u32 w = 0; for (; *s; s++) { u8 adv; font_glyph(*s, &adv); w += adv; } return w;
}

void fb_text2x_t(u32 x, u32 y, const char *s, u32 fg) {
    u16 h = font_height(), mw = font_max_width();
    for (; *s; s++) {
        u8 adv; const u16 *rows = font_glyph(*s, &adv);
        if (rows)
            for (u16 r = 0; r < h; r++)
                for (u16 col = 0; col < mw; col++)
                    if ((rows[r] >> col) & 1) fb_fill_rect(x + col * 2, y + r * 2, 2, 2, fg);
        x += adv * 2;
    }
}

/* 2x-scaled text in the built-in 8x8 font, independent of the loaded font. */
void fb_text2x_builtin(u32 x, u32 y, const char *s, u32 fg) {
    for (; *s; s++) {
        u8 adv; const u16 *rows = font_builtin_glyph(*s, &adv);
        if (rows)
            for (u16 r = 0; r < 8; r++)
                for (u16 col = 0; col < 8; col++)
                    if ((rows[r] >> col) & 1) fb_fill_rect(x + col * 2, y + r * 2, 2, 2, fg);
        x += adv * 2;
    }
}

void fb_text2x(u32 x, u32 y, const char *s, u32 fg, u32 bg) {
    u16 h = font_height(), mw = font_max_width();
    for (; *s; s++) {
        u8 adv; const u16 *rows = font_glyph(*s, &adv);
        fb_fill_rect(x, y, adv * 2, h * 2, bg);
        if (rows)
            for (u16 r = 0; r < h; r++)
                for (u16 col = 0; col < mw; col++)
                    if ((rows[r] >> col) & 1) fb_fill_rect(x + col * 2, y + r * 2, 2, 2, fg);
        x += adv * 2;
    }
}
