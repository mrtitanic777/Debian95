/* Debian95 — Font Preview: renders a REAL sample of the selected font using the
 * public-domain stb_truetype rasterizer, drawn straight to the framebuffer as
 * antialiased black-on-white (no faked preview — these are the font's own
 * glyphs). Opened from Control Panel > Fonts (Preview). */
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <debian95/win32.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HWND g_win;
static unsigned char *g_ttf; static long g_ttflen;
static stbtt_fontinfo g_font; static int g_ok;
static char g_fam[64];

static void fp_load(const char *file) {
    if (g_ttf) { free(g_ttf); g_ttf = 0; } g_ok = 0;
    FILE *f = fopen(file, "rb"); if (!f) return;
    fseek(f, 0, SEEK_END); g_ttflen = ftell(f); fseek(f, 0, SEEK_SET);
    if (g_ttflen <= 0 || g_ttflen > 16 * 1024 * 1024) { fclose(f); return; }
    g_ttf = malloc(g_ttflen); if (!g_ttf) { fclose(f); return; }
    if (fread(g_ttf, 1, g_ttflen, f) != (size_t)g_ttflen) { fclose(f); free(g_ttf); g_ttf = 0; return; }
    fclose(f);
    int off = stbtt_GetFontOffsetForIndex(g_ttf, 0); if (off < 0) return;
    if (stbtt_InitFont(&g_font, g_ttf, off)) g_ok = 1;
}

/* one line of the sample string at pixel height `px`; returns the next y. */
static int fp_line(int x, int y, int px, const char *s) {
    float sc = stbtt_ScaleForPixelHeight(&g_font, (float)px);
    int asc, desc, gap; stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
    int baseline = y + (int)(asc * sc), cx = x;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p, aw, lsb;
        stbtt_GetCodepointHMetrics(&g_font, c, &aw, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&g_font, c, sc, sc, &x0, &y0, &x1, &y1);
        int bw = x1 - x0, bh = y1 - y0;
        if (bw > 0 && bh > 0 && bw < 1024 && bh < 1024) {
            unsigned char *bmp = malloc((size_t)bw * bh);
            if (bmp) {
                stbtt_MakeCodepointBitmap(&g_font, bmp, bw, bh, bw, sc, sc, c);
                for (int yy = 0; yy < bh; yy++)
                    for (int xx = 0; xx < bw; xx++) {
                        int cov = bmp[yy * bw + xx]; if (!cov) continue;
                        int sh = 255 - cov;                 /* antialiased black on white */
                        fb_pixel((u32)(cx + (int)(lsb * sc) + x0 + xx), (u32)(baseline + y0 + yy), RGB(sh, sh, sh));
                    }
                free(bmp);
            }
        }
        cx += (int)(aw * sc);
    }
    return baseline + (int)(-desc * sc) + 4;
}

static void fp_paint(int ox, int oy, int cw, int ch) {
    fb_set_clip(ox, oy, cw, ch);
    fb_fill_rect(ox, oy, cw, ch, RGB(0xFF, 0xFF, 0xFF));
    int y = oy + 6;
    fb_text_t(ox + 8, y, g_fam, RGB(0, 0, 0)); y += 16;
    fb_fill_rect(ox + 6, y, cw - 12, 1, RGB(0xC0, 0xC0, 0xC0)); y += 6;
    if (!g_ok) { fb_text_t(ox + 8, y, "This font file could not be rendered.", RGB(0, 0, 0)); fb_clear_clip(); return; }
    static const char *S = "The quick brown fox jumps over the lazy dog.";
    static const char *A = "ABCDEFGHIJKLM abcdefghijklm 0123456789";
    static const int sizes[] = { 12, 16, 20, 28, 40, 54 };
    fb_text_t(ox + 8, y, A, RGB(0x80, 0x80, 0x80)); y += 14;
    for (int i = 0; i < 6 && y < oy + ch - 4; i++) y = fp_line(ox + 8, y, sizes[i], S);
    fb_clear_clip();
}

static LRESULT CALLBACK FP_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: g_win = hwnd; return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            fp_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_DESTROY: g_win = 0; if (g_ttf) { free(g_ttf); g_ttf = 0; } g_ok = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

/* open (or refresh) the preview for a font family + its file. */
void fontprev_open(const char *file, const char *family) {
    snprintf(g_fam, sizeof g_fam, "%s", family ? family : "");
    fp_load(file);
    if (g_win) InvalidateRect(g_win);
    else CreateAppWindow(150, 100, 560, 380, "Font Preview", FP_WndProc, 0);
}
