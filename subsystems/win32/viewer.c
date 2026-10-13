/* Debian95 — image viewer. Opens an .LGB image in a window and blits it into
 * the client area. Used by the File Manager's file associations and downloads. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/nt.h>
#include <debian95/img.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>

#define VW_BG RGB(0x18, 0x18, 0x18)

static HWND g_vw;             /* the single Image Viewer window (Win32 veneer) */
static u8   *g_data; static u32 g_len;
static u32  *g_decoded;                 /* kmalloc'd RGB from a BMP/ICO decode */
static const u32 *g_px; static int g_w, g_h;
static char g_name[16];

static int ext_is(const char *n, const char *e3) {
    int L = 0; while (n[L]) L++;
    if (L < 4 || n[L-4] != '.') return 0;
    return n[L-3] == e3[0] && n[L-2] == e3[1] && n[L-1] == e3[2];
}

static void v_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, VW_BG);
    if (g_px) {
        int x = cx + (cw - g_w) / 2, y = cy + (ch - g_h) / 2;
        if (x < cx) x = cx; if (y < cy) y = cy;
        fb_blit(x, y, g_w, g_h, g_px);       /* fb_blit clips to the screen */
    } else {
        fb_text_t(cx + 10, cy + 10, "cannot display this image", RGB(0xFF, 0xC0, 0xC0));
        fb_text_t(cx + 10, cy + 28, g_name, RGB(0xE0, 0xE0, 0xE0));
    }
}

/* ---- Win32 veneer: WndProc (display-only; the client blits the decoded image) ---- */
static LRESULT CALLBACK Viewer_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetWindowIcon(hwnd, 8); g_vw = hwnd; return 0;   /* generic image icon */
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            v_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_DESTROY:
            if (g_decoded) { kfree(g_decoded); g_decoded = 0; }
            if (g_data) { kfree(g_data); g_data = 0; }
            g_px = 0; g_w = g_h = 0; g_vw = 0;
            return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void viewer_open(const char *name) {
    int i = 0; for (; name[i] && i < 15; i++) g_name[i] = name[i]; g_name[i] = 0;
    wm_add_recent(name);                          /* Start > Documents MRU */
    g_px = 0; g_w = g_h = 0;
    if (g_decoded) { kfree(g_decoded); g_decoded = 0; }
    if (g_data) { kfree(g_data); g_data = 0; }

    if (FsReadAlloc(name, &g_data, &g_len) == 0) {
        u32 *px; int w, h;
        int is_png = g_len >= 8 && g_data[0] == 137 && g_data[1] == 'P' && g_data[2] == 'N' && g_data[3] == 'G';
        int is_qoi = g_len >= 14 && g_data[0] == 'q' && g_data[1] == 'o' && g_data[2] == 'i' && g_data[3] == 'f';
        int is_gif = g_len >= 6 && g_data[0] == 'G' && g_data[1] == 'I' && g_data[2] == 'F';
        int is_jpg = g_len >= 3 && g_data[0] == 0xFF && g_data[1] == 0xD8 && g_data[2] == 0xFF;
        if (is_png && png_decode(g_data, g_len, &px, &w, &h) == 0) {
            g_decoded = px; g_px = px; g_w = w; g_h = h;
        } else if (is_qoi && qoi_decode(g_data, g_len, &px, &w, &h) == 0) {
            g_decoded = px; g_px = px; g_w = w; g_h = h;
        } else if (is_gif && gif_decode(g_data, g_len, &px, &w, &h) == 0) {
            g_decoded = px; g_px = px; g_w = w; g_h = h;
        } else if (is_jpg && jpeg_decode(g_data, g_len, &px, &w, &h) == 0) {
            g_decoded = px; g_px = px; g_w = w; g_h = h;
        } else if (ext_is(name, "BMP") && bmp_decode(g_data, g_len, &px, &w, &h) == 0) {
            g_decoded = px; g_px = px; g_w = w; g_h = h;
        } else if (ext_is(name, "ICO") && ico_decode(g_data, g_len, VW_BG, &px, &w, &h) == 0) {
            g_decoded = px; g_px = px; g_w = w; g_h = h;
        } else if (g_len >= 8 && g_data[0] == 'L' && g_data[1] == 'G' && g_data[2] == 'B' && g_data[3] == '1') {
            int lw = g_data[4] | (g_data[5] << 8), lh = g_data[6] | (g_data[7] << 8);
            if ((u32)8 + (u32)lw * lh * 4 <= g_len) { g_px = (const u32 *)(g_data + 8); g_w = lw; g_h = lh; }
        }
    }
    int ww = g_w ? g_w + 20 : 320, wh = g_h ? g_h + 44 : 120;
    if (ww < 200) ww = 200; if (ww > 900) ww = 900; if (wh > 700) wh = 700;
    if (g_vw) SetWindowSize(g_vw, ww, wh);              /* reuse the window for the new image */
    else CreateAppWindow(180, 90, ww, wh, "Image Viewer", Viewer_WndProc, 0);
}
