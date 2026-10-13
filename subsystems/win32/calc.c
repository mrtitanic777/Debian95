/* Debian95 — Calculator (calc.exe Standard view), written in the Win32 idiom:
 * a single WndProc, WM_PAINT via a GDI HDC, WM_LBUTTONDOWN / WM_CHAR input, and
 * an Edit/Help menu firing WM_COMMAND. The compute engine (the Model) is the
 * original fixed-point int64 code, unchanged — only the View/input plumbing was
 * migrated to Win32, so the arithmetic behaves exactly as before. Serves as the
 * reference for migrating an existing app to the WndProc pattern. */
#include <debian95/win32.h>
#include <debian95/wm.h>       /* wm_clipboard_set/get */
#include <debian95/font.h>     /* text metrics */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SCALE 1000000LL
#define C_FACE   RGB(0xC0, 0xC0, 0xC0)
#define C_BLUE   RGB(0x00, 0x00, 0x80)
#define C_RED    RGB(0x80, 0x00, 0x00)
#define C_DK     RGB(0x00, 0x00, 0x00)
#define C_WHITE  RGB(0xFF, 0xFF, 0xFF)

typedef long long i64;

/* ============================ engine (Model) ============================== */
static char g_disp[24] = "0";
static i64  g_acc;
static char g_op;
static int  g_entering;
static i64  g_mem;
static int  g_err;
static int  g_sci;        /* 0 = Standard view, 1 = Scientific */
static int  g_rad;        /* 0 = degrees, 1 = radians */
static int  g_inv;        /* Inv modifier: next function uses its inverse */
static HWND g_calc_hwnd;

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static i64 parse_disp(void) {
    const char *s = g_disp; int neg = 0; i64 ip = 0, fp = 0, fdiv = 1;
    if (*s == '-') { neg = 1; s++; }
    while (*s && *s != '.') { ip = ip * 10 + (*s - '0'); s++; }
    if (*s == '.') { s++; int d = 0; while (*s && d < 6) { fp = fp * 10 + (*s - '0'); fdiv *= 10; s++; d++; } }
    i64 v = ip * SCALE + (fp * SCALE) / fdiv;
    return neg ? -v : v;
}
static void fmt(i64 v) {
    char *o = g_disp; int neg = v < 0; if (neg) { v = -v; *o++ = '-'; }
    i64 ip = v / SCALE, fp = v % SCALE;
    char tmp[24]; int n = 0;
    if (ip == 0) tmp[n++] = '0';
    while (ip > 0) { tmp[n++] = (char)('0' + ip % 10); ip /= 10; }
    for (int i = n - 1; i >= 0; i--) *o++ = tmp[i];
    if (fp > 0) {
        char fr[8]; for (int i = 5; i >= 0; i--) { fr[i] = (char)('0' + fp % 10); fp /= 10; }
        int last = 5; while (last >= 0 && fr[last] == '0') last--;
        *o++ = '.';
        for (int i = 0; i <= last; i++) *o++ = fr[i];
    }
    *o = 0;
}
static i64 isqrt64(i64 n) {
    if (n <= 0) return 0;
    i64 x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}
static void apply(void) {
    i64 b = parse_disp();
    switch (g_op) {
        case '+': g_acc += b; break;
        case '-': g_acc -= b; break;
        case '*': g_acc = (g_acc * b) / SCALE; break;
        case '/': if (b == 0) { g_err = 1; return; } g_acc = (g_acc * SCALE) / b; break;
        default:  g_acc = b; break;
    }
}
static void put_digit(char d) {
    if (g_err) return;
    if (!g_entering) { g_disp[0] = 0; g_entering = 1; }
    int n = slen(g_disp);
    if (n == 0 && d == '0') { g_disp[0] = '0'; g_disp[1] = 0; return; }
    if (n == 1 && g_disp[0] == '0' && d != '.') { g_disp[0] = d; g_disp[1] = 0; return; }
    if (n < 16) { g_disp[n] = d; g_disp[n + 1] = 0; }
}
enum { B_0=0,B_1,B_2,B_3,B_4,B_5,B_6,B_7,B_8,B_9,
       B_DOT, B_ADD, B_SUB, B_MUL, B_DIV, B_EQ, B_SIGN,
       B_SQRT, B_PCT, B_INV, B_BACK, B_CE, B_C,
       B_MC, B_MR, B_MS, B_MPLUS };
static void do_button(int id) {
    if (g_err && id != B_C && id != B_CE) return;
    if (id >= B_1 && id <= B_9) { put_digit((char)('0' + id)); return; }
    switch (id) {
        case B_0: put_digit('0'); break;
        case B_DOT: {
            if (!g_entering) { g_disp[0] = '0'; g_disp[1] = 0; g_entering = 1; }
            int dot = 0; for (int i = 0; g_disp[i]; i++) if (g_disp[i] == '.') dot = 1;
            if (!dot) { int n = slen(g_disp); g_disp[n] = '.'; g_disp[n + 1] = 0; }
            break;
        }
        case B_ADD: case B_SUB: case B_MUL: case B_DIV: {
            char op = id == B_ADD ? '+' : id == B_SUB ? '-' : id == B_MUL ? '*' : '/';
            if (g_op && g_entering) apply(); else g_acc = parse_disp();
            if (g_err) return;
            g_op = op; g_entering = 0; fmt(g_acc);
            break;
        }
        case B_EQ:
            if (g_op) { apply(); g_op = 0; }
            if (!g_err) fmt(g_acc);
            g_entering = 0;
            break;
        case B_SIGN:
            if (g_disp[0] == '-') { for (int i = 0; g_disp[i]; i++) g_disp[i] = g_disp[i + 1]; }
            else if (!(g_disp[0] == '0' && g_disp[1] == 0)) {
                for (int i = slen(g_disp); i >= 0; i--) g_disp[i + 1] = g_disp[i];
                g_disp[0] = '-';
            }
            break;
        case B_SQRT: fmt(isqrt64(parse_disp() * SCALE)); g_entering = 0; break;
        case B_PCT: { i64 x = parse_disp(); g_acc = (g_acc * x) / (100 * SCALE); fmt(g_acc); g_entering = 0; break; }
        case B_INV: { i64 x = parse_disp(); if (x == 0) g_err = 1; else fmt((SCALE * SCALE) / x); g_entering = 0; break; }
        case B_BACK: { int n = slen(g_disp); if (n > 0) g_disp[n - 1] = 0; if (g_disp[0] == 0) { g_disp[0] = '0'; g_disp[1] = 0; } break; }
        case B_CE: g_disp[0] = '0'; g_disp[1] = 0; g_entering = 0; g_err = 0; break;
        case B_C:  g_disp[0] = '0'; g_disp[1] = 0; g_acc = 0; g_op = 0; g_entering = 0; g_err = 0; break;
        case B_MC: g_mem = 0; break;
        case B_MR: fmt(g_mem); g_entering = 0; break;
        case B_MS: g_mem = parse_disp(); break;
        case B_MPLUS: g_mem += parse_disp(); break;
        default: break;
    }
}
static void engine_reset(void) { g_disp[0]='0'; g_disp[1]=0; g_acc=0; g_op=0; g_entering=0; g_mem=0; g_err=0; g_inv=0; }

/* ---- Scientific engine (double precision; results written back to the display) ---- */
static double disp_d(void) { return (double)parse_disp() / (double)SCALE; }
static void fmt_d(double v) {
    if (!(v == v) || v > 1e18 || v < -1e18) { g_err = 1; return; }   /* NaN/inf */
    snprintf(g_disp, sizeof g_disp, "%.10g", v);
    g_entering = 0;
}
enum { S_SIN=100, S_COS, S_TAN, S_LN, S_LOG, S_SQ, S_CUBE, S_RSQRT, S_FACT,
       S_PI, S_EXP, S_TENX, S_INV, S_DEG, S_RAD, S_PCTD };
static void do_sci(int id) {
    if (g_err && id != S_DEG && id != S_RAD) return;
    double x = disp_d(), a2r = g_rad ? 1.0 : (M_PI / 180.0), r2a = g_rad ? 1.0 : (180.0 / M_PI);
    switch (id) {
        case S_DEG: g_rad = 0; return;
        case S_RAD: g_rad = 1; return;
        case S_INV: g_inv = !g_inv; return;                 /* toggle the Inv modifier */
        case S_SIN: fmt_d(g_inv ? asin(x) * r2a : sin(x * a2r)); break;
        case S_COS: fmt_d(g_inv ? acos(x) * r2a : cos(x * a2r)); break;
        case S_TAN: fmt_d(g_inv ? atan(x) * r2a : tan(x * a2r)); break;
        case S_LN:  fmt_d(g_inv ? exp(x) : log(x)); break;
        case S_LOG: fmt_d(g_inv ? pow(10, x) : log10(x)); break;
        case S_SQ:  fmt_d(g_inv ? sqrt(x) : x * x); break;
        case S_CUBE:fmt_d(g_inv ? cbrt(x) : x * x * x); break;
        case S_RSQRT: fmt_d(sqrt(x)); break;
        case S_TENX:  fmt_d(pow(10, x)); break;
        case S_EXP:   fmt_d(exp(x)); break;
        case S_PI:  fmt_d(M_PI); g_entering = 1; break;
        case S_FACT: { if (x < 0 || x > 170 || x != (long)x) { g_err = 1; break; }
                       double f = 1; for (int i = 2; i <= (int)x; i++) f *= i; fmt_d(f); break; }
        default: break;
    }
    g_inv = 0;
}

/* ============================ View (Win32) ================================ */
#define BW 34
#define BH 26
#define GAP 4
typedef struct { int col, row, span, id; const char *lab; u32 clr; } Btn;
static const Btn BTNS[] = {
    { 1, 0, 2, B_BACK, "Back", C_RED }, { 3, 0, 1, B_CE, "CE", C_RED }, { 4, 0, 1, B_C, "C", C_RED },
    { 0, 1, 1, B_MC, "MC", C_RED }, { 0, 2, 1, B_MR, "MR", C_RED }, { 0, 3, 1, B_MS, "MS", C_RED }, { 0, 4, 1, B_MPLUS, "M+", C_RED },
    { 1, 1, 1, B_7, "7", C_BLUE }, { 2, 1, 1, B_8, "8", C_BLUE }, { 3, 1, 1, B_9, "9", C_BLUE }, { 4, 1, 1, B_DIV, "/", C_RED }, { 5, 1, 1, B_SQRT, "sqrt", C_BLUE },
    { 1, 2, 1, B_4, "4", C_BLUE }, { 2, 2, 1, B_5, "5", C_BLUE }, { 3, 2, 1, B_6, "6", C_BLUE }, { 4, 2, 1, B_MUL, "*", C_RED }, { 5, 2, 1, B_PCT, "%", C_BLUE },
    { 1, 3, 1, B_1, "1", C_BLUE }, { 2, 3, 1, B_2, "2", C_BLUE }, { 3, 3, 1, B_3, "3", C_BLUE }, { 4, 3, 1, B_SUB, "-", C_RED }, { 5, 3, 1, B_INV, "1/x", C_BLUE },
    { 1, 4, 1, B_0, "0", C_BLUE }, { 2, 4, 1, B_SIGN, "+/-", C_BLUE }, { 3, 4, 1, B_DOT, ".", C_BLUE }, { 4, 4, 1, B_ADD, "+", C_RED }, { 5, 4, 1, B_EQ, "=", C_RED },
};
#define NBTN ((int)(sizeof(BTNS)/sizeof(BTNS[0])))
#define C_GRN RGB(0x00, 0x60, 0x00)
/* scientific buttons occupy columns 6-8 (to the right of the standard pad) */
static const Btn BTNS_SCI[] = {
    { 6, 0, 1, S_DEG, "Deg", C_GRN }, { 7, 0, 1, S_RAD, "Rad", C_GRN }, { 8, 0, 1, S_INV, "Inv", C_GRN },
    { 6, 1, 1, S_SIN, "sin", C_BLUE }, { 7, 1, 1, S_COS, "cos", C_BLUE }, { 8, 1, 1, S_TAN, "tan", C_BLUE },
    { 6, 2, 1, S_LN, "ln", C_BLUE }, { 7, 2, 1, S_LOG, "log", C_BLUE }, { 8, 2, 1, S_SQ, "x^2", C_BLUE },
    { 6, 3, 1, S_CUBE, "x^3", C_BLUE }, { 7, 3, 1, S_RSQRT, "sqrt", C_BLUE }, { 8, 3, 1, S_FACT, "n!", C_BLUE },
    { 6, 4, 1, S_PI, "pi", C_BLUE }, { 7, 4, 1, S_TENX, "10^x", C_BLUE }, { 8, 4, 1, S_EXP, "exp", C_BLUE },
};
#define NSCI ((int)(sizeof(BTNS_SCI)/sizeof(BTNS_SCI[0])))
static void btn_rect(const Btn *b, int *x, int *y, int *w, int *h) {   /* client-relative */
    *x = 6 + b->col * (BW + GAP); *y = 40 + b->row * (BH + GAP);
    *w = BW * b->span + GAP * (b->span - 1); *h = BH;
}

enum { IDM_COPY = 1, IDM_PASTE = 2, IDM_ABOUT = 3, IDM_STD = 4, IDM_SCI = 5 };
static const MenuItemW32 M_edit[] = { { "Copy\tCtrl+C", IDM_COPY }, { "Paste\tCtrl+V", IDM_PASTE } };
static const MenuItemW32 M_view[] = { { "Standard", IDM_STD }, { "Scientific", IDM_SCI } };
static const MenuItemW32 M_help[] = { { "About Calculator", IDM_ABOUT } };
static const MenuW32 CALC_MENU[] = { { "Edit", M_edit, 2 }, { "View", M_view, 2 }, { "Help", M_help, 1 } };
static void calc_size(int *w, int *h) {
    int cols = g_sci ? 9 : 6;
    *w = cols * (BW + GAP) + 12 + 2;
    *h = 40 + 5 * (BH + GAP) + 8 + WM_TITLEH + 6 + 16;
}

static void calc_key(char c) {
    if (c >= '0' && c <= '9') { do_button(B_0 + (c - '0')); return; }
    switch (c) {
        case '.': do_button(B_DOT); break;
        case '+': do_button(B_ADD); break;
        case '-': do_button(B_SUB); break;
        case '*': do_button(B_MUL); break;
        case '/': do_button(B_DIV); break;
        case '%': do_button(B_PCT); break;
        case '=': case '\n': case '\r': do_button(B_EQ); break;
        case 8:   do_button(B_BACK); break;
        case 27:  do_button(B_C); break;
        default: break;
    }
}

static LRESULT CALLBACK Calc_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            engine_reset();
            g_calc_hwnd = hwnd;
            SetMenuW32(hwnd, CALC_MENU, 3);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            int cw = ps.rcPaint.right;
            RECT face = { 0, 0, ps.rcPaint.right, ps.rcPaint.bottom };
            HBRUSH bf = CreateSolidBrush(C_FACE); FillRect(hdc, &face, bf); DeleteObject(bf);
            /* display */
            RECT d = { 6, 6, cw - 6, 30 };
            HBRUSH wbr = CreateSolidBrush(C_WHITE); FillRect(hdc, &d, wbr); DeleteObject(wbr);
            DrawEdge3D(hdc, &d, 0);
            const char *shown = g_err ? "Cannot divide by zero" : g_disp;
            int tw = (int)font_measure(shown);
            SetTextColor(hdc, g_err ? C_RED : C_DK);
            TextOut(hdc, cw - 12 - tw, 12, shown, 24);
            /* buttons (standard pad; plus the scientific columns in Scientific view) */
            int total = g_sci ? NBTN + NSCI : NBTN;
            for (int i = 0; i < total; i++) {
                const Btn *bt = i < NBTN ? &BTNS[i] : &BTNS_SCI[i - NBTN];
                int x, y, w, h; btn_rect(bt, &x, &y, &w, &h);
                /* a mode button that is active (Deg/Rad/Inv) shows sunken */
                int active = (bt->id == S_DEG && !g_rad) || (bt->id == S_RAD && g_rad) || (bt->id == S_INV && g_inv);
                RECT r = { x, y, x + w, y + h };
                HBRUSH b = CreateSolidBrush(C_FACE); FillRect(hdc, &r, b); DeleteObject(b);
                DrawEdge3D(hdc, &r, active ? 0 : 1);
                int lw = (int)font_measure(bt->lab), o = active ? 1 : 0;
                SetTextColor(hdc, bt->clr);
                TextOut(hdc, x + (w - lw) / 2 + o, y + (h - (int)font_height()) / 2 + o, bt->lab, 8);
            }
            EndPaint(hwnd, hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int rx = GET_X_LPARAM(lParam), ry = GET_Y_LPARAM(lParam);
            int total = g_sci ? NBTN + NSCI : NBTN;
            for (int i = 0; i < total; i++) {
                const Btn *bt = i < NBTN ? &BTNS[i] : &BTNS_SCI[i - NBTN];
                int x, y, w, h; btn_rect(bt, &x, &y, &w, &h);
                if (rx >= x && rx < x + w && ry >= y && ry < y + h) {
                    if (bt->id >= S_SIN) do_sci(bt->id); else do_button(bt->id);
                    InvalidateRect(hwnd); return 0;
                }
            }
            return 0;
        }
        case WM_CHAR: calc_key((char)wParam); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_COPY: wm_clipboard_set(g_disp); break;
                case IDM_PASTE: {
                    const char *c = wm_clipboard_get();
                    if (c && c[0]) { engine_reset(); g_entering = 1; g_disp[0] = 0;
                        for (int i = 0; c[i] && i < 16; i++) if ((c[i] >= '0' && c[i] <= '9') || c[i] == '.' || c[i] == '-') { int n = slen(g_disp); g_disp[n] = c[i]; g_disp[n+1] = 0; }
                        if (!g_disp[0]) { g_disp[0] = '0'; g_disp[1] = 0; } }
                    break;
                }
                case IDM_STD: case IDM_SCI: {
                    g_sci = (LOWORD(wParam) == IDM_SCI);
                    int w, h; calc_size(&w, &h); SetWindowSize(hwnd, w, h); InvalidateRect(hwnd); break;
                }
                case IDM_ABOUT: MessageBoxW32(hwnd, g_sci ? "Debian95 Calculator\nScientific view" : "Debian95 Calculator\nStandard view", "About Calculator", MB_ICONINFO); break;
            }
            return 0;
        case WM_DESTROY: engine_reset(); return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void calc_open(void) {
    { const char *e = getenv("DEBIAN95_CALCSCI"); if (e && e[0] == '1') g_sci = 1; }   /* test: open in Scientific */
    int w, h; calc_size(&w, &h);
    CreateAppWindow(220, 130, w, h, "Calculator", Calc_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
/* Built as a hot-swappable app module (calc.so). See debian95/appsdk.h. */
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 10, "Calculator", IC_APP, 0, calc_open, -1 };
#endif
