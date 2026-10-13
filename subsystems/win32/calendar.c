/* Debian95 — Calendar. An authentic Win-style accessory showing a real monthly
 * calendar from the system clock, with the current day highlighted and
 * month navigation. Ships as a hot-swappable app module. */
#include <debian95/appsdk.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static HWND g_win;
static int  g_y, g_m;                 /* displayed year, month (0-11) */

static const char *MON[12] = { "January", "February", "March", "April", "May", "June",
                               "July", "August", "September", "October", "November", "December" };

static int days_in(int y, int m) {
    static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m];
}
static int first_wday(int y, int m) {
    struct tm t; memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m; t.tm_mday = 1; t.tm_hour = 12;
    mktime(&t); return t.tm_wday;     /* 0 = Sunday */
}
static void today(int *y, int *m, int *d) {
    time_t t = time(0); struct tm *lt = localtime(&t);
    *y = lt->tm_year + 1900; *m = lt->tm_mon; if (d) *d = lt->tm_mday;
}

static void paint(int ox, int oy, int cw, int ch) {
    fb_fill_rect((u32)ox, (u32)oy, (u32)cw, (u32)ch, RGB(192, 192, 192));
    char hdr[48]; snprintf(hdr, sizeof hdr, "%s %d", MON[g_m], g_y);
    int hw = (int)font_measure(hdr);
    fb_text_t((u32)(ox + (cw - hw) / 2), (u32)(oy + 8), hdr, RGB(0, 0, 128));

    static const char *WD[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    int gx = ox + 10, gy = oy + 30, cwid = (cw - 20) / 7, rh = (ch - 56) / 6;
    for (int i = 0; i < 7; i++)
        fb_text_t((u32)(gx + i * cwid + 6), (u32)gy, WD[i], i == 0 ? RGB(192, 0, 0) : RGB(0, 0, 0));

    int gridy = gy + 18;
    wm_bevel_sunken(gx, gridy, cwid * 7, rh * 6);
    fb_fill_rect((u32)gx + 2, (u32)gridy + 2, (u32)(cwid * 7 - 4), (u32)(rh * 6 - 4), RGB(255, 255, 255));

    int fw = first_wday(g_y, g_m), dim = days_in(g_y, g_m);
    int ty, tm, td; today(&ty, &tm, &td);
    int col = fw, row = 0;
    for (int d = 1; d <= dim; d++) {
        int cx = gx + col * cwid, cy = gridy + 2 + row * rh;
        char ds[12]; snprintf(ds, sizeof ds, "%d", d);
        if (g_y == ty && g_m == tm && d == td) {
            fb_fill_rect((u32)cx + 3, (u32)cy + 1, (u32)cwid - 6, (u32)rh - 2, RGB(0, 0, 128));
            fb_text_t((u32)(cx + 8), (u32)(cy + 3), ds, RGB(255, 255, 255));
        } else {
            fb_text_t((u32)(cx + 8), (u32)(cy + 3), ds, col == 0 ? RGB(192, 0, 0) : RGB(0, 0, 0));
        }
        if (++col == 7) { col = 0; row++; }
    }
}

#define IDM_PREV  1
#define IDM_NEXT  2
#define IDM_TODAY 3
#define IDM_EXIT  4
#define IDM_ABOUT 5
static const MenuItemW32 MI_month[] = {
    { "Previous Month", IDM_PREV }, { "Next Month", IDM_NEXT }, { "Today", IDM_TODAY }, { "Exit", IDM_EXIT } };
static const MenuItemW32 MI_help[] = { { "About Calendar", IDM_ABOUT } };
static const MenuW32 MENUS[] = { { "Month", MI_month, 4 }, { "Help", MI_help, 1 } };

static LRESULT CALLBACK Calendar_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, MENUS, 2);
            SetWindowIcon(hwnd, IC_APP);
            today(&g_y, &g_m, 0);
            g_win = hwnd;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_PREV:  if (--g_m < 0)  { g_m = 11; g_y--; } InvalidateRect(hwnd); break;
                case IDM_NEXT:  if (++g_m > 11) { g_m = 0;  g_y++; } InvalidateRect(hwnd); break;
                case IDM_TODAY: today(&g_y, &g_m, 0); InvalidateRect(hwnd); break;
                case IDM_EXIT:  DestroyWindow(hwnd); break;
                case IDM_ABOUT: MessageBoxW32(hwnd, "Calendar\nDebian95 Desktop Edition", "About", 0); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void calendar_open(void) {
    if (g_win) return;                                    /* single instance */
    CreateAppWindow(240, 130, 322, 300, "Calendar", Calendar_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 61, "Calendar", IC_APP, 0, calendar_open, -1 };
#endif
