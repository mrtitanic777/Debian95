/* Debian95 — Character Map accessory: a grid of the font's glyphs; click to add
 * a character to the "Characters to copy" field. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/rtl.h>

#define FIRST 32
#define LAST  126
#define COLS  19
#define CELLW 20
#define CELLH 20
#define GX    8
#define GY    8
#define FACE  RGB(0xC0,0xC0,0xC0)
#define WHITE RGB(0xFF,0xFF,0xFF)
#define DK    RGB(0x00,0x00,0x00)

static HWND g_win;            /* the single Character Map window (Win32 veneer) */
static char g_picked[40]; static int g_plen;
static int  g_sel = -1;

static int grid_rows(void) { int n = LAST - FIRST + 1; return (n + COLS - 1) / COLS; }
static int field_y(void)   { return GY + grid_rows() * CELLH + 12; }

static void cm_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    int n = LAST - FIRST + 1;
    for (int i = 0; i < n; i++) {
        int col = i % COLS, row = i / COLS;
        int x = cx + GX + col * CELLW, y = cy + GY + row * CELLH;
        fb_fill_rect(x, y, CELLW - 1, CELLH - 1, WHITE);
        if (i == g_sel) wm_bevel_sunken(x, y, CELLW - 1, CELLH - 1);
        else wm_bevel_raised(x, y, CELLW - 1, CELLH - 1);
        char c = (char)(FIRST + i); u8 adv; font_glyph(c, &adv);
        fb_char_t(x + (CELLW - 1 - adv) / 2, y + (CELLH - 1 - (int)font_height()) / 2, c, DK);
    }
    int fy = cy + field_y();
    cc_label(cx + 8, fy + 4, "Characters to copy:");
    cc_edit(cx + 132, fy, cw - 132 - 84, 20, g_picked, 0);
    cc_button(cx + cw - 76, fy, 68, 22, "Copy", 0);
}
static void cm_click(int rx, int ry) {
    int n = LAST - FIRST + 1;
    int col = (rx - GX) / CELLW, row = (ry - GY) / CELLH;
    if (rx >= GX && col >= 0 && col < COLS && row >= 0) {
        int i = row * COLS + col;
        if (i >= 0 && i < n && ry < GY + grid_rows() * CELLH) {
            g_sel = i;
            if (g_plen < 38) { g_picked[g_plen++] = (char)(FIRST + i); g_picked[g_plen] = 0; }
            return;
        }
    }
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
    int fy = field_y();
    if (ry >= fy && ry < fy + 22 && rx >= cw - 76) {     /* Copy button -> shared clipboard */
        if (g_plen) wm_clipboard_set(g_picked);
    }
}
/* ---- Win32 veneer: WndProc ---- */
static LRESULT CALLBACK CharMap_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetWindowIcon(hwnd, 8); g_win = hwnd; return 0;   /* IC_DOC-ish generic */
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            cm_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: cm_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void charmap_open(void) {
    if (g_win) return;                                    /* single instance */
    /* window height = client content (field + button + padding) + title/border chrome */
    CreateAppWindow(240, 140, 8 + COLS * CELLW + 8, field_y() + 20 + 14 + 25, "Character Map", CharMap_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 14, "Character Map", IC_DOC, 0, charmap_open, -1 };
#endif
