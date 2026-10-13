/* Debian95 — Clipboard Viewer: shows the current shared-clipboard text in a
 * sunken client area, word-wrapped, with an Edit menu to clear it. Mirrors the
 * Win95 CLIPBRD.EXE accessory. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/rtl.h>

#define FACE  RGB(0xC0,0xC0,0xC0)
#define WHITE RGB(0xFF,0xFF,0xFF)
#define DK    RGB(0x00,0x00,0x00)

#define IDM_DELETE 1
#define IDM_EXIT   2

static HWND g_win;            /* the single Clipboard Viewer window (Win32 veneer) */

static void cb_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    /* sunken viewing pane */
    int px = cx + 4, py = cy + 4, pw = cw - 8, ph = ch - 8;
    fb_fill_rect(px, py, pw, ph, WHITE);
    wm_bevel_sunken(px, py, pw, ph);
    const char *s = wm_clipboard_get();
    if (!s || !s[0]) { cc_label(px + 8, py + 8, "(Clipboard is empty)"); return; }
    /* word-wrap into the pane */
    int lh = (int)font_height() + 3, x = px + 6, y = py + 6, right = px + pw - 6;
    char word[64]; int wl = 0;
    for (int i = 0;; i++) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\t' || c == 0) {
            word[wl] = 0;
            if (wl) {
                int ww = (int)font_measure(word);
                if (x + ww > right) { x = px + 6; y += lh; }
                fb_text_t(x, y, word, DK); x += ww + (int)font_measure(" ");
                wl = 0;
            }
            if (c == '\n') { x = px + 6; y += lh; }
            if (c == 0 || y > py + ph - lh) break;
        } else if (wl < 62) word[wl++] = c;
    }
}
static const MenuItemW32 CB_file[] = { { "Exit", IDM_EXIT } };
static const MenuItemW32 CB_edit[] = { { "Delete", IDM_DELETE } };
static const MenuW32 CB_MENUS[] = { { "File", CB_file, 1 }, { "Edit", CB_edit, 1 } };

/* ---- Win32 veneer: WndProc ---- */
static LRESULT CALLBACK Clipbrd_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, CB_MENUS, 2);
            SetWindowIcon(hwnd, 8);   /* generic document icon */
            g_win = hwnd;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            cb_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_DELETE: wm_clipboard_set(""); break;
                case IDM_EXIT:   DestroyWindow(hwnd);  break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void clipbrd_open(void) {
    if (g_win) return;                                    /* single instance */
    CreateAppWindow(220, 150, 360, 240, "Clipboard Viewer", Clipbrd_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 16, "Clipboard Viewer", IC_DOC, 0, clipbrd_open, -1 };
#endif
