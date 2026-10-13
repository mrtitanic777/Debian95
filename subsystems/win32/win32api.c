/* Debian95 — Win32 veneer implementation (see sdk/include/debian95/win32.h).
 * Translates the window manager's per-window callbacks into standard Win32
 * messages dispatched to a single WndProc, and backs a small GDI HDC with the
 * framebuffer primitives. Additive: existing wm_add_custom apps are untouched. */
#include <debian95/win32.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>

/* ---- per-window device context ---- */
typedef struct { int ox, oy, cw, ch; u32 fg; int bk; u32 brush, pen; } LGDI;

/* ---- window slots (each gets its own trampolines so callbacks route by slot) ---- */
#define NWSLOT 16
typedef struct {
    int      used;
    Window  *win;
    WNDPROC  proc;
    void    *ud;
    LGDI     dc;
} W32Win;
static W32Win g_w[NWSLOT];
static int    g_inhook;                 /* 1 while inside the WM's close hook */

static W32Win *slot_of(HWND h) { return (W32Win *)h; }
static int     idx_of(W32Win *w) { return (int)(w - g_w); }

/* ---- message plumbing (called by the per-slot trampolines below) ---- */
static void on_paint(int i, int cx, int cy, int cw, int ch) {
    if (!g_w[i].used) return;
    g_w[i].dc.ox = cx; g_w[i].dc.oy = cy; g_w[i].dc.cw = cw; g_w[i].dc.ch = ch;
    g_w[i].proc((HWND)&g_w[i], WM_PAINT, 0, 0);
}
static void on_key(int i, char c) {
    if (g_w[i].used) g_w[i].proc((HWND)&g_w[i], WM_CHAR, (WPARAM)(unsigned char)c, 0);
}
static void on_click(int i, int x, int y) {
    if (g_w[i].used) g_w[i].proc((HWND)&g_w[i], WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
}
static void on_rclick(int i, int x, int y) {
    if (g_w[i].used) g_w[i].proc((HWND)&g_w[i], WM_RBUTTONDOWN, 0, MAKELPARAM(x, y));
}
static void on_menu(int i, int id) {
    if (g_w[i].used) g_w[i].proc((HWND)&g_w[i], WM_COMMAND, MAKEWPARAM(id, 0), 0);
}
static void on_hookclose(int i) {       /* the user clicked the window's [X] */
    if (!g_w[i].used) return;
    g_inhook = 1;
    g_w[i].proc((HWND)&g_w[i], WM_CLOSE, 0, 0);   /* app calls DestroyWindow (or DefWindowProc does) */
    g_inhook = 0;
}
static void on_motion(int i, int x, int y, u8 buttons) {
    if (g_w[i].used) g_w[i].proc((HWND)&g_w[i], WM_MOUSEMOVE, (WPARAM)buttons, MAKELPARAM(x, y));
}

/* generate NWSLOT sets of trampolines with distinct addresses */
#define TRAMPS(i) \
    static void paint_##i(int a,int b,int c,int d){ on_paint(i,a,b,c,d);} \
    static void key_##i(char c){ on_key(i,c);} \
    static void click_##i(int x,int y){ on_click(i,x,y);} \
    static void rclick_##i(int x,int y){ on_rclick(i,x,y);} \
    static void menu_##i(int id){ on_menu(i,id);} \
    static void motion_##i(int x,int y,u8 bt){ on_motion(i,x,y,bt);} \
    static void close_##i(void){ on_hookclose(i);}
TRAMPS(0) TRAMPS(1) TRAMPS(2) TRAMPS(3) TRAMPS(4) TRAMPS(5)
TRAMPS(6) TRAMPS(7) TRAMPS(8) TRAMPS(9) TRAMPS(10) TRAMPS(11)
TRAMPS(12) TRAMPS(13) TRAMPS(14) TRAMPS(15)
#define TROW(i) { paint_##i, key_##i, click_##i, rclick_##i, menu_##i, motion_##i, close_##i }
static const struct {
    void (*paint)(int,int,int,int); void (*key)(char); void (*click)(int,int);
    void (*rclick)(int,int); void (*menu)(int); void (*motion)(int,int,u8); void (*close)(void);
} TR[NWSLOT] = {
    TROW(0),TROW(1),TROW(2),TROW(3),TROW(4),TROW(5),
    TROW(6),TROW(7),TROW(8),TROW(9),TROW(10),TROW(11),
    TROW(12),TROW(13),TROW(14),TROW(15)
};

/* ---- window API ---- */
HWND CreateAppWindow(int x, int y, int w, int h, const char *title,
                     WNDPROC proc, void *createParam) {
    int i = 0; for (; i < NWSLOT && g_w[i].used; i++) {}
    if (i == NWSLOT) return 0;
    g_w[i].used = 1; g_w[i].proc = proc; g_w[i].ud = 0;
    g_w[i].dc.fg = RGB(0,0,0); g_w[i].dc.bk = TRANSPARENT;
    g_w[i].dc.brush = RGB(0xC0,0xC0,0xC0); g_w[i].dc.pen = RGB(0,0,0);
    g_w[i].win = wm_add_custom(x, y, w, h, title, TR[i].paint, TR[i].key, TR[i].click);
    wm_set_close(g_w[i].win, TR[i].close);
    wm_set_motion(g_w[i].win, TR[i].motion);          /* deliver WM_MOUSEMOVE (drag support) */
    wm_set_rclick(g_w[i].win, TR[i].rclick);          /* deliver WM_RBUTTONDOWN */
    g_w[i].proc((HWND)&g_w[i], WM_CREATE, 0, (LPARAM)createParam);
    wm_show(g_w[i].win);
    return (HWND)&g_w[i];
}
void DestroyWindow(HWND hwnd) {
    W32Win *wn = slot_of(hwnd); if (!wn || !wn->used) return;
    wn->proc(hwnd, WM_DESTROY, 0, 0);       /* app frees its state */
    Window *win = wn->win;
    wn->used = 0; wn->proc = 0; wn->win = 0; wn->ud = 0;
    if (!g_inhook) wm_destroy(win);          /* app-initiated: remove from the WM too */
}
LRESULT DefWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    if (msg == WM_CLOSE) DestroyWindow(hwnd);   /* default: closing destroys the window */
    return 0;
}
void *GetWindowUserData(HWND hwnd) { W32Win *w = slot_of(hwnd); return w ? w->ud : 0; }
void  SetWindowUserData(HWND hwnd, void *p) { W32Win *w = slot_of(hwnd); if (w) w->ud = p; }
void  InvalidateRect(HWND hwnd) { (void)hwnd; /* the WM recomposes every frame */ }
void  GetClientOrigin(HWND hwnd, int *x, int *y, int *w, int *h) {
    W32Win *wn = slot_of(hwnd); int cx = 0, cy = 0, cw = 0, ch = 0;
    if (wn && wn->win) wm_client_rect(wn->win, &cx, &cy, &cw, &ch);
    if (x) *x = cx; if (y) *y = cy; if (w) *w = cw; if (h) *h = ch;
}
void  SetWindowSize(HWND hwnd, int w, int h) { W32Win *s = slot_of(hwnd); if (s && s->win) wm_set_size(s->win, w, h); }
void  SetWindowSizeGrip(HWND hwnd) { W32Win *s = slot_of(hwnd); if (s && s->win) wm_set_sizegrip(s->win); }
void  SetWindowText(HWND hwnd, const char *title) { W32Win *s = slot_of(hwnd); if (s && s->win) wm_set_title(s->win, title); }
int   IsWindowVisible(HWND hwnd) { W32Win *s = slot_of(hwnd); return (s && s->used && s->win && wm_is_visible(s->win)); }
void  SetWindowTextCursor(HWND hwnd) { W32Win *s = slot_of(hwnd); if (s && s->win) wm_set_text_cursor(s->win); }
void *GetWindowObject(HWND hwnd) { W32Win *s = slot_of(hwnd); return (s && s->win) ? (void *)s->win : 0; }  /* underlying WM window */
void  SetWindowIcon(HWND hwnd, u16 ic) { W32Win *w = slot_of(hwnd); if (w && w->win) wm_set_icon(w->win, ic); }
void  SetWindowIconMod(HWND hwnd, const char *mod, u16 id) { W32Win *w = slot_of(hwnd); if (w && w->win) wm_set_icon_mod(w->win, mod, id); }
void  SetWindowHelp(HWND hwnd, const char *help) { W32Win *w = slot_of(hwnd); if (w && w->win) wm_set_help(w->win, help); }
void  win32_timer_tick(void) {
    for (int i = 0; i < NWSLOT; i++)
        if (g_w[i].used) g_w[i].proc((HWND)&g_w[i], WM_TIMER, 0, 0);
}
void  SetMenuW32(HWND hwnd, const MenuW32 *menus, int n) {
    W32Win *w = slot_of(hwnd); if (!w || !w->used) return;
    /* MenuW32/MenuItemW32 are layout-identical to WmMenu/WmMenuItem */
    wm_set_menu(w->win, (const WmMenu *)menus, n, TR[idx_of(w)].menu);
}
void  SetMenuFlagsW32(HWND hwnd, int (*cb)(int)) {
    W32Win *w = slot_of(hwnd); if (!w || !w->used) return;
    wm_set_menuflags(w->win, cb);
}

/* ---- GDI ---- */
static LGDI *dc_of(HDC h) { return (LGDI *)h; }
HDC BeginPaint(HWND hwnd, PAINTSTRUCT *ps) {
    W32Win *w = slot_of(hwnd);
    if (ps) { ps->hdc = (HDC)&w->dc; ps->rcPaint.left = 0; ps->rcPaint.top = 0;
              ps->rcPaint.right = w->dc.cw; ps->rcPaint.bottom = w->dc.ch; }
    fb_set_clip(w->dc.ox, w->dc.oy, w->dc.cw, w->dc.ch);   /* keep drawing inside the client */
    return (HDC)&w->dc;
}
void EndPaint(HWND hwnd, HDC hdc) { (void)hwnd; (void)hdc; fb_clear_clip(); }

#define GDI_MAX 64
static struct { int used; u32 color; } g_obj[GDI_MAX];
HBRUSH CreateSolidBrush(u32 rgb) {
    for (int i = 0; i < GDI_MAX; i++) if (!g_obj[i].used) { g_obj[i].used = 1; g_obj[i].color = rgb; return (HBRUSH)&g_obj[i]; }
    return 0;
}
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj) {
    LGDI *d = dc_of(hdc); (void)d;
    /* brushes are the only selectable object we model; return no meaningful prev */
    if (obj) d->brush = ((typeof(g_obj[0]) *)obj)->color;
    return 0;
}
void DeleteObject(HGDIOBJ obj) { if (obj) ((typeof(g_obj[0]) *)obj)->used = 0; }
static u32 obj_color(HBRUSH b, u32 dflt) { return b ? ((typeof(g_obj[0]) *)b)->color : dflt; }

void FillRect(HDC hdc, const RECT *rc, HBRUSH br) {
    LGDI *d = dc_of(hdc); u32 c = obj_color(br, d->brush);
    if (rc->right > rc->left && rc->bottom > rc->top)
        fb_fill_rect(d->ox + rc->left, d->oy + rc->top, rc->right - rc->left, rc->bottom - rc->top, c);
}
void FrameRect(HDC hdc, const RECT *rc, HBRUSH br) {
    LGDI *d = dc_of(hdc); u32 c = obj_color(br, d->pen);
    fb_rect(d->ox + rc->left, d->oy + rc->top, rc->right - rc->left, rc->bottom - rc->top, c);
}
void Rectangle(HDC hdc, int l, int t, int r, int b) {
    LGDI *d = dc_of(hdc);
    if (r > l && b > t) { fb_fill_rect(d->ox + l, d->oy + t, r - l, b - t, d->brush);
                          fb_rect(d->ox + l, d->oy + t, r - l, b - t, d->pen); }
}
void TextOut(HDC hdc, int x, int y, const char *s, int n) {
    LGDI *d = dc_of(hdc); char buf[256]; int k = 0;
    for (; s[k] && k < n && k < 255; k++) buf[k] = s[k]; buf[k] = 0;
    fb_text_t(d->ox + x, d->oy + y, buf, d->fg);
}
u32 SetTextColor(HDC hdc, u32 rgb) { LGDI *d = dc_of(hdc); u32 p = d->fg; d->fg = rgb; return p; }
int SetBkMode(HDC hdc, int mode) { LGDI *d = dc_of(hdc); int p = d->bk; d->bk = mode; return p; }
void DrawEdge3D(HDC hdc, const RECT *rc, int raised) {
    LGDI *d = dc_of(hdc); int x = d->ox + rc->left, y = d->oy + rc->top;
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    if (raised) wm_bevel_raised(x, y, w, h); else wm_bevel_sunken(x, y, w, h);
}

/* ---- COMDLG32-lite ---- */
int MessageBoxW32(HWND hwnd, const char *text, const char *caption, UINT type) {
    (void)hwnd;
    int sev = (type & MB_ICONERROR) ? 2 : (type & MB_ICONWARNING) ? 1 : 0;
    wm_messagebox_ex(caption ? caption : "Debian95", text ? text : "", sev);
    return 1;   /* IDOK */
}
