/* Debian95 — System Log. A real log viewer: it reads the most recent system
 * journal (journalctl, falling back to dmesg) and shows it in a scrollable,
 * monospaced list, with File > Refresh. Ships as an app module. */
#include <debian95/win32.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/icons.h>
#include <stdio.h>
#include <string.h>

#define MAXL 500
#define LW   160
static HWND g_win;
static char g_line[MAXL][LW];
static int  g_nline, g_top;

static void reload(void) {
    g_nline = 0;
    FILE *p = popen("journalctl -n 400 --no-pager -o short 2>/dev/null; dmesg 2>/dev/null | tail -n 400", "r");
    if (p) {
        char buf[256];
        while (g_nline < MAXL && fgets(buf, sizeof buf, p)) {
            buf[strcspn(buf, "\n")] = 0;
            int i = 0; for (; buf[i] && i < LW-1; i++) g_line[g_nline][i] = buf[i]; g_line[g_nline][i] = 0;
            g_nline++;
        }
        pclose(p);
    }
    if (g_nline == 0) { snprintf(g_line[0], LW, "(no log available)"); g_nline = 1; }
}

static int vis_rows(int ch) { int lh = (int)font_height() + 2; return (ch - 8) / lh; }

static int g_sb_x, g_sb_y, g_sb_h;   /* scrollbar geometry recorded by paint, hit by clicks */

static void paint(int ox, int oy, int cw, int ch) {
    fb_fill_rect((u32)ox, (u32)oy, (u32)cw, (u32)ch, RGB(192,192,192));
    int px = ox + 4, py = oy + 4, pw = cw - 8 - 16, ph = ch - 8;
    fb_fill_rect((u32)px, (u32)py, (u32)pw, (u32)ph, RGB(255,255,255));
    wm_bevel_sunken(px, py, pw, ph);
    int lh = (int)font_height() + 2, rows = vis_rows(ch);
    if (g_top > g_nline - rows) g_top = g_nline - rows;
    if (g_top < 0) g_top = 0;
    for (int i = 0; i < rows && g_top + i < g_nline; i++)
        fb_text_t((u32)(px + 4), (u32)(py + 3 + i * lh), g_line[g_top + i], RGB(0,0,0));
    /* scrollbar */
    int sbx = ox + cw - 16 - 2, sby = oy + 4, sbh = ch - 8;
    int track = sbh - 32, th = g_nline > 0 ? track * rows / g_nline : track;
    if (th < 12) th = 12; if (th > track) th = track;
    int maxtop = g_nline - rows; if (maxtop < 1) maxtop = 1;
    int tt = (track - th) * g_top / maxtop;
    cc_scrollbar_v(sbx, sby, sbh, tt, th);
    g_sb_x = sbx; g_sb_y = sby; g_sb_h = sbh;
}

static void on_click(int rx, int ry, int ch) {
    int rows = vis_rows(ch);
    if (rx >= g_sb_x && rx < g_sb_x + 16) {
        if (ry < g_sb_y + 16) g_top -= 3;                     /* up arrow */
        else if (ry > g_sb_y + g_sb_h - 16) g_top += 3;       /* down arrow */
        else if (ry < g_sb_y + g_sb_h/2) g_top -= rows;       /* page up */
        else g_top += rows;                                   /* page down */
        InvalidateRect(g_win);
    }
}

#define IDM_REFRESH 1
#define IDM_EXIT 2
#define IDM_ABOUT 3
static const MenuItemW32 MI_file[] = { { "Refresh", IDM_REFRESH }, { "", -1 }, { "Exit", IDM_EXIT } };
static const MenuItemW32 MI_help[] = { { "About System Log", IDM_ABOUT } };
static const MenuW32 MENUS[] = { { "File", MI_file, 3 }, { "Help", MI_help, 1 } };

static LRESULT CALLBACK Log_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetMenuW32(hwnd, MENUS, 2); SetWindowIcon(hwnd, IC_DOC); g_win = hwnd; return 0;
        case WM_LBUTTONDOWN: { int ox,oy,cw,ch; GetClientOrigin(hwnd,&ox,&oy,&cw,&ch);
            on_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), ch); return 0; }
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox,oy,cw,ch; GetClientOrigin(hwnd,&ox,&oy,&cw,&ch); paint(ox,oy,cw,ch); EndPaint(hwnd, ps.hdc); return 0; }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_REFRESH: reload(); g_top = 1<<30; InvalidateRect(hwnd); break;   /* re-read, jump to end */
                case IDM_EXIT: DestroyWindow(hwnd); break;
                case IDM_ABOUT: MessageBoxW32(hwnd, "System Log\nDebian95 Desktop Edition", "About", 0); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void logview_open(void) {
    if (g_win) return;
    reload(); g_top = 1<<30;                                  /* start at the most recent */
    CreateAppWindow(140, 100, 620, 420, "System Log", Log_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 63, "System Log", IC_DOC, 0, logview_open, -1, 0 };
#endif
