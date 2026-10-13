/* Debian95 — Clock (clock.exe analog face).
 *
 * A round analog clock with hour ticks and hour/minute/second hands, drawn
 * with an integer sine table (position 0..59 = 0..354 deg, sin*1024) so it
 * needs no FPU. Reads the system time via rtc_time() + sys_uptime_s(); the
 * second hand sweeps live. Shared win32 stack, both arches.
 */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/icons.h>

#define C_FACE RGB(0xC0, 0xC0, 0xC0)
#define C_DK   RGB(0x00, 0x00, 0x00)

void rtc_time(int *hh, int *mm);
unsigned sys_uptime_s(void);

/* sin(i*6deg)*1024 for i=0..59; cos(a) = SIN60[(a+15)%60] */
static const int SIN60[60] = {
        0,   107,   213,   316,   416,   512,   602,   685,   761,   828,
      887,   935,   974,  1002,  1018,  1024,  1018,  1002,   974,   935,
      887,   828,   761,   685,   602,   512,   416,   316,   213,   107,
        0,  -107,  -213,  -316,  -416,  -512,  -602,  -685,  -761,  -828,
     -887,  -935,  -974, -1002, -1018, -1024, -1018, -1002,  -974,  -935,
     -887,  -828,  -761,  -685,  -602,  -512,  -416,  -316,  -213,  -107,
};
static int sin60(int p) { return SIN60[((p % 60) + 60) % 60]; }
static int cos60(int p) { return SIN60[(((p + 15) % 60) + 60) % 60]; }

static HWND g_win;            /* the single Clock window (Win32 veneer) */

/* draw a hand from centre (cx,cy) at clock position p (0..59), length len,
 * thickness by drawing a few parallel lines */
static void hand(int cx, int cy, int p, int len, int thick, u32 col) {
    int ex = cx + sin60(p) * len / 1024;
    int ey = cy - cos60(p) * len / 1024;
    /* Bresenham line, drawn thick by nudging */
    for (int t = -(thick / 2); t <= thick / 2; t++) {
        int x0 = cx + t, y0 = cy, x1 = ex + t, y1 = ey;
        int dx = x1 - x0, dy = y1 - y0;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        int steps = adx > ady ? adx : ady; if (steps == 0) steps = 1;
        for (int i = 0; i <= steps; i++)
            fb_pixel((u32)(x0 + dx * i / steps), (u32)(y0 + dy * i / steps), col);
    }
}

static void paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, C_FACE);
    int r = (cw < ch ? cw : ch) / 2 - 12;
    int ox = cx + cw / 2, oy = cy + ch / 2;

    /* white filled circle via horizontal spans */
    for (int y = -r; y <= r; y++) {
        int half = 0; while ((half + 1) * (half + 1) + y * y <= r * r) half++;
        fb_fill_rect((u32)(ox - half), (u32)(oy + y), (u32)(2 * half + 1), 1, RGB(0xFF, 0xFF, 0xFF));
    }
    /* rim */
    for (int p = 0; p < 60; p++) {
        int x = ox + sin60(p) * r / 1024, y = oy - cos60(p) * r / 1024;
        fb_fill_rect((u32)x - 1, (u32)y - 1, 2, 2, C_DK);
    }
    /* hour ticks (every 5) */
    for (int p = 0; p < 60; p += 5) {
        int x0 = ox + sin60(p) * (r - 8) / 1024, y0 = oy - cos60(p) * (r - 8) / 1024;
        int x1 = ox + sin60(p) * r / 1024,       y1 = oy - cos60(p) * r / 1024;
        int dx = x1 - x0, dy = y1 - y0, st = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        if (!st) st = 1;
        for (int i = 0; i <= st; i++) fb_fill_rect((u32)(x0 + dx * i / st) - 1, (u32)(y0 + dy * i / st) - 1, 3, 3, C_DK);
    }

    int hh = 12, mm = 0; rtc_time(&hh, &mm);
    int ss = (int)(sys_uptime_s() % 60);
    int hp = ((hh % 12) * 5 + mm / 12) % 60;   /* hour hand position incl. minute offset */

    hand(ox, oy, hp, r * 55 / 100, 4, C_DK);          /* hour   */
    hand(ox, oy, mm, r * 80 / 100, 2, C_DK);          /* minute */
    hand(ox, oy, ss, r * 88 / 100, 0, RGB(0xC0, 0x00, 0x00));  /* second (thin, red) */
    fb_fill_rect((u32)ox - 3, (u32)oy - 3, 6, 6, C_DK);        /* hub */
}

/* ---- Win32 veneer: a display-only WndProc (the face repaints every frame) ---- */
static LRESULT CALLBACK Clock_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetWindowIcon(hwnd, IC_APP); g_win = hwnd; return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void clock_open(void) {
    if (g_win) return;                                    /* single instance */
    CreateAppWindow(300, 150, 200, 220, "Clock", Clock_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 11, "Clock", IC_APP, 0, clock_open, -1 };
#endif
