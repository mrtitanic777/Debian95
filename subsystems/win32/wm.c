/* Debian95 — window manager. Managed windows with focus, z-order, dragging and
 * close boxes, composited to a double-buffered framebuffer. Original artwork. */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/cursor.h>
#include <debian95/img.h>
#include <debian95/icons.h>
#include <debian95/nt.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include <debian95/ke.h>
#include <debian95/theme.h>
#include <debian95/version.h>

int sys_read_file(const char *path, char *out, int n);   /* /proc reads (shims) */

/* Colours come from the active theme (theme.c) so the whole shell restyles when
 * the theme changes. Win95 is the default and matches the historical constants. */
#define C_FACE     (g_theme->face)
#define C_SEL      (g_theme->sel)       /* highlight bar (classic menu selection) */
#define C_HILIGHT  (g_theme->white)
#define C_SHADOW   (g_theme->shadow)
#define C_DK       (g_theme->black)
/* these three seed the runtime g_desktop / g_title_l / g_title_r (static
 * initializers need a constant); the theme pushes live values via
 * wm_set_desktop_color / wm_set_title_colors in theme_set(). */
#define C_DESKTOP  RGB(0x00, 0x80, 0x80)
#define C_TITLE_L  RGB(0x00, 0x00, 0x80)
#define C_TITLE_R  RGB(0x00, 0x00, 0x80)
#define C_INACT    (g_theme->title_ilo)
#define C_WHITE    (g_theme->white)
#define C_EMBLEM   RGB(0x20, 0x40, 0xB0)

/* runtime theme (adjustable from the Control Panel's Appearance/Display applets) */
static u32 g_desktop = C_DESKTOP, g_title_l = C_TITLE_L, g_title_r = C_TITLE_R;
static int g_wall_on = 1;
void wm_set_desktop_color(u32 c)          { g_desktop = c; }
void wm_set_title_colors(u32 l, u32 r)    { g_title_l = l; g_title_r = r; }
static int g_swap_btn;                     /* left/right button swap (Mouse applet) */
void wm_set_swap_buttons(int on)          { g_swap_btn = on ? 1 : 0; }
static int g_mod_ctrl, g_mod_shift;        /* current Ctrl/Shift, set by the platform for click handlers */
void wm_set_mods(int ctrl, int shift)     { g_mod_ctrl = ctrl ? 1 : 0; g_mod_shift = shift ? 1 : 0; }
int  wm_mod_ctrl(void)                     { return g_mod_ctrl; }
int  wm_mod_shift(void)                    { return g_mod_shift; }
static int g_time24;                        /* tray clock 24-hour mode (Regional applet) */
void wm_set_time_24h(int on)              { g_time24 = on ? 1 : 0; }
static int g_show_clock = 1;                 /* Taskbar Properties: show the tray clock */
void wm_set_show_clock(int on)            { g_show_clock = on ? 1 : 0; }
static int g_small_start;                     /* Taskbar Properties: small Start-menu icons (persisted) */
static int g_tb_address;                      /* Address deskband on the taskbar (Toolbars menu, persisted) */
void wm_set_toolbar_address(int on)       { g_tb_address = on ? 1 : 0; }
static int  g_addr_active;                    /* the Address field has keyboard focus */
static char g_addr_buf[160]; static int g_addr_len;
static int  g_addr_bx, g_addr_by, g_addr_bw;  /* Address edit-box rect (hit-test) */

/* Links deskband: user-populated shortcuts (never fabricated). Stored in the
 * settings as "links" = name\turl entries joined by '\n'. */
static int g_tb_links;
void wm_set_toolbar_links(int on)         { g_tb_links = on ? 1 : 0; }
#define MAXLINK 12
static char g_lk_name[MAXLINK][32]; static char g_lk_url[MAXLINK][128]; static int g_nlink;
static int  g_lk_loaded;
static void links_load(void) {
    if (g_lk_loaded) return; g_lk_loaded = 1; g_nlink = 0;
    char buf[1536]; if (sys_get_setting("links", buf, sizeof buf) != 0 || !buf[0]) return;
    char *p = buf;
    while (*p && g_nlink < MAXLINK) {
        char *tab = p; while (*tab && *tab != '\t' && *tab != '\n') tab++;
        if (*tab != '\t') { while (*p && *p != '\n') p++; if (*p) p++; continue; }
        *tab = 0; char *url = tab + 1; char *end = url; while (*end && *end != '\n') end++;
        int hadnl = (*end == '\n'); *end = 0;
        snprintf(g_lk_name[g_nlink], 32, "%s", p); snprintf(g_lk_url[g_nlink], 128, "%s", url); g_nlink++;
        p = hadnl ? end + 1 : end;
    }
}
static void links_save(void) {
    char buf[1536]; int k = 0;
    for (int i = 0; i < g_nlink && k < (int)sizeof buf - 160; i++)
        k += snprintf(buf + k, sizeof buf - k, "%s\t%s\n", g_lk_name[i], g_lk_url[i]);
    if (k && buf[k-1] == '\n') buf[k-1] = 0;      /* trim trailing newline */
    sys_set_setting("links", buf);
}
int  wm_links_count(void)            { links_load(); return g_nlink; }
const char *wm_link_name(int i)      { links_load(); return (i >= 0 && i < g_nlink) ? g_lk_name[i] : ""; }
const char *wm_link_url(int i)       { links_load(); return (i >= 0 && i < g_nlink) ? g_lk_url[i] : ""; }
void wm_links_add(const char *n, const char *u) {
    links_load();
    if (g_nlink >= MAXLINK || !n || !u || !n[0] || !u[0]) return;
    snprintf(g_lk_name[g_nlink], 32, "%s", n); snprintf(g_lk_url[g_nlink], 128, "%s", u); g_nlink++;
    links_save();
}
void wm_links_remove(int i) {
    links_load();
    if (i < 0 || i >= g_nlink) return;
    for (int j = i; j < g_nlink - 1; j++) { memcpy(g_lk_name[j], g_lk_name[j+1], 32); memcpy(g_lk_url[j], g_lk_url[j+1], 128); }
    g_nlink--; links_save();
}
void wm_set_wallpaper_enabled(int on)     { g_wall_on = on; }
static int g_high_contrast, g_hc_wall_saved = 1;   /* Accessibility: High Contrast Black */
void wm_set_high_contrast(int on) {
    if (on && !g_high_contrast) { g_hc_wall_saved = g_wall_on;
        g_desktop = C_DK; g_title_l = g_title_r = C_DK; g_wall_on = 0; g_high_contrast = 1; }
    else if (!on && g_high_contrast) {
        g_desktop = C_DESKTOP; g_title_l = C_TITLE_L; g_title_r = C_TITLE_R; g_wall_on = g_hc_wall_saved; g_high_contrast = 0; }
}
#define TB_H       30

static void hline(int x, int y, int w, u32 c) { fb_fill_rect(x, y, w, 1, c); }
static void vline(int x, int y, int h, u32 c) { fb_fill_rect(x, y, 1, h, c); }

void wm_bevel_raised(int x, int y, int w, int h) {
    if (g_theme->style == STYLE_FLAT) { fb_rect(x, y, w, h, C_SHADOW); return; }  /* flat 1px edge */
    u32 dark = (g_theme->style == STYLE_XP) ? C_SHADOW : C_DK;   /* XP: softer dark edge */
    hline(x, y, w, C_HILIGHT);          vline(x, y, h, C_HILIGHT);
    hline(x, y + h - 1, w, dark);        vline(x + w - 1, y, h, dark);
    hline(x + 1, y + h - 2, w - 2, C_SHADOW);
    vline(x + w - 2, y + 1, h - 2, C_SHADOW);
}
void wm_bevel_sunken(int x, int y, int w, int h) {
    if (g_theme->style == STYLE_FLAT) { fb_rect(x, y, w, h, C_SHADOW); return; }  /* flat 1px edge */
    u32 dark = (g_theme->style == STYLE_XP) ? C_SHADOW : C_DK;
    hline(x, y, w, C_SHADOW);            vline(x, y, h, C_SHADOW);
    hline(x + 1, y + 1, w - 2, dark);    vline(x + 1, y + 1, h - 2, dark);
    hline(x, y + h - 1, w, C_HILIGHT);   vline(x + w - 1, y, h, C_HILIGHT);
}

static void emblem(int x, int y, int sz) {
    fb_fill_rect(x, y, sz, sz, C_EMBLEM);
    wm_bevel_raised(x, y, sz, sz);
    fb_char_t(x + (sz - 8) / 2, y + (sz - 8) / 2, 'L', C_WHITE);
}

/* forward decls: the caption/taskbar draw the window's real 16px icon from any
 * module (shell32 by default, or e.g. MSPAINT.EXE / PIFMGR.DLL) */
static const u32 *sm_icon(u16 id);
static const u32 *sm_icon_m(const char *mod, u16 id);
static void blit_key(int x, int y, int w, int h, const u32 *px);
static void win_icon(int x, int y, const char *mod, u16 id) {   /* real app icon, 'L' fallback */
    const u32 *ic = sm_icon_m(mod, id ? id : IC_APP);
    if (ic) blit_key(x, y, 16, 16, ic);
    else    emblem(x + 1, y + 1, 14);
}

static void title_gradient(int x, int y, int w, int h) {
    int lr = (g_title_l >> 16) & 0xFF, lg = (g_title_l >> 8) & 0xFF, lb = g_title_l & 0xFF;
    int rr = (g_title_r >> 16) & 0xFF, rg = (g_title_r >> 8) & 0xFF, rb = g_title_r & 0xFF;
    for (int i = 0; i < w; i++) {
        int t = (w > 1) ? (i * 255) / (w - 1) : 0;
        fb_fill_rect(x + i, y, 1, h, RGB(lr + ((rr - lr) * t) / 255,
                                         lg + ((rg - lg) * t) / 255,
                                         lb + ((rb - lb) * t) / 255));
    }
}
/* blend a->b by t/255 */
static u32 mix(u32 a, u32 b, int t) {
    int r = (((a>>16)&0xff)*(255-t) + ((b>>16)&0xff)*t)/255;
    int g = (((a>>8)&0xff)*(255-t)  + ((b>>8)&0xff)*t)/255;
    int bl= ((a&0xff)*(255-t)       + (b&0xff)*t)/255;
    return RGB(r,g,bl);
}
static void title_button(int x, int y, char kind) {
    int is_close = (kind != '_' && kind != 'o' && kind != 'r');

    if (g_theme->style == STYLE_XP) {                          /* Luna glossy caption button */
        u32 base = is_close ? g_theme->close_btn : g_theme->minmax_btn;
        u32 top = mix(base, RGB(0xFF,0xFF,0xFF), 90);          /* glossy light top */
        for (int j = 0; j < 14; j++) fb_fill_rect(x, y + j, 16, 1, mix(top, base, (j*255)/13));
        /* round the corners + a soft rim */
        fb_pixel(x, y, C_TITLE_R); fb_pixel(x+15, y, C_TITLE_R);
        fb_pixel(x, y+13, C_TITLE_R); fb_pixel(x+15, y+13, C_TITLE_R);
        fb_fill_rect(x, y, 16, 1, mix(top, RGB(0xFF,0xFF,0xFF), 120));  /* highlight edge */
        u32 gl = RGB(0xFF,0xFF,0xFF);
        if (kind == '_')      fb_fill_rect(x + 4, y + 9, 8, 2, gl);
        else if (kind == 'o') fb_rect(x + 4, y + 3, 8, 7, gl);
        else if (kind == 'r') { fb_rect(x + 5, y + 3, 6, 5, gl); fb_rect(x + 3, y + 6, 6, 5, gl); }
        else if (kind == '?') fb_text_bold_t(x + 5, y + 2, "?", gl);
        else for (int i = 0; i < 6; i++) { fb_fill_rect(x+4+i, y+4+i, 2, 1, gl); fb_fill_rect(x+9-i, y+4+i, 2, 1, gl); }
        return;
    }
    if (g_theme->style == STYLE_FLAT) {                        /* flat glyph, no button chrome */
        u32 gl = is_close ? g_theme->close_btn : g_theme->title_text;
        if (kind == '_')      fb_fill_rect(x + 4, y + 9, 8, 1, gl);
        else if (kind == 'o') fb_rect(x + 4, y + 3, 8, 8, gl);
        else if (kind == 'r') { fb_rect(x + 5, y + 3, 6, 6, gl); fb_rect(x + 3, y + 5, 6, 6, gl); }
        else if (kind == '?') fb_text_bold_t(x + 5, y + 2, "?", gl);
        else for (int i = 0; i < 8; i++) { fb_pixel(x+4+i, y+3+i, gl); fb_pixel(x+11-i, y+3+i, gl); }
        return;
    }
    /* classic 95/98 */
    fb_fill_rect(x, y, 16, 14, C_FACE);
    wm_bevel_raised(x, y, 16, 14);
    if (kind == '_')      fb_fill_rect(x + 4, y + 9, 6, 2, C_DK);
    else if (kind == 'o') { fb_rect(x + 3, y + 3, 10, 8, C_DK); fb_fill_rect(x + 3, y + 3, 10, 2, C_DK); }
    else if (kind == 'r') {                                      /* restore: two overlapped windows */
        fb_rect(x + 5, y + 3, 7, 5, C_DK); fb_fill_rect(x + 5, y + 3, 7, 2, C_DK);
        fb_fill_rect(x + 3, y + 6, 7, 5, C_FACE);
        fb_rect(x + 3, y + 6, 7, 5, C_DK); fb_fill_rect(x + 3, y + 6, 7, 2, C_DK);
    }
    else if (kind == '?') fb_text_bold_t(x + 5, y + 2, "?", C_DK);
    else for (int i = 0; i < 7; i++) { fb_pixel(x + 4 + i, y + 3 + i, C_DK); fb_pixel(x + 10 - i, y + 3 + i, C_DK); }
}

/* ---- window list ---- */
struct Window {
    int x, y, w, h;
    const char *title;
    const char **lines;
    int nlines;
    void (*paint)(int, int, int, int);
    void (*onkey)(char c);
    void (*onclick)(int rx, int ry);   /* click in the client area (relative coords) */
    void (*onrclick)(int rx, int ry);  /* right-click in the client area (games/menus) */
    void (*onmotion)(int rx, int ry, u8 buttons);   /* mouse move over the client */
    const WmMenu *menus; int nmenus;    /* optional drop-down menu bar */
    void (*onmenu)(int id);
    int (*menuflags)(int id);           /* dynamic check/disable: bit0=checked, bit1=disabled */
    int visible;
    int minimized;                      /* hidden from desktop but kept on the taskbar */
    int alive;                          /* slot in use (0 = destroyed/free, reusable) */
    void (*onclose)(void);              /* app cleanup: free resources + drop its window ref */
    u8  text_cursor;                    /* show the I-beam over the client area */
    u16 icon;                           /* RT_ICON id for the caption/taskbar (0 = generic) */
    const char *icon_mod;               /* icon module (0 = SHELL32.DLL) */
    const char *help;                   /* "What's This?": non-0 shows a ? caption button */
    int maxed;                          /* 1 = maximised; rx/ry/rw/rh hold the restore rect */
    int rx, ry, rw, rh;
    u8  sizegrip;                       /* draw the diagonal corner size grip (status-bar windows) */
    int external;                       /* a foreign X11 app (VLC/Chromium) framed by debian95-wm:
                                         * lives ONLY on the taskbar - never drawn/hit on our
                                         * framebuffer; clicks route to X via wm_take_ext_action */
    unsigned long xframe, xclient;      /* its debian95-wm frame + client X Window ids */
};
#define WM_MENUH 16
static struct Window g_store[16];
static int g_nstore;
static struct Window *g_z[16];   /* z-order: [0]=back .. [n-1]=front */
static int g_nz;

static struct Window *front_visible(void);
static void bring_front(struct Window *w);
static int hit_win(struct Window *w, int x, int y);
static int hit_grip(struct Window *w, int x, int y);
static int hit_title(struct Window *w, int x, int y);
static void wm_toggle_max(struct Window *w);

static struct Window *new_win(int x, int y, int w, int h, const char *title) {
    struct Window *win = 0;
    for (int i = 0; i < 16; i++) if (!g_store[i].alive) { win = &g_store[i]; break; }  /* reuse a destroyed slot */
    if (!win) return 0;
    if (g_nstore < 16) g_nstore++;
    /* keep the window on-screen without maximizing it: only shrink if it is
     * genuinely larger than the screen, and centre it when its default position
     * would push it off-screen (Win95 boots at 640x480, windows stay windowed). */
    { int sw = (int)fb_width(), sh = (int)fb_height() - TB_H;
      if (w > sw) w = sw; if (h > sh) h = sh;
      if (x + w > sw) x = (sw - w) / 2; if (x < 0) x = 0;
      if (y + h > sh) y = (sh - h) / 2; if (y < 0) y = 0; }
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->title = title; win->lines = 0; win->nlines = 0;
    win->paint = 0; win->onkey = 0; win->onclick = 0; win->onrclick = 0; win->onmotion = 0;
    win->menus = 0; win->nmenus = 0; win->onmenu = 0; win->menuflags = 0; win->onclose = 0;
    win->text_cursor = 0; win->icon = 0; win->icon_mod = 0; win->maxed = 0; win->sizegrip = 0;  /* clear reused-slot state */
    win->external = 0; win->xframe = 0; win->xclient = 0;
    win->visible = 1; win->minimized = 0; win->alive = 1;
    g_z[g_nz++] = win;
    return win;
}
void wm_set_close(Window *w, void (*fn)(void)) { if (w) w->onclose = fn; }
void wm_destroy(Window *w);   /* defined after the menu/context statics */
Window *wm_add_text(int x, int y, int w, int h, const char *title, const char **lines, int n) {
    struct Window *win = new_win(x, y, w, h, title);
    if (win) { win->lines = lines; win->nlines = n; }
    return win;
}
Window *wm_add_custom(int x, int y, int w, int h, const char *title,
                      void (*paint)(int, int, int, int), void (*onkey)(char),
                      void (*onclick)(int, int)) {
    struct Window *win = new_win(x, y, w, h, title);
    if (win) { win->paint = paint; win->onkey = onkey; win->onclick = onclick; }
    return win;
}

void wm_show(Window *w) { if (w) { w->visible = 1; w->minimized = 0; bring_front(w); } }
void wm_set_title(Window *w, const char *t) { if (w) w->title = t; }
void wm_set_icon(Window *w, u16 id) { if (w) { w->icon = (u16)id; w->icon_mod = 0; } }
void wm_set_help(Window *w, const char *h) { if (w) w->help = h; }
void wm_set_icon_mod(Window *w, const char *module, u16 id) { if (w) { w->icon = (u16)id; w->icon_mod = module; } }
void wm_hide(Window *w) { if (w) w->visible = 0; }
void wm_set_motion(Window *w, void (*fn)(int, int, u8)) { if (w) w->onmotion = fn; }
void wm_set_rclick(Window *w, void (*fn)(int, int)) { if (w) w->onrclick = fn; }
int  wm_is_visible(Window *w) { return w ? w->visible : 0; }

/* ---- task enumeration (for Task Manager): the same set the taskbar shows ---- */
int wm_enum_tasks(WmTaskInfo *out, int max) {
    int n = 0;
    for (int i = 0; i < 16 && n < max; i++) {
        struct Window *w = &g_store[i];
        if (!w->alive) continue;
        if (!w->visible && !w->minimized) continue;   /* closed windows aren't tasks */
        out[n].title = w->title ? w->title : "(untitled)";
        out[n].win = w;
        out[n].minimized = w->minimized;
        n++;
    }
    return n;
}
static void ext_push(struct Window *w, int a);   /* foreign-app taskbar action, defined below */
void wm_activate(void *win) {          /* bring a task to the front (Switch To) */
    struct Window *w = (struct Window *)win; if (!w || !w->alive) return;
    if (w->external) { w->visible = 1; w->minimized = 0; ext_push(w, 1); return; }  /* raise the X client */
    w->visible = 1; w->minimized = 0; bring_front(w);
}
void wm_close_task(void *win) {        /* End Task = the same teardown as clicking [X] */
    struct Window *w = (struct Window *)win; if (!w || !w->alive) return;
    if (w->external) { ext_push(w, 3); return; }   /* ask debian95-wm to close the X client */
    wm_destroy(w);
}

/* ---- foreign X11 apps on the taskbar (Desktop Edition) ------------------------
 * debian95-wm frames VLC/Chromium etc.; the platform layer mirrors each into an
 * "external" window here so it gets a real taskbar button + Task-Manager entry.
 * These windows are never drawn or hit-tested on our framebuffer (X paints them);
 * a click just records an action the platform layer drains and turns into X. */
static char g_ext_title[16][128];
static unsigned long g_ext_client; static int g_ext_action;   /* 1 activate, 2 minimize, 3 close */
static unsigned long g_active_ext;   /* the X-focused external client (from debian95-wm) */
void wm_ext_set_active(unsigned long xclient) { g_active_ext = xclient; }
static void ext_push(struct Window *w, int a) { if (w && w->external) { g_ext_client = w->xclient; g_ext_action = a; } }

void *wm_ext_find(unsigned long xclient) {
    for (int i = 0; i < 16; i++)
        if (g_store[i].alive && g_store[i].external && g_store[i].xclient == xclient) return &g_store[i];
    return 0;
}
void *wm_ext_add(unsigned long xframe, unsigned long xclient, const char *title) {
    struct Window *w = new_win(-1000, -1000, 10, 10, 0);   /* off-screen: never drawn/hit here */
    if (!w) return 0;
    int slot = (int)(w - g_store);
    w->external = 1; w->xframe = xframe; w->xclient = xclient;
    g_ext_title[slot][0] = 0;
    if (title) { int n = 0; while (title[n] && n < 127) { g_ext_title[slot][n] = title[n]; n++; } g_ext_title[slot][n] = 0; }
    w->title = g_ext_title[slot];
    return w;
}
void wm_ext_set_title(void *win, const char *title) {
    struct Window *w = (struct Window *)win; if (!w || !w->external) return;
    int slot = (int)(w - g_store);
    int n = 0; if (title) while (title[n] && n < 127) { g_ext_title[slot][n] = title[n]; n++; }
    g_ext_title[slot][n] = 0; w->title = g_ext_title[slot];
}
void wm_ext_set_min(void *win, int minimized) {   /* sync taskbar state from the X frame's map state */
    struct Window *w = (struct Window *)win; if (!w || !w->external) return;
    if (minimized) { w->visible = 0; w->minimized = 1; }
    else           { w->visible = 1; w->minimized = 0; }
}
void wm_ext_del(void *win) {   /* the X client is gone: free the slot (no onclose) */
    struct Window *w = (struct Window *)win; if (!w || !w->external) return;
    wm_destroy(w);
}
int wm_ext_enum(unsigned long *out, int max) {   /* current external X client ids (for the sweep) */
    int n = 0;
    for (int i = 0; i < 16 && n < max; i++)
        if (g_store[i].alive && g_store[i].external) out[n++] = g_store[i].xclient;
    return n;
}
/* the platform layer polls this each frame; returns 0/1/2/3 and the target X client */
int wm_take_ext_action(unsigned long *client) {
    if (!g_ext_action) return 0;
    if (client) *client = g_ext_client;
    int a = g_ext_action; g_ext_action = 0; return a;
}

/* ---- message box (used by the Win32 MessageBoxA shim) ---- */
static struct Window *g_mb;
static char g_mb_text[256];
static char g_mb_title[48];

/* real Win95 message-box icons (USER.EXE), one per severity, cached at 32px */
#define MB_ICON_KEY RGB(0xFF, 0x00, 0xFF)
static int g_mb_sev;   /* 0 info, 1 warning, 2 error, 3 question */
static u32 g_mb_ic4[4][32 * 32]; static int g_mb_ok4[4], g_mb_try4[4];
static void mb_icon(int x, int y) {
    static const u16 uid[4] = { UI_INFO, UI_WARNING, UI_ERROR, UI_QUESTION };
    int s = g_mb_sev & 3;
    if (!g_mb_ok4[s] && !g_mb_try4[s]) {
        g_mb_try4[s] = 1;
        g_mb_ok4[s] = (icon_get_from("USER.EXE", uid[s], 32, MB_ICON_KEY, g_mb_ic4[s]) == 0);
    }
    if (g_mb_ok4[s]) {                                      /* blit the real icon */
        for (int j = 0; j < 32; j++)
            for (int i = 0; i < 32; i++) {
                u32 c = g_mb_ic4[s][j * 32 + i];
                if (c != MB_ICON_KEY) fb_pixel(x + i, y + j, c);
            }
        return;
    }
    int d = 28, r = d / 2;                                  /* fallback: drawn 'i' circle */
    for (int yy = 0; yy < d; yy++)
        for (int xx = 0; xx < d; xx++) {
            int dx = xx - r, dy = yy - r, rr = dx * dx + dy * dy;
            if (rr <= r * r)
                fb_pixel(x + xx, y + yy, rr <= (r - 2) * (r - 2) ? RGB(0x00, 0x00, 0xA0) : C_DK);
        }
    fb_fill_rect(x + r - 1, y + 6, 3, 3, C_WHITE);
    fb_fill_rect(x + r - 1, y + 11, 3, 9, C_WHITE);
}

static void mb_paint(int cx, int cy, int cw, int ch) {
    mb_icon(cx + 16, cy + 12);
    int lh = font_height() + 4;
    int y = cy + 16, x = cx + 56;
    for (const char *p = g_mb_text; *p; ) {
        char line[80]; int i = 0;
        while (*p && *p != '\n' && i < 79) line[i++] = *p++;
        line[i] = 0; if (*p == '\n') p++;
        fb_text_t(x, y, line, C_DK); y += lh;
    }
    int bw = 76, bh = 25, bx = cx + (cw - bw) / 2, by = cy + ch - bh - 12;
    cc_defbutton(bx, by, bw, bh, "OK", 0);
}
static void mb_click(int rx, int ry) { (void)rx; (void)ry; if (g_mb) g_mb->visible = 0; }

/* ---- a Yes/No confirmation dialog (calls onyes() when Yes is chosen) ---- */
static struct Window *g_cf; static char g_cf_text[256], g_cf_title[48];
static void (*g_cf_yes)(void);
static void cf_paint(int cx, int cy, int cw, int ch) {
    g_mb_sev = 3; mb_icon(cx + 16, cy + 12);           /* the question icon */
    int lh = font_height() + 4, y = cy + 16, x = cx + 56;
    for (const char *p = g_cf_text; *p; ) { char line[80]; int i = 0;
        while (*p && *p != '\n' && i < 79) line[i++] = *p++; line[i] = 0; if (*p == '\n') p++;
        fb_text_t(x, y, line, C_DK); y += lh; }
    int bw = 76, bh = 25, by = cy + ch - bh - 12, bx = cx + (cw - bw * 2 - 8) / 2;
    cc_defbutton(bx, by, bw, bh, "Yes", 0);
    cc_button(bx + bw + 8, by, bw, bh, "No", 0);
}
static void cf_click(int rx, int ry) {
    int cx, cy, cw, ch; wm_client_rect((Window *)g_cf, &cx, &cy, &cw, &ch); (void)cx; (void)cy;
    int bw = 76, bh = 25, by = ch - bh - 12, bx = (cw - bw * 2 - 8) / 2;
    if (ry >= by && ry < by + bh) {
        void (*yes)(void) = g_cf_yes;
        if (rx >= bx && rx < bx + bw) { g_cf->visible = 0; if (yes) yes(); return; }
        if (rx >= bx + bw + 8 && rx < bx + 2 * bw + 8) { g_cf->visible = 0; return; }
    }
}
void wm_confirm(const char *title, const char *text, void (*onyes)(void)) {
    snd_event("question");
    int i = 0; while (title[i] && i < 46) { g_cf_title[i] = title[i]; i++; } g_cf_title[i] = 0;
    i = 0; while (text[i] && i < 254) { g_cf_text[i] = text[i]; i++; } g_cf_text[i] = 0;
    g_cf_yes = onyes;
    int w = 340, h = 150;
    if (!g_cf) g_cf = (struct Window *)wm_add_custom(((int)fb_width() - w) / 2, ((int)fb_height() - h) / 2,
                                                     w, h, g_cf_title, cf_paint, 0, cf_click);
    else g_cf->title = g_cf_title;
    wm_show((Window *)g_cf);
}

void wm_messagebox_ex(const char *title, const char *text, int severity) {
    g_mb_sev = severity;
    snd_event(severity == 1 ? "warning" : severity == 2 ? "error" : severity == 3 ? "question" : "info");
    int i = 0; while (title[i] && i < 46) { g_mb_title[i] = title[i]; i++; } g_mb_title[i] = 0;
    i = 0; while (text[i] && i < 254) { g_mb_text[i] = text[i]; i++; } g_mb_text[i] = 0;
    int w = 320, h = 150;
    if (!g_mb) {
        g_mb = (struct Window *)wm_add_custom(((int)fb_width() - w) / 2, ((int)fb_height() - h) / 2,
                                              w, h, g_mb_title, mb_paint, 0, mb_click);
    } else {
        g_mb->title = g_mb_title;
    }
    wm_show((Window *)g_mb);
}
void wm_messagebox(const char *title, const char *text) { wm_messagebox_ex(title, text, 0); }

/* desktop icon inline-rename state (declared early: used by key dispatch) */
static int  g_ren_active, g_ren_uidx; static char g_ren_buf[40]; static int g_ren_len;
static char g_ren_pending[40];
static void ren_commit(void);
static void dk_set(char *d, const char *s);
static void dk_cat(char *d, const char *s);
/* logon modal (defined lower, used by key/mouse dispatch) */
static int  logon_is_active(void);
static void logon_key(char c);
static void logon_click(int mx, int my);
static struct Window *g_kbw; static int g_kbmode;   /* keyboard Move(0)/Size(1) mode */

/* Address deskband: open the typed text. A URL (http(s)://, www., or a bare
 * dotted host) opens the Web browser; anything else is treated as a path and
 * opened in Explorer. Uses the module/path hooks that already exist. */
int  app_launch_doc(int id, const char *path);   /* appreg.c */
extern void files_open_at(const char *abspath);  /* filemgr.c */
static int str_pre(const char *s, const char *p) { for (int i = 0; p[i]; i++) if (s[i] != p[i]) return 0; return 1; }
static void addr_commit(void) {
    g_addr_active = 0;
    char *s = g_addr_buf; while (*s == ' ') s++;
    if (!*s) { g_addr_buf[0] = 0; g_addr_len = 0; return; }
    int isurl = 0;
    if (str_pre(s, "http://") || str_pre(s, "https://") || str_pre(s, "www.")) isurl = 1;
    else if (s[0] != '/') {                          /* a bare host like example.com ? */
        int dot = 0, slash = 0;
        for (char *p = s; *p && *p != ' '; p++) { if (*p == '.') dot = 1; if (*p == '/') { slash = 1; break; } }
        if (dot && !slash) isurl = 1;
    }
    if (isurl) { if (!app_launch_doc(28, s)) wm_post_action(28); }   /* navigate the browser (fallback: just open it) */
    else files_open_at(s);                                      /* a path -> Explorer */
    g_addr_buf[0] = 0; g_addr_len = 0;
}

void wm_dispatch_key(char c) {
    if (logon_is_active()) { logon_key(c); return; }   /* logon modal grabs the keyboard */
    if (g_addr_active) {                               /* typing in the taskbar Address field */
        if (c == '\n' || c == '\r') { addr_commit(); return; }
        if (c == 27) { g_addr_active = 0; return; }                 /* Esc cancels focus */
        if (c == '\b') { if (g_addr_len) g_addr_buf[--g_addr_len] = 0; return; }
        if (c >= 32 && c < 127 && g_addr_len < (int)sizeof(g_addr_buf) - 1) { g_addr_buf[g_addr_len++] = c; g_addr_buf[g_addr_len] = 0; }
        return;
    }
    if (g_kbw) {                                  /* window Move/Size via the arrow keys */
        unsigned char uc = (unsigned char)c; int step = 8;
        int sw = (int)fb_width(), sh = (int)fb_height();
        if (uc == 0x91)      { if (g_kbmode) { if (g_kbw->h > 90) g_kbw->h -= step; } else g_kbw->y -= step; }
        else if (uc == 0x92) { if (g_kbmode) { if (g_kbw->y + g_kbw->h < sh) g_kbw->h += step; } else g_kbw->y += step; }
        else if (uc == 0x93) { if (g_kbmode) { if (g_kbw->w > 160) g_kbw->w -= step; } else g_kbw->x -= step; }
        else if (uc == 0x94) { if (g_kbmode) { if (g_kbw->x + g_kbw->w < sw) g_kbw->w += step; } else g_kbw->x += step; }
        else if (c == '\n' || c == '\r' || c == 27) g_kbw = 0;   /* commit / cancel */
        if (g_kbw) { if (g_kbw->x < -g_kbw->w + 40) g_kbw->x = -g_kbw->w + 40; if (g_kbw->y < 0) g_kbw->y = 0;
                     if (g_kbw->x > sw - 40) g_kbw->x = sw - 40; if (g_kbw->y > sh - 24) g_kbw->y = sh - 24; }
        return;
    }
    if (g_ren_active) {                          /* editing a desktop icon's name */
        if (c == '\n' || c == '\r') { ren_commit(); return; }
        if (c == 27) { g_ren_active = 0; return; }               /* Esc cancels */
        if (c == '\b') { if (g_ren_len) g_ren_buf[--g_ren_len] = 0; return; }
        if (c >= 32 && c != '/' && c != '\\' && g_ren_len < 38) { g_ren_buf[g_ren_len++] = c; g_ren_buf[g_ren_len] = 0; }
        return;
    }
    struct Window *f = front_visible();
    if (f && f->onkey) f->onkey(c);
}

void wm_set_menu(Window *w, const WmMenu *menus, int n, void (*onmenu)(int)) {
    if (!w) return;
    w->menus = menus; w->nmenus = n; w->onmenu = onmenu;
}
void wm_set_menuflags(Window *w, int (*cb)(int)) { if (w) w->menuflags = cb; }
/* effective per-item state: static fields OR the dynamic callback */
static int menu_item_flags(struct Window *w, const WmMenuItem *it) {
    int f = (it->checked ? 1 : 0) | (it->disabled ? 2 : 0);
    if (w->menuflags) f |= w->menuflags(it->id);
    return f;
}

void wm_client_rect(Window *w, int *cx, int *cy, int *cw, int *ch) {
    int mh = w->nmenus ? WM_MENUH : 0;
    *cx = w->x + 3;
    *cy = w->y + 3 + WM_TITLEH + 1 + mh;
    *cw = w->w - 6;
    *ch = w->h - (3 + WM_TITLEH + 1) - 3 - mh;
}

static struct Window *front_visible(void) {
    for (int i = g_nz - 1; i >= 0; i--) if (g_z[i]->visible && !g_z[i]->external) return g_z[i];
    return 0;
}
static void bring_front(struct Window *w) {
    int i = 0; while (i < g_nz && g_z[i] != w) i++;
    if (i >= g_nz) return;
    for (; i < g_nz - 1; i++) g_z[i] = g_z[i + 1];
    g_z[g_nz - 1] = w;
}

static int g_cx = 512, g_cy = 384;            /* cursor position (used for menu hover) */

/* ---- window menu bars (File / Edit / View ...) ---- */
static struct Window *g_menu_win;    /* window whose menu bar is active */
static int g_menu_open = -1;         /* open top-level index, -1 = none */

static int menubar_label_x(struct Window *w, int idx, int *width) {
    int x = w->x + 5;
    for (int i = 0; i < idx; i++) x += (int)font_measure(w->menus[i].label) + 16;
    if (width) *width = (int)font_measure(w->menus[idx].label) + 16;
    return x;
}
static int menubar_y(struct Window *w) { return w->y + 3 + WM_TITLEH + 1; }

static void draw_menubar(struct Window *w) {
    int my = menubar_y(w);
    fb_fill_rect(w->x + 3, my, w->w - 6, WM_MENUH, C_FACE);
    for (int i = 0; i < w->nmenus; i++) {
        int lw, lx = menubar_label_x(w, i, &lw);
        int open = (g_menu_win == w && g_menu_open == i);
        if (open) fb_fill_rect(lx, my, lw, WM_MENUH, C_SEL);
        int mty = my + (WM_MENUH - (int)font_height()) / 2;    /* vertically centred */
        fb_text_t(lx + 8, mty, w->menus[i].label, open ? C_WHITE : C_DK);
        u8 aw; font_glyph(w->menus[i].label[0], &aw);          /* accelerator underline */
        fb_fill_rect(lx + 8, mty + (int)font_height() - 1, aw, 1, open ? C_WHITE : C_DK);
    }
}
static void dropdown_geom(struct Window *w, int idx, int *dx, int *dy, int *dw, int *dh) {
    const WmMenu *m = &w->menus[idx];
    int lw, lx = menubar_label_x(w, idx, &lw);
    int width = 120;
    for (int i = 0; i < m->nitems; i++) {
        if (m->items[i].id < 0) continue;
        int tw = (int)font_measure(m->items[i].label) + 44;
        if (tw > width) width = tw;
    }
    int x = lx, y = menubar_y(w) + WM_MENUH;
    if (x + width > (int)fb_width()) x = (int)fb_width() - width;
    *dx = x; *dy = y; *dw = width; *dh = m->nitems * 18 + 6;
}
static void draw_menu_dropdown(void) {
    if (!g_menu_win || g_menu_open < 0) return;
    struct Window *w = g_menu_win; const WmMenu *m = &w->menus[g_menu_open];
    int dx, dy, dw, dh; dropdown_geom(w, g_menu_open, &dx, &dy, &dw, &dh);
    fb_fill_rect(dx, dy, dw, dh, C_FACE); wm_bevel_raised(dx, dy, dw, dh);
    for (int i = 0; i < m->nitems; i++) {
        int iy = dy + 3 + i * 18;
        if (m->items[i].id < 0) { hline(dx + 4, iy + 8, dw - 8, C_SHADOW); hline(dx + 4, iy + 9, dw - 8, C_HILIGHT); continue; }
        int fl = menu_item_flags(w, &m->items[i]);
        int dis = fl & 2;
        int hov = !dis && g_cx >= dx && g_cx < dx + dw && g_cy >= iy && g_cy < iy + 18;
        if (hov) fb_fill_rect(dx + 2, iy, dw - 4, 18, C_SEL);
        if (fl & 1) {                                 /* check mark (pixel, left gutter) */
            u32 ck = dis ? C_SHADOW : (hov ? C_WHITE : C_DK);
            for (int k = 0; k < 2; k++) {
                fb_pixel(dx + 6, iy + 8 + k, ck); fb_pixel(dx + 7, iy + 9 + k, ck);
                fb_pixel(dx + 8, iy + 10 + k, ck); fb_pixel(dx + 9, iy + 8 + k, ck);
                fb_pixel(dx + 10, iy + 6 + k, ck); fb_pixel(dx + 11, iy + 4 + k, ck);
            }
        }
        u32 fg = dis ? RGB(0x80,0x80,0x80) : (hov ? C_WHITE : C_DK);
        if (dis && !hov) fb_text_t(dx + 21, iy + 4, m->items[i].label, RGB(0xFF,0xFF,0xFF));  /* Win95 gray embossed */
        fb_text_t(dx + 20, iy + 3, m->items[i].label, fg);
    }
}
/* left-click on a window menu bar / open dropdown; returns 1 if consumed */
static int menubar_click(void) {
    if (g_menu_win && g_menu_open >= 0) {
        struct Window *w = g_menu_win; const WmMenu *m = &w->menus[g_menu_open];
        int dx, dy, dw, dh; dropdown_geom(w, g_menu_open, &dx, &dy, &dw, &dh);
        for (int i = 0; i < m->nitems; i++) {
            int iy = dy + 3 + i * 18;
            if (m->items[i].id >= 0 && g_cx >= dx && g_cx < dx + dw && g_cy >= iy && g_cy < iy + 18) {
                if (menu_item_flags(w, &m->items[i]) & 2) return 1;   /* disabled: swallow, don't fire */
                int id = m->items[i].id; void (*cb)(int) = w->onmenu;
                g_menu_open = -1; g_menu_win = 0;
                if (cb) cb(id);
                return 1;
            }
        }
        int my = menubar_y(w);
        if (g_cy >= my && g_cy < my + WM_MENUH)
            for (int j = 0; j < w->nmenus; j++) {
                int lw, lx = menubar_label_x(w, j, &lw);
                if (g_cx >= lx && g_cx < lx + lw) { g_menu_open = j; return 1; }
            }
        g_menu_open = -1; g_menu_win = 0;
        return 1;
    }
    for (int i = g_nz - 1; i >= 0; i--) {
        struct Window *w = g_z[i];
        if (!w->visible || !w->nmenus) continue;
        int my = menubar_y(w);
        if (g_cy < my || g_cy >= my + WM_MENUH || g_cx < w->x || g_cx >= w->x + w->w) continue;
        for (int j = 0; j < w->nmenus; j++) {
            int lw, lx = menubar_label_x(w, j, &lw);
            if (g_cx >= lx && g_cx < lx + lw) { bring_front(w); g_menu_win = w; g_menu_open = j; return 1; }
        }
        return 1;   /* clicked the bar but between labels: consume */
    }
    return 0;
}

static void draw_window(struct Window *w, int active) {
    fb_fill_rect(w->x, w->y, w->w, w->h, C_FACE);
    wm_bevel_raised(w->x, w->y, w->w, w->h);
    if (g_theme->style == STYLE_XP) {                    /* XP: coloured window border */
        u32 edge = active ? C_TITLE_L : C_INACT;
        fb_rect(w->x, w->y, w->w, w->h, edge);
        fb_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, edge);
    }
    int tx = w->x + 3, ty = w->y + 3, tw = w->w - 6;
    if (active) title_gradient(tx, ty, tw, WM_TITLEH);
    else        fb_fill_rect(tx, ty, tw, WM_TITLEH, C_INACT);
    win_icon(tx + 2, ty + 1, w->icon_mod, w->icon);
    int cty = ty + (WM_TITLEH - (int)font_height()) / 2;    /* vertically centred caption */
    fb_text_bold_t(tx + 20, cty, w->title, active ? C_WHITE : RGB(0xD0, 0xD0, 0xD0));
    int bx = w->x + w->w - 3 - 18;
    title_button(bx, ty + 2, 'x');
    title_button(bx - 17, ty + 2, w->maxed ? 'r' : 'o');
    title_button(bx - 34, ty + 2, '_');
    if (w->help) title_button(bx - 51, ty + 2, '?');   /* What's This? context help */

    if (w->nmenus) draw_menubar(w);

    int cx, cy, cw, ch; wm_client_rect(w, &cx, &cy, &cw, &ch);
    if (w->paint) { w->paint(cx, cy, cw, ch); }
    else {
        int lh = font_height() + 3;
        for (int i = 0; i < w->nlines; i++)
            fb_text_t(cx + 6, cy + 4 + i * lh, w->lines[i], C_DK);
    }
    /* Win95 draws the diagonal corner grip only on windows that have a status
     * bar (Explorer, Find, Recycle Bin) - three dotted diagonal ridges. */
    if (w->sizegrip && !w->maxed) {
        int cxg = w->x + w->w - 4, cyg = w->y + w->h - 4;
        for (int line = 1; line <= 3; line++) {
            int L = line * 4;
            for (int i = 0; i < L; i++) {
                int px = cxg - i, py = cyg - (L - 1 - i);
                if (px <= w->x + 3 || py <= w->y + 3) continue;
                if (((px + py) & 1) == 0) continue;              /* dotted */
                fb_pixel(px, py, C_SHADOW);
                fb_pixel(px + 1, py + 1, C_HILIGHT);
            }
        }
    }
}
void wm_set_sizegrip(Window *w) { if (w) ((struct Window *)w)->sizegrip = 1; }
void wm_set_size(Window *w, int nw, int nh) { if (w) { ((struct Window *)w)->w = nw; ((struct Window *)w)->h = nh; } }

/* real shell32 icons (magenta = transparent, blitted with blit_key). Loaded
 * once into caches: 3 desktop shortcuts @32px, 7 Start-menu items @16px, and a
 * taskbar app icon @16px. */
#define ICON_KEY RGB(0xFF, 0x00, 0xFF)
static u32 g_di[3][32 * 32];   static int g_di_ok[3];   /* app shortcuts: Files/Editor/Paint */
static u32 g_sys[3][32 * 32];  static int g_sys_ok[3];  /* My Computer / Network / Recycle Bin */
static u32 g_mi_ic[7][16 * 16]; static int g_mi_ok[7];  /* Start-menu glyphs */
static u32 g_tb_ic[16 * 16];   static int g_tb_ok;      /* taskbar app icon */
static u32 g_flag[16 * 16];    static int g_flag_ok;    /* Start-button flag */
static u32 g_tray_spk[16 * 16]; static int g_tray_spk_ok;  /* tray volume speaker (mmsys.cpl) */
static u32 g_tray_net[16 * 16]; static int g_tray_net_ok;  /* tray network (shell32) */
static u32 g_ic_bmp[32 * 32];  static int g_ic_bmp_ok;   /* bitmap-document icon (mspaint id 3) */
static u32 g_recycle_full[32 * 32]; static int g_recycle_full_ok;   /* recycle bin with items */
static int g_icons_loaded;

static void wm_load_icons(void) {
    if (g_icons_loaded) return;
    static const u16 d[3] = { IC_FOLDER, IC_DOC, IC_GRAPHICS };
    static const u16 s[3] = { IC_MYCOMPUTER, IC_NETWORK, IC_RECYCLE };
    static const u16 m[7] = { IC_FOLDER, IC_DOC, IC_GRAPHICS, IC_SETTINGS, IC_FIND, IC_HELP, IC_WINLOGO };
    int any = 0;
    for (int i = 0; i < 2; i++) if (!g_di_ok[i]  && icon_get(d[i], 32, ICON_KEY, g_di[i])  == 0) { g_di_ok[i]  = 1; any = 1; }
    if (!g_di_ok[2] && icon_get_from("MSPAINT.EXE", 1, 32, ICON_KEY, g_di[2]) == 0) { g_di_ok[2] = 1; any = 1; }  /* Paint shortcut */
    if (!g_ic_bmp_ok && icon_get_from("MSPAINT.EXE", 3, 32, ICON_KEY, g_ic_bmp) == 0) g_ic_bmp_ok = 1;  /* .bmp document */
    if (!g_recycle_full_ok && icon_get(IC_RECYCLE_FULL, 32, ICON_KEY, g_recycle_full) == 0) g_recycle_full_ok = 1;
    for (int i = 0; i < 3; i++) if (!g_sys_ok[i] && icon_get(s[i], 32, ICON_KEY, g_sys[i]) == 0) { g_sys_ok[i] = 1; any = 1; }
    for (int i = 0; i < 7; i++) if (!g_mi_ok[i]  && icon_get(m[i], 16, ICON_KEY, g_mi_ic[i]) == 0) g_mi_ok[i] = 1;
    if (!g_tb_ok   && icon_get(IC_APP,     16, ICON_KEY, g_tb_ic) == 0) g_tb_ok = 1;
    if (!g_flag_ok && icon_get_from("USER.EXE", UI_FLAG16, 16, ICON_KEY, g_flag) == 0) g_flag_ok = 1;
    if (!g_tray_spk_ok && icon_get_from("MMSYS.CPL", 7, 16, ICON_KEY, g_tray_spk) == 0) g_tray_spk_ok = 1;
    if (!g_tray_net_ok && icon_get(IC_NETWORK16, 16, ICON_KEY, g_tray_net) == 0) g_tray_net_ok = 1;
    if (any) g_icons_loaded = 1;                   /* else retry next frame (FS not ready) */
}

static void blit_key(int x, int y, int w, int h, const u32 *px) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            u32 c = px[j * w + i];
            if (c != ICON_KEY) fb_pixel(x + i, y + j, c);
        }
}

/* ---- start menu ---- */
static int g_start_open;
static int g_action;                          /* pending action from a menu click */
/* cascading Start menu, data-driven. A leaf carries act>0; a submenu carries
 * sub/nsub; act==-1 is a separator. */
typedef struct SMItem { const char *label; int act; const struct SMItem *sub; int nsub; u16 icon; const char *imod; } SMItem;
/* Accessories > System Tools > { ScanDisk, Disk Defragmenter } */
static const SMItem SM_systools[] = {
    { "System Information", 60, 0, 0, IC_APP }, { "Device Manager", 64, 0, 0, IC_MYCOMPUTER },
    { "Event Viewer", 65, 0, 0, IC_DOC }, { "System Log", 63, 0, 0, IC_DOC },
    { "Scheduled Tasks", 68, 0, 0, IC_SETTINGS },
    { "Remote Access", 73, 0, 0, IC_APP },
    { "Windows Firewall", 74, 0, 0, IC_SETTINGS }, { "System Restore", 75, 0, 0, IC_SETTINGS },
    { "ScanDisk", 40, 0, 0, IC_APP }, { "Disk Defragmenter", 41, 0, 0, IC_APP },
    { "Backup", 76, 0, 0, IC_APP },
    { "Windows Update", 77, 0, 0, IC_SETTINGS },
    { "Install Debian95...", 50, 0, 0, IC_SETTINGS },
};
static const SMItem SM_acc[] = {
    { "System Tools", 0, SM_systools, 13, IC_FOLDER },
    { "Calculator", 10, 0, 0, IC_APP }, { "Calendar", 61, 0, 0, IC_APP },
    { "Character Map", 14, 0, 0, IC_DOC },
    { "HyperTerminal", 67, 0, 0, IC_APP }, { "Dial-Up Networking", 78, 0, 0, IC_NETWORK },
    { "Snipping Tool", 69, 0, 0, IC_DOC },
    { "Narrator", 70, 0, 0, IC_APP }, { "Clippy", 72, 0, 0, IC_APP },
    { "Clipboard Viewer", 16, 0, 0, IC_DOC }, { "Clock", 11, 0, 0, IC_APP },
    { "Notepad", 4, 0, 0, IC_DOC }, { "Paint", 5, 0, 0, 2, "MSPAINT.EXE" },
    { "Media Player", 13, 0, 0, IC_APP }, { "System Monitor", 22, 0, 0, IC_APP },
    { "Volume Control", 25, 0, 0, IC_APP }, { "Sound Recorder", 26, 0, 0, IC_APP },
    { "WordPad", 27, 0, 0, IC_DOC },
    { "Registry Editor", 29, 0, 0, IC_APP }, { "Task Manager", 31, 0, 0, IC_APP },
    { "Services", 33, 0, 0, IC_APP }, { "Resource Viewer", 34, 0, 0, IC_APP },
};
static const SMItem SM_games[] = { { "FreeCell", 24, 0, 0, IC_APP }, { "Minesweeper", 9, 0, 0, IC_APP }, { "Reversi", 62, 0, 0, IC_APP }, { "Solitaire", 12, 0, 0, IC_APP } };
/* mutable + spare capacity: the platform layer appends downloaded apps (VLC,
 * Chromium) after the built-ins via wm_register_program (see below). */
static SMItem SM_prog[6 + 58] = {
    { "Accessories", 0, SM_acc, 18, IC_FOLDER }, { "Games", 0, SM_games, 4, IC_FOLDER },
    { "MS-DOS Prompt", 15, 0, 0, 1, "PIFMGR.DLL" }, { "Debian95 Terminal", 23, 0, 0, 1, "PIFMGR.DLL" },
    { "Windows Explorer", 3, 0, 0, IC_FOLDER }, { "Web Browser", 28, 0, 0, IC_APP },
};
static int g_nsm_prog = 6;
#define NSM_PROG 6
/* category submenus for discovered/downloaded apps (so Start > Programs groups them
 * like a real system). Index: 0 Internet, 1 Multimedia, 2 Graphics, 3 Office; a
 * category of -1 stays flat at the top level. Games/Accessories already have folders. */
#define APP_CATS 4
static const char *APP_CAT_NAME[APP_CATS] = { "Internet", "Multimedia", "Graphics", "Office" };
static SMItem g_catitems[APP_CATS][24]; static int g_catn[APP_CATS]; static int g_cat_folder[APP_CATS];
static const SMItem SM_themes[] = {
    { "Windows 95", 90, 0, 0, IC_APP }, { "Windows 98", 91, 0, 0, IC_APP },
    { "Windows XP", 92, 0, 0, IC_APP }, { "Modern (Flat)", 93, 0, 0, IC_APP },
    { "Windows XP (Olive)", 94, 0, 0, IC_APP }, { "Windows XP (Silver)", 95, 0, 0, IC_APP },
    { "Windows Vista (Aero)", 96, 0, 0, IC_APP }, { "Windows 7 (Aero)", 97, 0, 0, IC_APP },
    { "Desert", 98, 0, 0, IC_APP }, { "Rose", 99, 0, 0, IC_APP },
    { "Eggplant", 100, 0, 0, IC_APP }, { "Spruce", 101, 0, 0, IC_APP },
    { "Brick", 102, 0, 0, IC_APP }, { "High Contrast Black", 103, 0, 0, IC_APP },
};
static const SMItem SM_settings[] = {
    { "Control Panel", 6, 0, 0, IC_SETTINGS }, { "Display", 20, 0, 0, IC_SETTINGS },
    { "Printers", 32, 0, 0, IC_APP },
    { "Theme", 0, SM_themes, 14, IC_SETTINGS },
    { "Desktop Gadgets", 53, 0, 0, IC_SETTINGS },
    { "Taskbar & Start Menu...", 21, 0, 0, IC_SETTINGS },
};
static const SMItem SM_find[] = { { "Files or Folders...", 7, 0, 0, IC_FIND }, { "Search (indexed)...", 79, 0, 0, IC_FIND } };
/* Documents is a REAL most-recently-used list, rebuilt from persisted state. */
#define MRU_MAX 8
static char g_mru[MRU_MAX][64]; static int g_nmru; static int g_mru_loaded;
static SMItem SM_docs[MRU_MAX] = { { "(Empty)", 0, 0, 0, 0 } };
static SMItem SM_main[] = {
    { "Programs", 0, SM_prog, NSM_PROG, IC_FOLDER },
    { "Documents", 0, SM_docs, 1, IC_OPENDIR },
    { "Settings", 0, SM_settings, 6, IC_SETTINGS },
    { "Find", 0, SM_find, 2, IC_FIND },
    { "Help", 8, 0, 0, IC_HELP },
    { "-", -1, 0, 0, 0 },
    { "Run...", 30, 0, 0, IC_APP },
    { "Log Off...", 52, 0, 0, IC_SETTINGS },
    { "Lock Computer", 51, 0, 0, IC_SETTINGS },
    { "Shut Down...", 2, 0, 0, IC_SHUTDOWN },
};
#define NSM_MAIN ((int)(sizeof(SM_main)/sizeof(SM_main[0])))

/* ---- Documents MRU: real, persisted list of recently-opened files ---- */
int  sys_get_setting(const char *key, char *out, int n);   /* shims */
void sys_set_setting(const char *key, const char *val);
#define MRU_ACT_BASE 200                       /* menu act ids MRU_ACT_BASE+i */
static void mru_basename(const char *p, char *o) {         /* keep the leaf name */
    const char *b = p; for (const char *s = p; *s; s++) if (*s == '/' || *s == '\\') b = s + 1;
    int k = 0; for (; b[k] && k < 63; k++) o[k] = b[k]; o[k] = 0;
}
static void mru_load(void) {
    if (g_mru_loaded) return; g_mru_loaded = 1;
    char key[8] = "mru0", v[64];
    for (int i = 0; i < MRU_MAX; i++) { key[3] = (char)('0' + i);
        if (sys_get_setting(key, v, sizeof v) == 0 && v[0]) { int k = 0; for (; v[k] && k < 63; k++) g_mru[g_nmru][k] = v[k]; g_mru[g_nmru][k] = 0; g_nmru++; } }
}
static void mru_save(void) {
    char key[8] = "mru0";
    for (int i = 0; i < MRU_MAX; i++) { key[3] = (char)('0' + i); sys_set_setting(key, i < g_nmru ? g_mru[i] : ""); }
}
/* record a freshly opened document at the top of the MRU (dedup, cap, persist) */
void wm_add_recent(const char *path) {
    if (!path || !path[0]) return;
    mru_load();
    char nm[64]; mru_basename(path, nm); if (!nm[0]) return;
    int at = -1;
    for (int i = 0; i < g_nmru; i++) { int eq = 1; for (int k = 0;; k++) { if (g_mru[i][k] != nm[k]) { eq = 0; break; } if (!nm[k]) break; } if (eq) { at = i; break; } }
    if (at < 0) { if (g_nmru < MRU_MAX) g_nmru++; at = g_nmru - 1; }
    for (int i = at; i > 0; i--) { for (int k = 0; k < 64; k++) g_mru[i][k] = g_mru[i - 1][k]; }
    { int k = 0; for (; nm[k] && k < 63; k++) g_mru[0][k] = nm[k]; g_mru[0][k] = 0; }
    mru_save();
}
static void mru_rebuild(void) {                 /* refresh the Documents fly-out from the MRU */
    mru_load();
    if (g_nmru == 0) { SM_docs[0].label = "(Empty)"; SM_docs[0].act = 0; SM_docs[0].sub = 0; SM_docs[0].nsub = 0; SM_docs[0].icon = 0; SM_docs[0].imod = 0; SM_main[1].nsub = 1; return; }
    for (int i = 0; i < g_nmru; i++) { SM_docs[i].label = g_mru[i]; SM_docs[i].act = MRU_ACT_BASE + i; SM_docs[i].sub = 0; SM_docs[i].nsub = 0; SM_docs[i].icon = IC_DOC; SM_docs[i].imod = 0; }
    SM_main[1].nsub = g_nmru;
}
/* open a recent document by MRU index (Notepad for text, viewer for images) */
static void mru_open(int i) {
    if (i < 0 || i >= g_nmru) return;
    const char *nm = g_mru[i];
    int isbmp = 0; { int l = 0; while (nm[l]) l++; if (l > 4) { const char *e = nm + l - 4;
        char a = e[1], b = e[2], c = e[3]; if (a>='A'&&a<='Z')a+=32; if (b>='A'&&b<='Z')b+=32; if (c>='A'&&c<='Z')c+=32;
        if (e[0]=='.' && a=='b'&&b=='m'&&c=='p') isbmp = 1; } }
    if (isbmp) viewer_open(nm); else app_launch_doc(4, nm);   /* Notepad (module) */
}
static void mru_clear(void) { g_nmru = 0; mru_save(); }   /* Taskbar Properties: Clear Documents */

/* ---- Taskbar Properties (Start > Settings > Taskbar) ---- */
static Window *g_tbp; static int g_tbp_tab, g_tb_autohide;
static void tbp_close(void) { g_tbp = 0; }
static void tbp_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, C_FACE);
    static const char *tabs[] = { "Taskbar Options", "Start Menu Programs" };
    cc_tabs(cx + 6, cy + 6, 120, tabs, 2, g_tbp_tab);
    int th = (int)font_height() + 8;
    int px = cx + 10, py = cy + 6 + th + 8, pw = cw - 20, ph = ch - (6 + th) - 52;
    fb_fill_rect(px, py, pw, ph, C_FACE); wm_bevel_raised(px, py, pw, ph);
    if (g_tbp_tab == 0) {
        cc_checkbox(px + 16, py + 16, "Always on top", 1);
        cc_checkbox(px + 16, py + 40, "Auto hide", g_tb_autohide);
        cc_checkbox(px + 16, py + 64, "Show small icons in Start menu", g_small_start);
        cc_checkbox(px + 16, py + 88, "Show clock", g_show_clock);
    } else {
        cc_label(px + 16, py + 12, "Customize Start menu");
        cc_label(px + 16, py + 44, "Documents menu");
        cc_button(px + 16, py + 62, 110, 22, "Clear", 0);
        cc_label(px + 136, py + 66, g_nmru ? "" : "(already empty)");
    }
    int by = cy + ch - 32, bw = 74;
    cc_defbutton(cx + cw - bw * 2 - 15, by, bw, 24, "OK", 0);
    cc_button(cx + cw - bw - 8, by, bw, 24, "Cancel", 0);
}
static void tbp_click(int rx, int ry) {
    int cx, cy, cw, ch; wm_client_rect(g_tbp, &cx, &cy, &cw, &ch); (void)cx; (void)cy;
    int th = (int)font_height() + 8;
    int t = cc_tab_hit(6, 6, 120, 2, rx, ry); if (t >= 0) { g_tbp_tab = t; return; }
    int px = 10, py = 6 + th + 8;
    if (g_tbp_tab == 0 && rx >= px + 16 && rx < px + 240) {
        if (ry >= py + 40 && ry < py + 56) { g_tb_autohide = !g_tb_autohide; sys_set_setting("tb_autohide", g_tb_autohide ? "1" : "0"); return; }
        if (ry >= py + 64 && ry < py + 80) { g_small_start = !g_small_start; sys_set_setting("small_start", g_small_start ? "1" : "0"); return; }
        if (ry >= py + 88 && ry < py + 104) { g_show_clock = !g_show_clock; sys_set_setting("show_clock", g_show_clock ? "1" : "0"); return; }
    }
    if (g_tbp_tab == 1 && rx >= px + 16 && rx < px + 126 && ry >= py + 62 && ry < py + 84) { mru_clear(); return; }
    int by = ch - 32, bw = 74;
    if (ry >= by && ry < by + 24 && rx >= cw - bw * 2 - 15) wm_hide(g_tbp);
}
void wm_taskbar_props(void) {
    char v[8];
    if (sys_get_setting("tb_autohide", v, sizeof v) == 0 && v[0]) g_tb_autohide = v[0] == '1';
    if (!g_tbp) g_tbp = wm_add_custom(200, 130, 330, 250, "Taskbar Properties", tbp_paint, 0, tbp_click);
    wm_set_close(g_tbp, tbp_close);
    wm_show(g_tbp);
}

static struct { int x, y, w, h, act, is_sub, level, idx; } g_mi[64];
static int g_nmi;
static int g_sm_path[3] = { -1, -1, -1 };   /* open submenu index per level */

/* lazy 16px icon cache keyed by (module, id) — module 0 means SHELL32.DLL */
static const char *g_smic_mod[20]; static u16 g_smic_id[20]; static u32 g_smic_buf[20][16 * 16]; static int g_smic_ok[20]; static int g_smic_n;
static int smic_mod_eq(const char *a, const char *b) { if (a == b) return 1; if (!a || !b) return 0; int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
static const u32 *sm_icon_m(const char *mod, u16 id) {
    if (!id) return 0;
    for (int i = 0; i < g_smic_n; i++) if (g_smic_id[i] == id && smic_mod_eq(g_smic_mod[i], mod)) return g_smic_ok[i] ? g_smic_buf[i] : 0;
    if (g_smic_n >= 20) return 0;
    int s = g_smic_n++; g_smic_id[s] = id; g_smic_mod[s] = mod;
    g_smic_ok[s] = (icon_get_from(mod ? mod : "SHELL32.DLL", id, 16, ICON_KEY, g_smic_buf[s]) == 0);
    return g_smic_ok[s] ? g_smic_buf[s] : 0;
}
static const u32 *sm_icon(u16 id) { return sm_icon_m(0, id); }

/* Start button width depends on the UX era: classic = flag + "Start"; XP = a wider
 * green "start" pill; Aero (Vista/7) = a round orb. */
static int start_btn_w(void) {
    switch (g_theme->era) {
        case ERA_AERO: return 30;                                           /* round orb */
        case ERA_XP:   return 5 + 18 + 5 + (int)fb_measure_bold("start") + 14;
        default:       return 4 + 16 + 2 + (int)fb_measure_bold("Start") + 6;
    }
}
static int start_hit(int x, int y) {
    int ty = (int)fb_height() - TB_H;
    return x >= 4 && x < 4 + start_btn_w() && y >= ty + 3 && y < ty + 27;
}
#define SM_ROWH 24                                   /* Win95 Start-menu item height */
static int sm_height(const SMItem *items, int n) {
    int h = 6; for (int i = 0; i < n; i++) h += items[i].act == -1 ? 9 : SM_ROWH; return h;
}
/* draw one row at (x,y,w); record it as clickable; returns row height */
static int sm_row(int x, int y, int w, const SMItem *it, int level, int idx) {
    if (it->act == -1) { hline(x + 8, y + 4, w - 16, C_SHADOW); hline(x + 8, y + 5, w - 16, C_HILIGHT); return 9; }
    int hov = g_cx >= x && g_cx < x + w && g_cy >= y && g_cy < y + SM_ROWH;
    int hl = hov || (it->sub && g_sm_path[level] == idx);
    if (hl) fb_fill_rect(x, y, w, SM_ROWH, C_SEL);
    const u32 *ic = sm_icon_m(it->imod, it->icon);
    if (ic) blit_key(x + 4, y + (SM_ROWH - 16) / 2, 16, 16, ic);
    else { fb_fill_rect(x + 4, y + 4, 16, 14, RGB(0x60, 0x80, 0xC0)); wm_bevel_raised(x + 4, y + 4, 16, 14); }
    fb_text_t(x + 26, y + (SM_ROWH - (int)font_height()) / 2, it->label, hl ? C_WHITE : C_DK);
    if (it->sub) {                                   /* right-pointing submenu arrow */
        static const int tw[5] = { 1, 2, 3, 2, 1 };
        int ay = y + (SM_ROWH - 9) / 2;
        for (int k = 0; k < 5; k++) fb_fill_rect(x + w - 12, ay + k, tw[k], 1, hl ? C_WHITE : C_DK);
    }
    g_mi[g_nmi].x = x; g_mi[g_nmi].y = y; g_mi[g_nmi].w = w; g_mi[g_nmi].h = SM_ROWH;
    g_mi[g_nmi].act = it->act; g_mi[g_nmi].is_sub = it->sub ? 1 : 0;
    g_mi[g_nmi].level = level; g_mi[g_nmi].idx = idx; g_nmi++;
    return SM_ROWH;
}
static void draw_flyout(const SMItem *items, int n, int x, int y, int level) {
    int w = 150;
    for (int i = 0; i < n; i++) if (items[i].act != -1) {
        int tw = (int)font_measure(items[i].label) + 44; if (tw > w) w = tw;
    }
    int h = sm_height(items, n);
    if (x + w > (int)fb_width()) x = (int)fb_width() - w;
    if (y + h > (int)fb_height() - TB_H) y = (int)fb_height() - TB_H - h;
    fb_fill_rect(x, y, w, h, C_FACE); wm_bevel_raised(x, y, w, h);
    int iy = y + 3;
    for (int i = 0; i < n; i++) {
        int rh = sm_row(x + 2, iy, w - 4, &items[i], level, i);
        if (items[i].sub && g_sm_path[level] == i)
            draw_flyout(items[i].sub, items[i].nsub, x + w - 2, iy - 3, level + 1);
        iy += rh;
    }
}
/* the Start-menu wordmark: 8x8 glyphs rotated 90 deg CCW and stacked upward at
 * 2x, like the "Windows 95" sidebar. First char sits at the bottom. */
static void sidebar_text(int x, int y_bottom, const char *s, u32 fg) {
    for (int i = 0; s[i]; i++) {
        int gy = y_bottom - i * 16;
        u8 adv; const u16 *rows = font_builtin_glyph(s[i], &adv);
        if (!rows) continue;
        for (int r = 0; r < 8; r++)
            for (int col = 0; col < 8; col++)
                if ((rows[r] >> col) & 1)
                    fb_fill_rect(x + r * 2, gy + (7 - col) * 2, 2, 2, fg);
    }
}
static void vgrad(int x, int y, int w, int h, u32 lo, u32 hi);   /* defined below (taskbar) */
static void panes(int x, int y, int s);                          /* generic four-pane mark */
/* ---- XP / Vista / 7 two-pane Start menu (left = programs, right = places;
 * a user band on top, Log Off / Turn Off on the bottom). Original layout. ---- */
static const SMItem SM_xp_pin[] = {
    { "Web Browser", 28, 0, 0, IC_APP }, { "Debian95 Terminal", 23, 0, 0, 1, "PIFMGR.DLL" },
    { "Notepad", 4, 0, 0, IC_DOC }, { "Paint", 5, 0, 0, 2, "MSPAINT.EXE" },
    { "Media Player", 13, 0, 0, IC_APP }, { "Files", 3, 0, 0, IC_FOLDER },
};
static const SMItem SM_xp_sys[] = {
    { "My Computer", 18, 0, 0, IC_MYCOMPUTER }, { "Control Panel", 6, 0, 0, IC_SETTINGS },
    { "Network", 66, 0, 0, IC_APP }, { "Search...", 7, 0, 0, IC_FIND },
    { "Run...", 30, 0, 0, IC_APP }, { "Help and Support", 0, 0, 0, IC_DOC },
};
static int xp_row(int x, int y, int w, const SMItem *it, int level, int idx) {
    int hov = g_cx >= x && g_cx < x + w && g_cy >= y && g_cy < y + SM_ROWH;
    int hl = hov || (it->sub && g_sm_path[level] == idx);
    if (hl) fb_fill_rect(x, y, w, SM_ROWH, g_theme->sel);
    const u32 *ic = sm_icon_m(it->imod, it->icon);
    if (ic) blit_key(x + 4, y + (SM_ROWH - 16) / 2, 16, 16, ic);
    fb_text_t(x + 26, y + (SM_ROWH - (int)font_height()) / 2, it->label, hl ? C_WHITE : C_DK);
    if (it->sub) { static const int tw[5] = { 1, 2, 3, 2, 1 }; int ay = y + (SM_ROWH - 9) / 2;
        for (int k = 0; k < 5; k++) fb_fill_rect(x + w - 12, ay + k, tw[k], 1, hl ? C_WHITE : C_DK); }
    g_mi[g_nmi].x = x; g_mi[g_nmi].y = y; g_mi[g_nmi].w = w; g_mi[g_nmi].h = SM_ROWH;
    g_mi[g_nmi].act = it->act; g_mi[g_nmi].is_sub = it->sub ? 1 : 0;
    g_mi[g_nmi].level = level; g_mi[g_nmi].idx = idx; g_nmi++;
    return SM_ROWH;
}
static void draw_startmenu_xp(void) {
    int ty = (int)fb_height() - TB_H;
    int npin = (int)(sizeof SM_xp_pin / sizeof SM_xp_pin[0]);
    int nsys = (int)(sizeof SM_xp_sys / sizeof SM_xp_sys[0]);
    int aero = (g_theme->era == ERA_AERO);
    int toph = 54, both = 42;
    int leftrows = npin * SM_ROWH + 10 + SM_ROWH + (aero ? 26 : 0);
    int rightrows = nsys * SM_ROWH;
    int paneh = (leftrows > rightrows ? leftrows : rightrows) + 8;
    int mw = 408, lw = 214, rw = mw - lw;
    int mh = toph + paneh + both, mx = 2, my = ty - mh;
    fb_fill_rect(mx, my, mw, mh, g_theme->window_bg); wm_bevel_raised(mx, my, mw, mh);
    /* top user band */
    vgrad(mx + 2, my + 2, mw - 4, toph, g_theme->title_lo, g_theme->title_hi);
    fb_fill_rect(mx + 10, my + 9, 36, 36, C_WHITE); wm_bevel_sunken(mx + 10, my + 9, 36, 36);
    panes(mx + 19, my + 18, 18);
    fb_text_bold_t(mx + 54, my + 20, "Debian95 User", g_theme->title_text);
    /* two panes */
    int py = my + toph;
    fb_fill_rect(mx + 2, py, lw - 2, paneh, g_theme->window_bg);
    fb_fill_rect(mx + lw, py, rw - 2, paneh, g_theme->face);
    for (int j = 0; j < paneh; j++) fb_fill_rect(mx + lw - 1, py + j, 1, 1, g_theme->shadow);
    g_nmi = 0;
    int iy = py + 4; for (int i = 0; i < npin; i++) iy += xp_row(mx + 4, iy, lw - 8, &SM_xp_pin[i], 2, i);
    hline(mx + 8, iy + 4, lw - 16, g_theme->shadow); hline(mx + 8, iy + 5, lw - 16, g_theme->light); iy += 10;
    int apy = iy;
    { SMItem ap = { "All Programs", 0, SM_prog, g_nsm_prog, IC_FOLDER, 0 };
      xp_row(mx + 4, iy, lw - 8, &ap, 0, 0); }
    if (aero) { int sy2 = py + paneh - 22; fb_fill_rect(mx + 6, sy2, lw - 12, 18, C_WHITE);
        wm_bevel_sunken(mx + 6, sy2, lw - 12, 18);
        fb_text_t(mx + 11, sy2 + (18 - (int)font_height()) / 2, "Search programs and files", g_theme->gray_text); }
    int ry = py + 6; for (int i = 0; i < nsys; i++) ry += xp_row(mx + lw + 4, ry, rw - 8, &SM_xp_sys[i], 1, i);
    /* bottom band: Log Off / Turn Off */
    int by = my + mh - both; vgrad(mx + 2, by, mw - 4, both - 2, g_theme->title_hi, g_theme->title_lo);
    struct { const char *l; int act; u32 c; } bb[2] = { { "Log Off", 52, RGB(0xE0,0xA0,0x20) }, { "Turn Off Computer", 2, RGB(0xC0,0x30,0x20) } };
    int bx = mx + mw - 4;
    for (int i = 1; i >= 0; i--) {
        int bw2 = (int)font_measure(bb[i].l) + 38; bx -= bw2 + 4;
        int hov = g_cx >= bx && g_cx < bx + bw2 && g_cy >= by + 4 && g_cy < by + both - 4;
        if (hov) fb_fill_rect(bx, by + 4, bw2, both - 8, g_theme->sel);
        fb_fill_rect(bx + 8, by + (both - 14) / 2, 13, 13, bb[i].c); wm_bevel_raised(bx + 8, by + (both - 14) / 2, 13, 13);
        fb_text_t(bx + 26, by + (both - (int)font_height()) / 2, bb[i].l, hov ? C_WHITE : g_theme->title_text);
        g_mi[g_nmi].x = bx; g_mi[g_nmi].y = by; g_mi[g_nmi].w = bw2; g_mi[g_nmi].h = both;
        g_mi[g_nmi].act = bb[i].act; g_mi[g_nmi].is_sub = 0; g_mi[g_nmi].level = 2; g_mi[g_nmi].idx = 50 + i; g_nmi++;
    }
    if (g_sm_path[0] == 0) draw_flyout(SM_prog, g_nsm_prog, mx + lw - 2, apy - 3, 1);
}

static void draw_startmenu(void) {
    g_nmi = 0;
    if (!g_start_open) return;
    if (g_theme->era == ERA_XP || g_theme->era == ERA_AERO) { draw_startmenu_xp(); return; }
    mru_rebuild();                               /* Documents reflects the real MRU */
    int ty = (int)fb_height() - TB_H, mw = 190, bw = 33;
    int mh = sm_height(SM_main, NSM_MAIN) + 8;
    int mx = 2, my = ty - mh;
    fb_fill_rect(mx, my, mw, mh, C_FACE);
    wm_bevel_raised(mx, my, mw, mh);
    for (int j = 0; j < mh - 4; j++)                     /* dark-blue vertical gradient */
        fb_fill_rect(mx + 2, my + 2 + j, bw, 1, RGB(0x08, 0x08, 0x30 + (0x60 * (mh - j)) / mh));
    sidebar_text(mx + 8, my + mh - 26, "Debian95", RGB(0x2A, 0x2A, 0x60));   /* shadow */
    sidebar_text(mx + 7, my + mh - 27, "Debian95", RGB(0xC0, 0xC0, 0xC0));   /* silver */
    int iy = my + 6;
    for (int i = 0; i < NSM_MAIN; i++) {
        int rh = sm_row(mx + bw + 2, iy, mw - bw - 6, &SM_main[i], 0, i);
        if (SM_main[i].sub && g_sm_path[0] == i)
            draw_flyout(SM_main[i].sub, SM_main[i].nsub, mx + mw - 2, iy - 3, 1);
        iy += rh;
    }
}

int wm_take_action(void) { int a = g_action; g_action = 0; return a; }
void wm_post_action(int a) { g_action = a; }   /* dialogs queue an action for the platform to dispatch */
/* global keyboard shortcuts (platform maps Ctrl+Esc / Alt+F4 to these) */
void wm_toggle_start(void) { g_start_open = !g_start_open; g_sm_path[0] = g_sm_path[1] = g_sm_path[2] = -1; }
void wm_debug_open_programs(void) { g_start_open = 1; g_sm_path[0] = 0; g_sm_path[1] = -1; g_sm_path[2] = -1; }  /* test: Start > Programs */
void wm_close_active(void) { struct Window *w = front_visible(); if (w) wm_destroy(w); }   /* Alt+F4 */

/* ---- Alt+Tab task switcher (the Win95 "cool switch" box) ---- */
static int g_altswitch, g_altsel, g_altn;
static struct Window *g_altlist[16];
void wm_alt_tab(void) {                          /* Tab while Alt is held */
    if (!g_altswitch) {
        g_altn = 0;
        for (int i = g_nz - 1; i >= 0 && g_altn < 16; i--) if ((g_z[i]->visible || g_z[i]->minimized) && !g_z[i]->external) g_altlist[g_altn++] = g_z[i];
        if (g_altn == 0) return;
        g_altswitch = 1; g_altsel = g_altn > 1 ? 1 : 0;   /* highlight the next window */
    } else g_altsel = (g_altsel + 1) % g_altn;
}
int  wm_alt_active(void) { return g_altswitch; }
void wm_alt_commit(void) {                       /* Alt released: raise the highlighted window */
    if (g_altswitch && g_altsel < g_altn) { struct Window *w = g_altlist[g_altsel]; w->visible = 1; w->minimized = 0; bring_front(w); }
    g_altswitch = 0;
}
static void draw_altswitch(void) {
    if (!g_altswitch || g_altn == 0) return;
    int cell = 40, pad = 14, bw = pad * 2 + g_altn * cell, bh = pad * 2 + cell + 18;
    if (bw > (int)fb_width() - 8) bw = (int)fb_width() - 8;
    int bx = ((int)fb_width() - bw) / 2, by = ((int)fb_height() - bh) / 2;
    fb_fill_rect(bx, by, bw, bh, C_FACE); wm_bevel_raised(bx, by, bw, bh);
    for (int i = 0; i < g_altn; i++) {
        int x = bx + pad + i * cell, y = by + pad;
        if (i == g_altsel) { fb_fill_rect(x - 3, y - 3, cell - 2, cell - 2, C_SEL); }
        win_icon(x + (cell - 18) / 2, y + (cell - 22) / 2, g_altlist[i]->icon_mod, g_altlist[i]->icon);
    }
    const char *t = g_altlist[g_altsel]->title;
    int tw = (int)font_measure(t); if (tw > bw - 8) tw = bw - 8;
    fb_text_t(bx + (bw - tw) / 2, by + pad + cell + 2, t, C_DK);
}

/* ---- desktop icons (real SHELL32.DLL RT_ICONs, blitted at 32x32) ---- */
static struct { int x, y, w, h, act, uidx; char key[40]; } g_icon[32];
static int g_nicon;
static int g_sel_icon = -1;                 /* selected icon (Win95 single-click selection) */
static unsigned g_iselmask;                 /* marquee multi-selection (bit i = g_icon[i]) */
static int g_marq_on, g_marq_x0, g_marq_y0; /* desktop rubber-band origin */
static int g_marq_rx, g_marq_ry, g_marq_rw, g_marq_rh;  /* its current rect (for compose) */

/* draggable icons: per-icon saved positions (Win95 free desktop layout), keyed
 * by the icon's label/name and persisted in SETTINGS.INI ("icons" = name|x,y;...) */
static struct { char key[40]; int x, y; } g_iover[24]; static int g_niover, g_iover_loaded;
static int  ipos_find(const char *k) { for (int i = 0; i < g_niover; i++) { int eq = 1; for (int j = 0;; j++) { if (g_iover[i].key[j] != k[j]) { eq = 0; break; } if (!k[j]) break; } if (eq) return i; } return -1; }
static void ipos_load(void) {
    if (g_iover_loaded) return; g_iover_loaded = 1; g_niover = 0;
    char v[512]; if (sys_get_setting("icons", v, sizeof v) != 0 || !v[0]) return;
    int i = 0;
    while (v[i] && g_niover < 24) {
        char *k = g_iover[g_niover].key; int n = 0;
        while (v[i] && v[i] != '|' && n < 39) k[n++] = v[i++]; k[n] = 0;
        if (v[i] != '|') break; i++;
        int x = 0, sx = 1; if (v[i] == '-') { sx = -1; i++; } while (v[i] >= '0' && v[i] <= '9') x = x * 10 + (v[i++] - '0');
        if (v[i] == ',') i++; int y = 0, sy = 1; if (v[i] == '-') { sy = -1; i++; } while (v[i] >= '0' && v[i] <= '9') y = y * 10 + (v[i++] - '0');
        g_iover[g_niover].x = x * sx; g_iover[g_niover].y = y * sy; g_niover++;
        if (v[i] == ';') i++;
    }
}
static void ipos_save(void) {
    char v[512]; int p = 0;
    for (int i = 0; i < g_niover && p < 460; i++) {
        for (int j = 0; g_iover[i].key[j] && p < 460; j++) v[p++] = g_iover[i].key[j];
        v[p++] = '|';
        char b[12]; int n = 0, x = g_iover[i].x; if (x < 0) { v[p++] = '-'; x = -x; } if (x == 0) b[n++] = '0'; while (x) { b[n++] = (char)('0' + x % 10); x /= 10; } while (n) v[p++] = b[--n];
        v[p++] = ','; n = 0; int y = g_iover[i].y; if (y < 0) { v[p++] = '-'; y = -y; } if (y == 0) b[n++] = '0'; while (y) { b[n++] = (char)('0' + y % 10); y /= 10; } while (n) v[p++] = b[--n];
        v[p++] = ';';
    }
    v[p] = 0; sys_set_setting("icons", v);
}
static void ipos_set(const char *k, int x, int y) {
    int i = ipos_find(k);
    if (i < 0) { if (g_niover >= 24) return; i = g_niover++; int j = 0; for (; k[j] && j < 39; j++) g_iover[i].key[j] = k[j]; g_iover[i].key[j] = 0; }
    g_iover[i].x = x; g_iover[i].y = y; ipos_save();
}
static void ipos_clear(void) { g_niover = 0; sys_set_setting("icons", ""); }   /* Line up Icons */

/* user items living in the real "Desktop" folder (like C:\WINDOWS\Desktop). New
 * documents/folders are created HERE, not at the volume root. */
#define MAXUSER 20
static char g_uname[MAXUSER][40]; static int g_uisdir[MAXUSER]; static int g_nuser;
static void desk_enum(void) {
    FsEntry e[MAXUSER]; int n = FsListDir("Desktop", e, MAXUSER);
    g_nuser = 0;
    for (int i = 0; i < n && g_nuser < MAXUSER; i++) {
        int k = 0; for (; e[i].name[k] && k < 39; k++) g_uname[g_nuser][k] = e[i].name[k];
        g_uname[g_nuser][k] = 0; g_uisdir[g_nuser] = e[i].is_dir; g_nuser++;
    }
}

/* Win95 desktop icon grid: SM_CXICONSPACING/SM_CYICONSPACING = 75x75 px cells,
 * column-major (LVS_ALIGNLEFT) - fill the left column top-to-bottom, then wrap to
 * the next column right. The 32px icon is centred in the cell: our per-icon anchor
 * puts the icon at anchor+8..+40 (centre anchor+24), so anchor left ~= (75-48)/2. */
#define ICON_CELL_W 75
#define ICON_CELL_H 75
#define ICON_LEFT   13
#define ICON_TOP    10
/* one centred label line (highlighted when selected) */
static void icon_label_line(int cx, int y, const char *s, int sel, int fh) {
    int tw = (int)font_measure(s), lx = cx - tw / 2;
    if (sel) {
        fb_fill_rect(lx - 2, y - 1, tw + 4, fh + 2, C_SEL);
        fb_text_t(lx, y, s, C_WHITE);
    } else {
        fb_text_t(lx + 1, y + 1, s, RGB(0x00, 0x20, 0x28));      /* shadow */
        fb_text_t(lx, y, s, C_WHITE);
    }
}
/* Win95 icon label: centred under the icon and, when wider than the cell, wrapped
 * to a second line at a word break (the last line ellipsis-clipped to the cell
 * width) - never spilling past the 75px cell the way a single long line would. */
static void icon_label(int ix, int iy, const char *s, int sel) {
    int cx = ix + 24, fh = (int)font_height(), ly = iy + 32;
    int budget = ICON_CELL_W - 6;                               /* text box width ~ cell */
    char l1[48], l2[48]; l1[0] = l2[0] = 0;
    if ((int)font_measure(s) <= budget) {                       /* fits on one line */
        int j = 0; for (; s[j] && j < 47; j++) l1[j] = s[j]; l1[j] = 0;
    } else {                                                    /* greedy 2-line word wrap */
        int n = 0; while (s[n]) n++;
        int split = -1, j = 0; char t[48];
        for (int i = 0; i < n && j < 47; i++) {                 /* longest prefix ending at a space that fits */
            if (s[i] == ' ') { t[j] = 0; if ((int)font_measure(t) <= budget) split = i; }
            t[j++] = s[i];
        }
        if (split <= 0) {                                       /* no usable break -> hard split */
            int k = 0; for (; k < n && k < 47; k++) { l1[k] = s[k]; l1[k + 1] = 0;
                if ((int)font_measure(l1) > budget) { l1[k] = 0; break; } }
            split = k - 1;
        } else { int k = 0; for (; k < split && k < 47; k++) l1[k] = s[k]; l1[k] = 0; }
        int m = 0; for (int i = split + 1; s[i] && m < 47; i++) l2[m++] = s[i]; l2[m] = 0;
        while (l2[0] && (int)font_measure(l2) > budget) {       /* ellipsis-clip the 2nd line */
            int e = 0; while (l2[e]) e++; if (e < 4) break;
            l2[e - 4] = '.'; l2[e - 3] = '.'; l2[e - 2] = '.'; l2[e - 1] = 0;
        }
    }
    icon_label_line(cx, ly, l1, sel, fh);
    if (l2[0]) icon_label_line(cx, ly + fh + 1, l2, sel, fh);
}

/* one 32px shortcut: the real icon if SHELL32 has loaded, else a labelled box.
 * The selected icon gets a 50% blue wash, like the Win95 desktop. */
static void icon_cell(int ix, int iy, const u32 *px, int ok, const char *label, u32 fallback, int act) {
    int oi = ipos_find(label); if (oi >= 0) { ix = g_iover[oi].x; iy = g_iover[oi].y; }   /* saved position */
    int sel = (g_nicon == g_sel_icon) || (g_iselmask & (1u << g_nicon));
    if (ok) {
        blit_key(ix + 8, iy, 32, 32, px);
        if (sel)
            for (int j = 0; j < 32; j++)
                for (int i = 0; i < 32; i++) {
                    u32 c = px[j * 32 + i];
                    if (c != ICON_KEY)
                        fb_pixel(ix + 8 + i, iy + j,
                                 (((c >> 1) & 0x7F7F7F) + ((C_SEL >> 1) & 0x7F7F7F)));
                }
    } else { fb_fill_rect(ix + 8, iy, 32, 28, fallback); wm_bevel_raised(ix + 8, iy, 32, 28);
             fb_char_t(ix + 20, iy + 10, label[0], RGB(0, 0, 0)); }
    icon_label(ix, iy, label, sel);
    g_icon[g_nicon].x = ix; g_icon[g_nicon].y = iy; g_icon[g_nicon].w = 56; g_icon[g_nicon].h = 58;
    g_icon[g_nicon].act = act; g_icon[g_nicon].uidx = -1;
    { int j = 0; for (; label[j] && j < 39; j++) g_icon[g_nicon].key[j] = label[j]; g_icon[g_nicon].key[j] = 0; }
    g_nicon++;
}

/* a user desktop item: real folder/document icon + label, with inline rename */
static const char *uext(const char *n) { const char *d = 0; for (const char *p = n; *p; p++) if (*p == '.') d = p; return d ? d + 1 : ""; }
static int ends_ci(const char *e, const char *w) { int a=0,b=0; while(e[a])a++; while(w[b])b++; if(b>a)return 0; for(int i=0;i<b;i++){char x=e[a-b+i],y=w[i]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0;} return 1; }
static void user_cell(int ix, int iy, int uidx) {
    const char *nm = g_uname[uidx];
    int oi = ipos_find(nm); if (oi >= 0) { ix = g_iover[oi].x; iy = g_iover[oi].y; }   /* saved position */
    int sel = (g_nicon == g_sel_icon) || (g_iselmask & (1u << g_nicon));
    const u32 *px = 0; int ok = 0;
    if (g_uisdir[uidx])                 { px = g_di[0]; ok = g_di_ok[0]; }        /* folder */
    else if (ends_ci(nm, ".bmp"))       { px = g_ic_bmp; ok = g_ic_bmp_ok; }      /* bitmap document */
    else                                { px = g_di[1]; ok = g_di_ok[1]; }        /* text/other document */
    if (ok) {
        blit_key(ix + 8, iy, 32, 32, px);
        if (sel) for (int j = 0; j < 32; j++) for (int i = 0; i < 32; i++) { u32 c = px[j*32+i];
            if (c != ICON_KEY) fb_pixel(ix+8+i, iy+j, (((c>>1)&0x7F7F7F)+((C_SEL>>1)&0x7F7F7F))); }
    } else { fb_fill_rect(ix + 8, iy, 32, 28, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(ix + 8, iy, 32, 28); }
    if (ends_ci(nm, ".lnk")) {                          /* shortcut arrow badge (lower-left) */
        int ax = ix + 6, ay = iy + 20;
        fb_fill_rect(ax, ay, 11, 11, C_WHITE); fb_rect(ax, ay, 11, 11, C_DK);
        for (int i = 0; i < 6; i++) fb_pixel(ax + 3, ay + 8 - i, C_DK);   /* shaft up */
        for (int i = 0; i < 5; i++) fb_pixel(ax + 3 + i, ay + 3, C_DK);   /* shaft right */
        fb_pixel(ax + 6, ay + 1, C_DK); fb_pixel(ax + 7, ay + 2, C_DK); fb_pixel(ax + 7, ay + 3, C_DK);  /* head */
    }
    if (g_ren_active && g_ren_uidx == uidx) {           /* inline edit box */
        int tw = (int)font_measure(g_ren_buf); if (tw < 40) tw = 40;
        int lx = ix + 24 - tw / 2, ly = iy + 32;
        fb_fill_rect(lx - 2, ly - 1, tw + 4, (int)font_height() + 2, C_WHITE);
        fb_rect(lx - 3, ly - 2, tw + 6, (int)font_height() + 4, C_DK);
        fb_text_t(lx, ly, g_ren_buf, C_DK);
        if (wm_caret_on()) fb_fill_rect(lx + tw, ly, 1, (int)font_height(), C_DK);   /* blinking caret */
    } else icon_label(ix, iy, nm, sel);
    g_icon[g_nicon].x = ix; g_icon[g_nicon].y = iy; g_icon[g_nicon].w = 56; g_icon[g_nicon].h = 58;
    g_icon[g_nicon].act = 0; g_icon[g_nicon].uidx = uidx;
    { int j = 0; for (; nm[j] && j < 39; j++) g_icon[g_nicon].key[j] = nm[j]; g_icon[g_nicon].key[j] = 0; }
    g_nicon++;
}

/* progressive desktop populate: after login the shell reveals icons as the
 * KERNEL releases them (debian95.ko /proc/debian95/boot_ready), taskbar first.
 * The pace is owned by the kernel module - rmmod it and the desktop populates
 * instantly, so the "loading" is a real, removable kernel behaviour, not a
 * userspace timer. */
static int g_booting, g_reveal, g_icon_total;
void wm_boot_start(void) { g_booting = 1; g_reveal = 0; g_icon_total = 99; }
int  wm_boot_tick(void) {
    if (!g_booting) return 0;
    g_reveal = sys_boot_ready();                  /* how many the kernel has released */
    if (g_reveal >= g_icon_total) g_booting = 0;
    return 1;                                      /* keep composing while populating */
}

/* external programs registered by the platform layer (downloaded apps like VLC /
 * Chromium on the Debian edition): they get a real desktop icon + Programs entry,
 * launched through the normal action dispatch (app_launch). Empty on the Appliance
 * edition, which registers none. The icon is a 32x32 keyed buffer the platform
 * decoded (0 = draw a fallback tile). */
typedef struct { const char *name; int act; const u32 *icon; int desktop; } ExtProg;
static ExtProg g_ext[64]; static int g_next_ext;
void wm_register_program_cat(const char *name, int act, const u32 *icon32, int desktop, int cat) {
    if (g_next_ext >= 64 || !name) return;
    g_ext[g_next_ext].name = name; g_ext[g_next_ext].act = act;
    g_ext[g_next_ext].icon = icon32; g_ext[g_next_ext].desktop = desktop; g_next_ext++;
    if (cat >= 0 && cat < APP_CATS) {                     /* grouped into a category folder */
        if (g_catn[cat] < 24) {
            SMItem *it = &g_catitems[cat][g_catn[cat]++];
            it->label = name; it->act = act; it->sub = 0; it->nsub = 0; it->icon = IC_APP; it->imod = 0;
        }
        if (!g_cat_folder[cat] && g_nsm_prog < (int)(sizeof(SM_prog) / sizeof(SM_prog[0]))) {
            g_cat_folder[cat] = g_nsm_prog;              /* create the category folder once */
            SM_prog[g_nsm_prog].label = APP_CAT_NAME[cat]; SM_prog[g_nsm_prog].act = 0;
            SM_prog[g_nsm_prog].sub = g_catitems[cat]; SM_prog[g_nsm_prog].nsub = 0;
            SM_prog[g_nsm_prog].icon = IC_FOLDER; SM_prog[g_nsm_prog].imod = 0;
            g_nsm_prog++; SM_main[0].nsub = g_nsm_prog;
        }
        if (g_cat_folder[cat]) SM_prog[g_cat_folder[cat]].nsub = g_catn[cat];
        return;
    }
    if (g_nsm_prog < (int)(sizeof(SM_prog) / sizeof(SM_prog[0]))) {   /* flat at top level */
        SM_prog[g_nsm_prog].label = name; SM_prog[g_nsm_prog].act = act;
        SM_prog[g_nsm_prog].sub = 0; SM_prog[g_nsm_prog].nsub = 0;
        SM_prog[g_nsm_prog].icon = IC_APP; SM_prog[g_nsm_prog].imod = 0;
        g_nsm_prog++; SM_main[0].nsub = g_nsm_prog;
    }
}
void wm_register_program(const char *name, int act, const u32 *icon32, int desktop) {
    wm_register_program_cat(name, act, icon32, desktop, -1);
}
int  wm_ext_prog_count(void) { return g_next_ext; }
const char *wm_ext_prog_name(int i) { return (i >= 0 && i < g_next_ext) ? g_ext[i].name : 0; }
int  wm_ext_prog_act(int i)  { return (i >= 0 && i < g_next_ext) ? g_ext[i].act : 0; }

static void draw_icons(void) {
    /* My Computer / Network Neighborhood / Recycle Bin — real Win95 shell icons */
    static const char *sysl[3] = { "My Computer", "Network Neighborhood", "Recycle Bin" };
    static const int   sysa[3] = { 18, 66, 17 };       /* My Computer view / Network Neighborhood / Recycle Bin */
    static const u32   sysc[3] = { RGB(0xC0,0xC0,0xB0), RGB(0x60,0x80,0xC0), RGB(0xB8,0xB8,0xB8) };
    ipos_load();
    g_nicon = 0;
    int bottom = (int)fb_height() - TB_H - ICON_CELL_H;   /* work area minus taskbar */
    int ix = ICON_LEFT, iy = ICON_TOP, shown = 0;
    /* advance to the next grid slot, wrapping to a new column at the taskbar */
    #define ICON_STEP() do { iy += ICON_CELL_H; if (iy > bottom) { iy = ICON_TOP; ix += ICON_CELL_W; } } while (0)
    FsEntry rb[2]; int recycle_full = FsListDir("Recycled", rb, 2) > 0;   /* real bin state */
    for (int i = 0; i < 3; i++) {
        const u32 *px = g_sys[i]; int ok = g_sys_ok[i];
        if (i == 2 && recycle_full && g_recycle_full_ok) { px = g_recycle_full; ok = 1; }  /* full bin icon */
        if (!g_booting || shown < g_reveal) icon_cell(ix, iy, px, ok, sysl[i], sysc[i], sysa[i]); shown++; ICON_STEP();
    }
    /* app shortcuts */
    static const char *labels[3] = { "Files", "Editor", "Paint" };
    static const int   acts[3]   = { 3, 4, 5 };
    static const u32   col[3]     = { RGB(0xF0,0xC0,0x30), RGB(0xF0,0xF0,0xF0), RGB(0xC0,0x40,0xF0) };
    for (int i = 0; i < 3; i++) { if (!g_booting || shown < g_reveal) icon_cell(ix, iy, g_di[i], g_di_ok[i], labels[i], col[i], acts[i]); shown++; ICON_STEP(); }
    /* downloaded apps registered by the platform layer (VLC, Chromium, ...); an app
     * with no staged artwork falls back to the shell's folder icon, not a letter tile */
    for (int i = 0; i < g_next_ext; i++) if (g_ext[i].desktop) {
        const u32 *ic = g_ext[i].icon ? g_ext[i].icon : (g_di_ok[0] ? g_di[0] : 0);
        if (!g_booting || shown < g_reveal)
            icon_cell(ix, iy, ic, ic ? 1 : 0, g_ext[i].name, RGB(0x40, 0x80, 0xF0), g_ext[i].act);
        shown++; ICON_STEP();
    }
    /* user items from the real Desktop folder, continuing the grid (wrap to a
     * new column when the current one reaches the taskbar) */
    desk_enum();
    if (g_ren_pending[0]) {                       /* a freshly-created item -> enter rename */
        for (int u = 0; u < g_nuser; u++) { int eq = 1; for (int k = 0;; k++) {
            if (g_uname[u][k] != g_ren_pending[k]) { eq = 0; break; } if (!g_uname[u][k]) break; }
            if (eq) { g_ren_active = 1; g_ren_uidx = u; dk_set(g_ren_buf, g_uname[u]);
                      g_ren_len = (int)strlen(g_ren_buf); g_sel_icon = 6 + u; break; } }
        g_ren_pending[0] = 0;
    }
    for (int u = 0; u < g_nuser; u++) {
        if (!g_booting || shown < g_reveal) user_cell(ix, iy, u);
        shown++; ICON_STEP();
    }
    g_icon_total = shown;                        /* total icons, for the reveal to finish on */
    #undef ICON_STEP
}

/* ---- taskbar (with a button per window) ---- */
static struct { int x, y, w, h; struct Window *win; } g_tbtn[16];
static int g_ntbtn;
/* tray volume control */
static int g_vol_open, g_vol = 66, g_vol_mute, g_spk_x, g_spk_y;
static int g_clk_x, g_clk_y, g_clk_w;        /* tray clock rect (double-click -> Date/Time) */
static int g_peek_x;                          /* Win7 "Show Desktop" peek corner (0 = none) */
static int g_lkbtn_x[MAXLINK+1], g_lkbtn_w[MAXLINK+1], g_lkbtn_act[MAXLINK+1], g_nlkbtn;  /* Links band buttons (act -1 = Edit...) */
static int g_lkbtn_y, g_lkbtn_h;

/* ---- Safely Remove Hardware (notification area): a tray icon that appears when a
 * removable drive is mounted; click it for a per-drive Eject menu. The platform's
 * periodic disk poll refreshes g_has_removable via wm_set_removable(). ---- */
static int g_has_removable, g_sru_x, g_sru_y;
struct lg_disk { char mount[128]; char label[32]; char size[16]; int removable; };
int  sys_disk_list(struct lg_disk *out, int max);
int  sys_disk_eject(const char *mount);
void wm_set_removable(int on) { g_has_removable = on; }
static struct lg_disk g_sru_list[8]; static int g_sru_n;
static char g_sru_lbl[8][56];
/* ---- XEMBED system tray: debian95-wm docks real Linux tray icons; the shell
 * reserves g_tray_slots slots in the notification area (blank - the X icon
 * windows float there) and publishes that strip's screen rect for the WM. ---- */
static int g_tray_slots, g_trayslot_x, g_trayslot_y, g_trayslot_w, g_trayslot_h;
void wm_set_tray_slots(int n) { if (n < 0) n = 0; if (n > 16) n = 16; g_tray_slots = n; }
int  wm_get_tray_strip(int *x, int *y, int *w, int *h) {
    if (g_trayslot_w <= 0) return 0;
    *x = g_trayslot_x; *y = g_trayslot_y; *w = g_trayslot_w; *h = g_trayslot_h; return 1;
}
static void sru_draw(int x, int y) {         /* a drive with a green "eject/remove" arrow */
    fb_fill_rect(x + 1, y + 7, 14, 7, RGB(0x90,0x90,0x90)); wm_bevel_raised(x + 1, y + 7, 14, 7);  /* device */
    fb_fill_rect(x + 3, y + 9, 2, 2, RGB(0x30,0xC0,0x30));                                          /* green LED */
    u32 g = RGB(0x00,0xA0,0x00);                                                                    /* up arrow */
    for (int r = 0; r < 4; r++) fb_fill_rect(x + 8 - r, y + 1 + r, 2*r + 1, 1, g);
    fb_fill_rect(x + 7, y + 4, 3, 2, g);
}
static void sru_eject_cb(int act) {
    if (act < 0 || act >= g_sru_n) return;
    int rc = sys_disk_eject(g_sru_list[act].mount);
    wm_messagebox("Safely Remove Hardware",
        rc == 0 ? "The drive can now be safely removed." : "The drive is in use and could not be ejected.");
}
static int sru_click(int x, int y) {
    if (!g_has_removable || !g_sru_x) return 0;
    if (x < g_sru_x || x >= g_sru_x + 16 || y < g_sru_y || y >= g_sru_y + 16) return 0;
    struct lg_disk d[8]; int n = sys_disk_list(d, 8); g_sru_n = 0;
    static const char *items[8]; static int acts[8];
    for (int i = 0; i < n && g_sru_n < 8; i++) {
        if (!d[i].removable) continue;
        g_sru_list[g_sru_n] = d[i];
        const char *nm = d[i].label[0] ? d[i].label : d[i].mount; int k = 0;
        const char *pre = "Eject "; for (; pre[k]; k++) g_sru_lbl[g_sru_n][k] = pre[k];
        for (int j = 0; nm[j] && k < 54; j++) g_sru_lbl[g_sru_n][k++] = nm[j];
        g_sru_lbl[g_sru_n][k] = 0;
        items[g_sru_n] = g_sru_lbl[g_sru_n]; acts[g_sru_n] = g_sru_n; g_sru_n++;
    }
    if (g_sru_n == 0) return 0;
    wm_context_menu(items, acts, g_sru_n, sru_eject_cb);
    return 1;
}
static u32 g_dclick_ms = 400;                 /* double-click threshold (Mouse applet) */
/* Win95 rubber-band: a dotted XOR outline while moving/resizing a window
 * (contents are not shown until the drag is released) */
static int g_band_on, g_bx, g_by, g_bw, g_bh;

/* vertical gradient (or solid fill when lo==hi) - used for themed taskbar/Start */
static void vgrad(int x, int y, int w, int h, u32 lo, u32 hi) {
    if (lo == hi) { fb_fill_rect(x, y, w, h, lo); return; }
    for (int j = 0; j < h; j++) {
        int t = h > 1 ? (j * 255) / (h - 1) : 0;
        int r = (((lo >> 16) & 0xff) * (255 - t) + ((hi >> 16) & 0xff) * t) / 255;
        int g = (((lo >> 8) & 0xff) * (255 - t) + ((hi >> 8) & 0xff) * t) / 255;
        int b = ((lo & 0xff) * (255 - t) + (hi & 0xff) * t) / 255;
        fb_fill_rect(x, y + j, w, 1, RGB(r, g, b));
    }
}
/* Quick Launch toolbar (Win95 OSR2/IE4): Show Desktop + launchers next to Start */
static struct { int x, y, w, h, act; const char *tip; } g_ql[4]; static int g_nql;
static unsigned g_sd_saved; static int g_showdesk;
static void ql_show_desktop(void) {                 /* toggle: minimise all, then restore */
    if (!g_showdesk) {
        g_sd_saved = 0;
        for (int i = 0; i < 16; i++) if (g_store[i].alive && g_store[i].visible && !g_store[i].external) {
            g_sd_saved |= 1u << i; g_store[i].visible = 0; g_store[i].minimized = 1; }
        g_showdesk = 1;
    } else {
        for (int i = 0; i < 16; i++) if (g_sd_saved & (1u << i)) { g_store[i].visible = 1; g_store[i].minimized = 0; }
        g_showdesk = 0;
    }
}
/* integer filled circle (no libm) - the Aero Start orb base */
static void fill_circle(int cx, int cy, int r, u32 c) {
    for (int dy = -r; dy <= r; dy++) {
        int w = 0; while ((w + 1) * (w + 1) + dy * dy <= r * r) w++;
        fb_fill_rect(cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}
/* a generic four-pane "window" motif (our own mark, not a product logo) */
static void panes(int x, int y, int s) {
    int h = (s - 1) / 2, g = 1;
    fb_fill_rect(x,         y,         h, h, RGB(0xE0, 0x55, 0x45));
    fb_fill_rect(x + h + g, y,         h, h, RGB(0x4F, 0xB0, 0x52));
    fb_fill_rect(x,         y + h + g, h, h, RGB(0x41, 0x86, 0xE0));
    fb_fill_rect(x + h + g, y + h + g, h, h, RGB(0xF2, 0xC2, 0x34));
}
/* Start button, drawn per UX era: classic flag+"Start", XP green "start" pill,
 * Aero (Vista/7) a glossy round orb. */
static void draw_start_button(int sx, int sy, int pressed) {
    int h = 24, w = start_btn_w(), o = pressed ? 1 : 0;
    if (g_theme->era == ERA_AERO) {                                 /* round glossy orb */
        int r = 13, cx = sx + r, cy = sy + h / 2 + 1;
        fill_circle(cx, cy, r, RGB(0x10, 0x18, 0x28));              /* dark rim */
        fill_circle(cx, cy, r - 1, g_theme->start_hi);
        fill_circle(cx, cy, r - 3, g_theme->start_lo);
        fill_circle(cx, cy - 3, r - 5, RGB(0xBF, 0xD8, 0xF2));      /* top gloss highlight */
        fill_circle(cx, cy, r - 7, g_theme->start_lo);
        panes(cx - 5 + o, cy - 5 + o, 11);
        return;
    }
    if (g_theme->era == ERA_XP) {                                   /* green pill */
        vgrad(sx + 2, sy, w - 2, h, g_theme->start_lo, g_theme->start_hi);
        fb_fill_rect(sx, sy + 3, 2, h - 6, g_theme->start_lo);      /* rounded-ish left */
        hline(sx + 3, sy, w - 4, RGB(0x8C, 0xD8, 0x6C));           /* top gloss */
        hline(sx + 2, sy + h - 1, w - 3, RGB(0x1E, 0x5A, 0x14));
        panes(sx + 6 + o, sy + 6 + o, 12);
        fb_text_bold_t(sx + 6 + 14 + 4 + o, sy + (h - (int)font_height()) / 2 + o, "start", g_theme->start_text);
        return;
    }
    /* classic (95/98/2000/schemes) + flat */
    vgrad(sx, sy, w, h, g_theme->start_lo, g_theme->start_hi);
    if (pressed) wm_bevel_sunken(sx, sy, w, h); else wm_bevel_raised(sx, sy, w, h);
    if (g_flag_ok) blit_key(sx + 4 + o, sy + 4 + o, 16, 16, g_flag);
    else           emblem(sx + 4 + o, sy + 5 + o, 14);
    fb_text_bold_t(sx + 22 + o, sy + (h - (int)font_height()) / 2 + o, "Start", g_theme->start_text);
}

static void draw_taskbar(void) {
    int ty = (int)fb_height() - TB_H;
    vgrad(0, ty, fb_width(), TB_H, g_theme->taskbar_lo, g_theme->taskbar_hi);
    hline(0, ty, fb_width(), C_HILIGHT);

    int aero = (g_theme->era == ERA_AERO);    /* Vista/7: icon-only "superbar", no Quick Launch strip */
    int sx = 4, sy = ty + 3, sbw = start_btn_w();
    draw_start_button(sx, sy, g_start_open);

    /* Quick Launch (classic/XP): a sunken strip of small launchers. Aero pins them
     * as larger icon-only buttons flush on the bar instead. */
    static const struct { int act; u16 id; const char *tip; } QL[] = {
        { -20, 0, "Show Desktop" }, { 3, IC_FOLDER, "Files" }, { 28, IC_APP, "Web Browser" } };
    int qn = (int)(sizeof QL / sizeof QL[0]);
    int qbw = aero ? 40 : 24;
    int qlx = sx + sbw + 6, qlw = qn * qbw + (aero ? 0 : 2);
    if (!aero) { fb_fill_rect(qlx, sy, qlw, 24, C_FACE); wm_bevel_sunken(qlx, sy, qlw, 24); }
    g_nql = 0;
    for (int i = 0; i < qn; i++) {
        int bxq = qlx + (aero ? 0 : 2) + i * qbw;
        int icx = bxq + (aero ? (qbw - 16) / 2 : 2);
        if (aero) { fb_fill_rect(bxq + 1, sy, qbw - 2, 24, C_FACE); wm_bevel_raised(bxq + 1, sy, qbw - 2, 24); }
        if (QL[i].act == -20) {                          /* Show Desktop: a little monitor glyph */
            fb_fill_rect(icx, sy + 4, 15, 11, RGB(0xB8,0xB8,0xB8)); wm_bevel_raised(icx, sy + 4, 15, 11);
            fb_fill_rect(icx + 2, sy + 6, 11, 7, RGB(0x00,0x80,0x80));
            fb_fill_rect(icx + 6, sy + 15, 3, 2, RGB(0x80,0x80,0x80));
        } else win_icon(icx, sy + 4, 0, QL[i].id);
        g_ql[g_nql].x = bxq; g_ql[g_nql].y = sy; g_ql[g_nql].w = qbw - 2; g_ql[g_nql].h = 24;
        g_ql[g_nql].act = QL[i].act; g_ql[g_nql].tip = QL[i].tip;
        g_nql++;
    }

    struct Window *fv = front_visible();
    g_ntbtn = 0;
    int bx = qlx + qlw + 6;
    g_addr_bw = 0;
    if (g_tb_address && !aero) {                      /* Address deskband (classic/XP taskbar) */
        int fh = (int)font_height();
        fb_text_t(bx, sy + (24 - fh) / 2, "Address", C_DK);
        int ebx = bx + (int)font_measure("Address") + 4, eby = sy + 2, ebw = 150;
        fb_fill_rect(ebx, eby, ebw, 20, C_WHITE); wm_bevel_sunken(ebx, eby, ebw, 20);
        int tx = ebx + 4, ty2 = eby + (20 - fh) / 2;
        fb_text_t(tx, ty2, g_addr_buf, C_DK);
        if (g_addr_active) { int cw = (int)font_measure(g_addr_buf); fb_fill_rect(tx + cw + 1, ty2, 1, fh, C_DK); }
        g_addr_bx = ebx; g_addr_by = eby; g_addr_bw = ebw;
        bx = ebx + ebw + 8;
    }
    g_nlkbtn = 0;
    if (g_tb_links && !aero) {                        /* Links deskband (user-populated shortcuts) */
        int fh = (int)font_height();
        fb_text_t(bx, sy + (24 - fh) / 2, "Links", C_DK); bx += (int)font_measure("Links") + 6;
        links_load();
        for (int i = 0; i < g_nlink && g_nlkbtn < MAXLINK; i++) {
            int tw = (int)font_measure(g_lk_name[i]) + 10;
            fb_fill_rect(bx, sy, tw, 24, C_FACE); wm_bevel_raised(bx, sy, tw, 24);
            fb_text_t(bx + 5, sy + (24 - fh) / 2, g_lk_name[i], C_DK);
            g_lkbtn_x[g_nlkbtn] = bx; g_lkbtn_w[g_nlkbtn] = tw; g_lkbtn_act[g_nlkbtn] = i; g_nlkbtn++;
            bx += tw + 2;
        }
        int ew = (int)font_measure("Edit...") + 10;   /* trailing Edit... opens the manage dialog */
        fb_fill_rect(bx, sy, ew, 24, C_FACE); wm_bevel_raised(bx, sy, ew, 24);
        fb_text_t(bx + 5, sy + (24 - fh) / 2, "Edit...", C_DK);
        g_lkbtn_x[g_nlkbtn] = bx; g_lkbtn_w[g_nlkbtn] = ew; g_lkbtn_act[g_nlkbtn] = -1; g_nlkbtn++;
        g_lkbtn_y = sy; g_lkbtn_h = 24;
        bx += ew + 8;
    }
    /* buttons keep a STABLE order (slot/open order) - never shuffle a button to the
     * end just because its window was activated. Aero = icon-only; else text+icon. */
    for (int i = 0; i < 16 && g_ntbtn < 16; i++) {
        struct Window *w = &g_store[i];
        if (!w->alive) continue;
        if (!w->visible && !w->minimized) continue;    /* closed windows leave the taskbar */
        int bw = aero ? 46 : 150;
        /* the active button is sunken: the front window for shell apps, or the
         * X-focused one (published by debian95-wm) for external apps */
        int actv = w->external ? (w->visible && w->xclient == g_active_ext)
                               : (w->visible && w == fv);
        if (aero) {
            fb_fill_rect(bx, sy, bw, 24, C_FACE);
            if (actv) { vgrad(bx, sy, bw, 24, g_theme->light, g_theme->face); wm_bevel_sunken(bx, sy, bw, 24); }
            else      wm_bevel_raised(bx, sy, bw, 24);
            int p = actv ? 1 : 0;
            win_icon(bx + (bw - 16) / 2 + p, sy + 4 + p, w->icon_mod, w->icon);   /* icon only */
        } else {
            fb_fill_rect(bx, sy, bw, 24, C_FACE);
            if (actv) wm_bevel_sunken(bx, sy, bw, 24); else wm_bevel_raised(bx, sy, bw, 24);
            int p = actv ? 1 : 0;
            win_icon(bx + 4 + p, sy + 4 + p, w->icon_mod, w->icon);
            fb_text_t(bx + 24 + p, sy + (24 - (int)font_height()) / 2 + p, w->title, C_DK);
        }
        g_tbtn[g_ntbtn].x = bx; g_tbtn[g_ntbtn].y = sy;
        g_tbtn[g_ntbtn].w = bw; g_tbtn[g_ntbtn].h = 24; g_tbtn[g_ntbtn].win = w;
        g_ntbtn++;
        bx += bw + (aero ? 3 : 4);
    }

    /* system tray (TrayNotifyWnd): status icons + clock in a sunken area whose
     * width is DYNAMIC - it only holds the icons whose drivers are installed
     * (like real Win95: no sound driver -> no speaker, etc.) plus the clock. */
    int hh = 0, mm = 0; rtc_time(&hh, &mm);
    char clk[9]; int p = 0;
    if (g_time24) {                                   /* Regional Settings: 24-hour clock */
        clk[p++] = (char)('0' + hh / 10);
        clk[p++] = (char)('0' + hh % 10);
        clk[p++] = ':';
        clk[p++] = (char)('0' + mm / 10);
        clk[p++] = (char)('0' + mm % 10);
        clk[p] = 0;
    } else {
        int pm = hh >= 12, h12 = hh % 12; if (h12 == 0) h12 = 12;
        if (h12 >= 10) clk[p++] = '1';
        clk[p++] = (char)('0' + h12 % 10);
        clk[p++] = ':';
        clk[p++] = (char)('0' + mm / 10);
        clk[p++] = (char)('0' + mm % 10);
        clk[p++] = ' ';
        clk[p++] = pm ? 'P' : 'A';
        clk[p++] = 'M';
        clk[p] = 0;
    }

    int show_spk = sys_has_audio(), show_net = sys_has_network(), show_sru = g_has_removable;
    int bat_pct = -1, bat_chg = 0, show_bat = (sys_battery(&bat_pct, &bat_chg) == 0);
    int nicons = (show_spk ? 1 : 0) + (show_net ? 1 : 0) + (show_bat ? 1 : 0) + (show_sru ? 1 : 0);
    int clkw = g_show_clock ? (int)font_measure(clk) : 0;       /* Taskbar Properties: Show clock */
    /* Win7 "Show Desktop" peek corner: a thin pane at the far-right end of the
     * superbar that minimises everything on click. Drawn before the tray so it
     * exists even when the tray is empty. */
    g_peek_x = 0;
    if (aero) {
        int pw = 15, px0 = (int)fb_width() - pw;
        vgrad(px0, ty, pw, TB_H, g_theme->taskbar_lo, g_theme->taskbar_hi);
        vline(px0, ty, TB_H, C_SHADOW); vline(px0 + 1, ty, TB_H, C_HILIGHT);   /* separator */
        g_peek_x = px0;
    }
    if (!g_show_clock && !nicons && !g_tray_slots) return;      /* nothing to show -> no tray */
    int tw = (nicons + g_tray_slots) * 20 + clkw + (g_show_clock ? 16 : 8);   /* status + docked tray icons + clock */
    if (nicons + g_tray_slots) tw += 2;
    int tx = (int)fb_width() - tw - 4 - (aero ? 15 : 0);
    fb_fill_rect(tx, sy, tw, 24, C_FACE);
    wm_bevel_sunken(tx, sy, tw, 24);
    int ix = tx + 5, iy = sy + 4; g_spk_x = 0; g_spk_y = sy;
    if (show_spk) {
        g_spk_x = ix - 3;
        if (g_tray_spk_ok) blit_key(ix, iy, 16, 16, g_tray_spk);
        else { fb_fill_rect(ix + 2, iy + 6, 3, 4, C_DK);
            for (int j = 0; j < 5; j++) fb_fill_rect(ix + 5, iy + 8 - j, 1, 2 * j + 1 < 12 ? 2 * j + 1 : 11, C_DK); }
        ix += 20;
    }
    if (show_net) {
        if (g_tray_net_ok) blit_key(ix, iy, 16, 16, g_tray_net);
        else { fb_fill_rect(ix, iy + 1, 6, 5, RGB(0x30,0x30,0x30)); fb_fill_rect(ix + 1, iy + 2, 4, 3, RGB(0x30,0x90,0xF0));
            fb_fill_rect(ix + 5, iy + 5, 6, 5, RGB(0x30,0x30,0x30)); fb_fill_rect(ix + 6, iy + 6, 4, 3, RGB(0x30,0x90,0xF0)); }
        ix += 20;
    }
    if (show_bat) {                                   /* battery gauge (laptops) */
        int bbx = ix + 1, bby = iy + 4;
        fb_rect(bbx, bby, 12, 7, C_DK);
        fb_fill_rect(bbx + 12, bby + 2, 1, 3, C_DK);                        /* terminal nub */
        int fillw = bat_pct * 10 / 100; if (fillw < 1 && bat_pct > 0) fillw = 1; if (fillw > 10) fillw = 10;
        u32 col = bat_chg ? RGB(0x00,0xA0,0x00) : (bat_pct <= 15 ? RGB(0xC0,0x00,0x00) : RGB(0x00,0x80,0x00));
        fb_fill_rect(bbx + 1, bby + 1, fillw, 5, col);
        ix += 20;
    }
    g_sru_x = 0;
    if (show_sru) { g_sru_x = ix; g_sru_y = iy; sru_draw(ix, iy); ix += 20; }
    g_trayslot_w = 0;
    if (g_tray_slots > 0) {                                     /* reserved strip for debian95-wm's docked XEMBED icons */
        g_trayslot_x = ix; g_trayslot_y = iy; g_trayslot_w = g_tray_slots * 20; g_trayslot_h = 16;
        ix += g_tray_slots * 20;
    }
    if (g_show_clock) { fb_text_t(tx + tw - clkw - 8, sy + (24 - (int)font_height()) / 2, clk, C_DK);
        g_clk_x = tx + tw - clkw - 10; g_clk_y = sy; g_clk_w = clkw + 12; }
    else { g_clk_w = 0; }
}

/* ---- hover tooltips (Win95 info tips): a pale-yellow box after ~0.5s of dwell ---- */
static const char *tooltip_at(int x, int y) {
    static char buf[48];
    int ty = (int)fb_height() - TB_H;
    if (y < ty) return 0;                             /* taskbar area only */
    if (start_hit(x, y)) return "Click here to begin.";
    for (int i = 0; i < g_nql; i++)
        if (x >= g_ql[i].x && x < g_ql[i].x + g_ql[i].w && y >= g_ql[i].y && y < g_ql[i].y + g_ql[i].h) return g_ql[i].tip;
    for (int i = 0; i < g_ntbtn; i++)
        if (g_tbtn[i].win && x >= g_tbtn[i].x && x < g_tbtn[i].x + g_tbtn[i].w && y >= g_tbtn[i].y && y < g_tbtn[i].y + g_tbtn[i].h)
            return g_tbtn[i].win->title;
    if (g_spk_x && x >= g_spk_x && x < g_spk_x + 16 && y >= g_spk_y && y < g_spk_y + 16) {
        int v = g_vol_mute ? 0 : g_vol, n = 0; const char *p = "Volume: "; while (*p) buf[n++] = *p++;
        char t[4]; int k = 0; if (!v) t[k++] = '0'; while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
        while (k) buf[n++] = t[--k]; buf[n++] = '%'; buf[n] = 0; return buf;
    }
    if (g_has_removable && g_sru_x && x >= g_sru_x && x < g_sru_x + 16 && y >= g_sru_y && y < g_sru_y + 16) return "Safely Remove Hardware";
    if (g_show_clock && g_clk_w > 0 && x >= g_clk_x && x < g_clk_x + g_clk_w) return "Double-click to set date/time.";
    return 0;
}
static int g_tt_lx = -9999, g_tt_ly = -9999, g_tt_show; static u32 g_tt_t0; static char g_tt_buf[48];
int wm_hover_tick(void) {
    int dx = g_cx - g_tt_lx, dy = g_cy - g_tt_ly;
    if (dx*dx + dy*dy > 4) {                           /* moved: reset dwell, hide any tip */
        g_tt_lx = g_cx; g_tt_ly = g_cy; g_tt_t0 = wm_now_ms();
        if (g_tt_show) { g_tt_show = 0; return 1; }
        return 0;
    }
    if (!g_tt_show && wm_now_ms() - g_tt_t0 > 500) {
        const char *t = tooltip_at(g_cx, g_cy);
        if (t && t[0]) { int i = 0; for (; t[i] && i < 47; i++) g_tt_buf[i] = t[i]; g_tt_buf[i] = 0; g_tt_show = 1; return 1; }
        g_tt_t0 = wm_now_ms();                         /* nothing here; don't re-scan every frame */
    }
    return 0;
}
static void draw_tooltip(void) {
    if (!g_tt_show || !g_tt_buf[0]) return;
    int tw = (int)font_measure(g_tt_buf), th = (int)font_height();
    int bw = tw + 8, bh = th + 5, bx = g_cx + 12, by = g_cy + 20;
    if (bx + bw > (int)fb_width()) bx = (int)fb_width() - bw;
    if (by + bh > (int)fb_height()) by = g_cy - bh - 2;
    fb_fill_rect(bx, by, bw, bh, RGB(0xFF,0xFF,0xE1));                    /* info-tip yellow */
    fb_fill_rect(bx, by, bw, 1, 0); fb_fill_rect(bx, by + bh - 1, bw, 1, 0);
    fb_fill_rect(bx, by, 1, bh, 0); fb_fill_rect(bx + bw - 1, by, 1, bh, 0);
    fb_text_t(bx + 4, by + 2, g_tt_buf, RGB(0,0,0));
}
/* double-click the tray clock -> Date/Time properties (real Win95 behaviour) */
static int clock_click(int x, int y) {
    if (!g_show_clock || g_clk_w <= 0) return 0;
    if (x < g_clk_x || x >= g_clk_x + g_clk_w || y < g_clk_y || y >= g_clk_y + 24) return 0;
    static u32 last; u32 now = wm_now_ms();
    if (now - last < g_dclick_ms) { applet_open(12); last = 0; return 1; }   /* A_TIMEDATE = 12 */
    last = now; return 1;                          /* first click: consume, arm the double-click */
}

/* ---- tray volume control (opens from the tray speaker) ---- */
#define VOL_W 60
#define VOL_H 138
static void vol_rect(int *x, int *y) {
    *x = g_spk_x - 4; *y = (int)fb_height() - TB_H - VOL_H - 2;
    if (*x + VOL_W > (int)fb_width()) *x = (int)fb_width() - VOL_W - 2;
}
static void vol_track(int x, int y, int *cx, int *t0, int *t1) {
    *cx = x + VOL_W / 2; *t0 = y + 24; *t1 = y + VOL_H - 40;
}
static void draw_volume(void) {
    if (!g_vol_open) return;
    int x, y; vol_rect(&x, &y);
    fb_fill_rect(x, y, VOL_W, VOL_H, C_FACE); wm_bevel_raised(x, y, VOL_W, VOL_H);
    fb_text_t(x + (VOL_W - (int)font_measure("Volume")) / 2, y + 5, "Volume", C_DK);
    int cx, t0, t1; vol_track(x, y, &cx, &t0, &t1);
    wm_bevel_sunken(cx - 2, t0, 4, t1 - t0);                 /* slider track */
    int th = t1 - (t1 - t0) * g_vol / 100;                   /* thumb (0 bottom..100 top) */
    fb_fill_rect(cx - 9, th - 4, 18, 9, C_FACE); wm_bevel_raised(cx - 9, th - 4, 18, 9);
    hline(cx - 8, th, 16, C_SHADOW);
    int mx = x + 8, my = y + VOL_H - 20;                     /* mute checkbox */
    fb_fill_rect(mx, my, 11, 11, C_WHITE); wm_bevel_sunken(mx, my, 11, 11);
    if (g_vol_mute)
        for (int i = 0; i < 2; i++) {
            fb_pixel(mx + 2, my + 4 + i, C_DK); fb_pixel(mx + 4, my + 6 + i, C_DK);
            fb_pixel(mx + 6, my + 4 + i, C_DK); fb_pixel(mx + 8, my + 2 + i, C_DK);
        }
    fb_text_t(mx + 15, my - 1, "Mute", C_DK);
}
void wm_volume_popup(void) { g_vol_open = 1; }   /* open the tray volume popup programmatically */
/* handle a click while the volume popup logic is active; returns 1 if consumed */
static int vol_click(int mx, int my) {
    /* the tray speaker: single-click toggles the mini popup, double-click opens
     * the full Volume Control mixer (exactly like Win95). */
    if (g_spk_x > 0 && mx >= g_spk_x && mx < g_spk_x + 18 && my >= g_spk_y && my < g_spk_y + 24) {
        static u32 spk_last; u32 now = wm_now_ms();
        if (now - spk_last < g_dclick_ms) { g_action = 25; g_vol_open = 0; spk_last = 0; return 1; }  /* open Volume Control (module) */
        spk_last = now;
        g_vol_open = !g_vol_open; return 1;
    }
    if (!g_vol_open) return 0;
    int x, y; vol_rect(&x, &y);
    if (mx < x || mx >= x + VOL_W || my < y || my >= y + VOL_H) { g_vol_open = 0; return 1; }
    int cx, t0, t1; vol_track(x, y, &cx, &t0, &t1);
    if (my >= t0 - 6 && my <= t1 + 6 && mx >= cx - 12 && mx <= cx + 12) {   /* set level */
        int v = (t1 - my) * 100 / (t1 - t0);
        g_vol = v < 0 ? 0 : v > 100 ? 100 : v;
        sys_set_volume(g_vol);                     /* set the REAL system volume */
        return 1;
    }
    int cbx = x + 8, cby = y + VOL_H - 20;
    if (mx >= cbx && mx < cbx + 60 && my >= cby - 1 && my < cby + 12) { g_vol_mute = !g_vol_mute; sys_set_mute(g_vol_mute); return 1; }
    return 1;   /* clicks inside the popup are consumed */
}

/* ---- cursor ---- */
/* the pointer bitmap (a natively-decoded .CUR, set at boot); a small built-in
 * arrow is the fallback so there is always a cursor. */
static const Debian95Cursor *g_cursor;         /* arrow (default) */
static const Debian95Cursor *g_cur_ibeam, *g_cur_size;
void wm_set_cursor(const Debian95Cursor *c)       { g_cursor = c; }
void wm_set_cursor_ibeam(const Debian95Cursor *c) { g_cur_ibeam = c; }
void wm_set_cursor_size(const Debian95Cursor *c)  { g_cur_size = c; }
void wm_set_text_cursor(Window *w) { if (w) ((struct Window *)w)->text_cursor = 1; }

/* the Win95 hourglass (busy) cursor: 16x16, 1=black outline 2=white glass
 * 3=gray sand, hotspot centred. Shown while a launch/op is in progress. */
static const u8 hourglass_pix[16 * 16] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0,1,2,2,2,2,2,2,2,2,2,2,2,2,1,0,
    0,1,2,3,3,3,3,3,3,3,3,3,3,2,1,0,
    0,0,1,2,3,3,3,3,3,3,3,3,2,1,0,0,
    0,0,0,1,2,3,3,3,3,3,3,2,1,0,0,0,
    0,0,0,0,1,2,3,3,3,3,2,1,0,0,0,0,
    0,0,0,0,0,1,2,3,3,2,1,0,0,0,0,0,
    0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,
    0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,
    0,0,0,0,0,1,2,2,2,2,1,0,0,0,0,0,
    0,0,0,0,1,2,2,2,2,2,2,1,0,0,0,0,
    0,0,0,1,2,2,3,3,3,3,2,2,1,0,0,0,
    0,0,1,2,2,3,3,3,3,3,3,2,2,1,0,0,
    0,1,2,2,3,3,3,3,3,3,3,3,2,2,1,0,
    0,1,2,3,3,3,3,3,3,3,3,3,3,2,1,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};
static const Debian95Cursor g_hourglass = { 16, 16, 8, 8, hourglass_pix };
static int g_busy, g_busy_frames;
void wm_set_busy(int on) { g_busy = on ? 1 : 0; g_busy_frames = on ? 80 : 0; }  /* ~1.2s */
int  wm_busy_tick(void) {                       /* auto-clear; returns 1 on the clear edge */
    if (!g_busy) return 0;
    if (--g_busy_frames <= 0) { g_busy = 0; return 1; }
    return 0;
}

static const Debian95Cursor *pick_cursor(void) {
    if (g_busy) return &g_hourglass;
    for (int i = g_nz - 1; i >= 0; i--) {          /* topmost window under the pointer */
        struct Window *w = g_z[i];
        if (!w->visible || !hit_win(w, g_cx, g_cy)) continue;
        if (g_cur_size && hit_grip(w, g_cx, g_cy)) return g_cur_size;
        if (g_cur_ibeam && w->text_cursor) {
            int cx, cy, cw, ch; wm_client_rect((Window *)w, &cx, &cy, &cw, &ch);
            if (g_cx >= cx && g_cx < cx + cw && g_cy >= cy && g_cy < cy + ch) return g_cur_ibeam;
        }
        break;
    }
    return g_cursor;
}

/* the authentic Win95 pointer: 11x16, 1 = black outline, 2 = white fill,
 * hotspot at the tip (0,0). Small and proportional, like the real thing. */
static const u8 fallback_arrow[11 * 16] = {
    1,0,0,0,0,0,0,0,0,0,0,
    1,1,0,0,0,0,0,0,0,0,0,
    1,2,1,0,0,0,0,0,0,0,0,
    1,2,2,1,0,0,0,0,0,0,0,
    1,2,2,2,1,0,0,0,0,0,0,
    1,2,2,2,2,1,0,0,0,0,0,
    1,2,2,2,2,2,1,0,0,0,0,
    1,2,2,2,2,2,2,1,0,0,0,
    1,2,2,2,2,2,2,2,1,0,0,
    1,2,2,2,2,2,1,1,1,1,0,
    1,2,2,1,2,2,1,0,0,0,0,
    1,2,1,0,1,2,2,1,0,0,0,
    1,1,0,0,1,2,2,1,0,0,0,
    1,0,0,0,0,1,2,2,1,0,0,
    0,0,0,0,0,1,2,2,1,0,0,
    0,0,0,0,0,0,1,1,1,0,0,
};
static const Debian95Cursor g_fallback = { 11, 16, 0, 0, fallback_arrow };

static void draw_cursor(void) {
    const Debian95Cursor *c = pick_cursor();
    if (!c) c = &g_fallback;
    int ox = g_cx - c->hotx, oy = g_cy - c->hoty;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            u8 v = c->pix[y * c->w + x];
            if (!v) continue;
            u32 col = v == 1 ? C_DK : v == 2 ? C_WHITE : C_SHADOW;
            fb_pixel(ox + x, oy + y, col);
        }
}
/* the damage-limited software cursor: composited straight onto the scanout, so
 * a plain mouse move only touches the cursor's rectangles (no full-frame flush,
 * which flickers on QEMU's deferred-flush framebuffer). */
static int g_cur_ox = -1000, g_cur_oy, g_cur_ow, g_cur_oh;
static void cursor_to_front(void) {
    const Debian95Cursor *c = pick_cursor();
    if (!c) c = &g_fallback;
    int ox = g_cx - c->hotx, oy = g_cy - c->hoty;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            u8 v = c->pix[y * c->w + x]; if (!v) continue;
            u32 col = v == 1 ? C_DK : v == 2 ? C_WHITE : C_SHADOW;
            fb_front_pixel(ox + x, oy + y, col);
        }
    g_cur_ox = ox; g_cur_oy = oy; g_cur_ow = c->w; g_cur_oh = c->h;
}
/* draw the cursor onto the scanout after a full present */
void wm_cursor_overlay(void) { cursor_to_front(); }
/* a cursor-only move: erase the old cursor from the scanout, draw the new one */
void wm_cursor_move(void) {
    if (g_cur_ox > -1000) fb_flush_rect(g_cur_ox, g_cur_oy, g_cur_ow, g_cur_oh);
    cursor_to_front();
}

/* ---- wallpaper ---- */
static const u32 *g_wall;
static int g_wall_w, g_wall_h;

void wm_set_wallpaper(const void *lgb, u32 len) {
    const u8 *p = (const u8 *)lgb;
    if (len < 8 || p[0] != 'L' || p[1] != 'G' || p[2] != 'B' || p[3] != '1') return;
    int w = p[4] | (p[5] << 8);
    int h = p[6] | (p[7] << 8);
    if ((u32)8 + (u32)w * h * 4 > len) return;
    g_wall = (const u32 *)(p + 8);
    g_wall_w = w; g_wall_h = h;
}

/* set the wallpaper from a decoded pixel buffer (e.g. a natively-loaded BMP) */
void wm_set_wallpaper_px(const u32 *px, int w, int h) {
    g_wall = px; g_wall_w = w; g_wall_h = h;
}
static int g_wall_mode = 0;                  /* 0 = Tile, 1 = Center, 2 = Stretch */
void wm_set_wallpaper_mode(int m) { g_wall_mode = m; }
int  wm_wallpaper_mode(void) { return g_wall_mode; }

static void draw_background(void) {
    int sw = (int)fb_width(), sh = (int)fb_height();
    if (g_wall && g_wall_on && g_wall_w > 0 && g_wall_h > 0) {
        if (g_wall_mode == 2) {                             /* Stretch (nearest-neighbour) */
            for (int y = 0; y < sh; y++) { int sy = y * g_wall_h / sh;
                for (int x = 0; x < sw; x++) fb_pixel(x, y, g_wall[sy * g_wall_w + (x * g_wall_w / sw)]); }
        } else if (g_wall_mode == 1 || (g_wall_w >= sw && g_wall_h >= sh)) {  /* Center */
            fb_clear(g_desktop);
            fb_blit((sw - g_wall_w) / 2, (sh - g_wall_h) / 2, g_wall_w, g_wall_h, g_wall);
        } else {                                            /* Tile */
            for (int y = 0; y < sh; y += g_wall_h)
                for (int x = 0; x < sw; x += g_wall_w)
                    fb_blit(x, y, g_wall_w, g_wall_h, g_wall);
        }
    } else {
        fb_clear(g_desktop);
    }
    /* brand wordmark + version marker, stacked in the bottom-right of the wallpaper
     * above the taskbar. The marker shows the shell/desktop release and the running
     * kernel so you can tell at a glance what you're on. */
    const char *brand   = "Debian95";
    const char *edition = "Desktop Edition " DEBIAN95_VERSION;   /* e.g. "Desktop Edition 1.0" */
    /* kernel release is read once from /proc and cached ("kernel 6.1.0-..-amd64") */
    static char s_kern[80];
    if (!s_kern[0]) {
        char kr[64] = {0};
        if (sys_read_file("/proc/sys/kernel/osrelease", kr, (int)sizeof kr - 1) != 0) kr[0] = 0;
        for (int i = 0; kr[i]; i++) if (kr[i] == '\n' || kr[i] == '\r') { kr[i] = 0; break; }
        const char *pre = "kernel "; int j = 0;
        while (*pre) s_kern[j++] = *pre++;
        const char *src = kr[0] ? kr : "unknown";
        while (*src && j < (int)sizeof s_kern - 1) s_kern[j++] = *src++;
        s_kern[j] = 0;
    }
    int rx  = (int)fb_width() - 14;                 /* shared right margin */
    int tby = (int)fb_height() - TB_H;              /* taskbar top */
    int ky  = tby - 10 - 12;                        /* kernel line */
    int ey  = ky - 15;                              /* edition line */
    int ly  = ey - 22;                              /* logo top (16px tall + gap) */
    int lx  = rx - 8 * 16;                          /* 8 glyphs * 8px * 2x */
    fb_text2x_builtin(lx + 2, ly + 2, brand, RGB(0x00, 0x30, 0x30));     /* drop shadow */
    fb_text2x_builtin(lx, ly, brand, C_WHITE);
    u32 sub = RGB(0xCF, 0xD6, 0xDE), ssh = RGB(0x10, 0x18, 0x20);        /* subtext + shadow */
    int cx = lx + 8 * 16 / 2;                       /* wordmark centre - centre each line on it */
    int ex = cx - (int)fb_measure(edition) / 2;
    fb_text_t(ex + 1, ey + 1, edition, ssh); fb_text_t(ex, ey, edition, sub);
    int kx = cx - (int)fb_measure(s_kern) / 2;
    fb_text_t(kx + 1, ky + 1, s_kern, ssh);  fb_text_t(kx, ky, s_kern, sub);
}

/* ---- shared text clipboard (Character Map, Notepad, Clipboard Viewer) ---- */
static char g_clipboard[512];
void wm_clipboard_set(const char *text) {
    int i = 0; if (text) for (; text[i] && i < (int)sizeof g_clipboard - 1; i++) g_clipboard[i] = text[i];
    g_clipboard[i] = 0;
}
const char *wm_clipboard_get(void) { return g_clipboard; }

/* ---- context menus (right-click) + window arrangement ---- */
#define MAXPOP 10
/* a nested fly-out menu attached to a parent item (Win95 "New >", "Send To >") */
typedef WmContextSub PopSub;
static int g_pop_open, g_pop_x, g_pop_y, g_pop_n, g_pop_w;
static const char *g_pop_item[MAXPOP];
static int g_pop_act[MAXPOP];
static const PopSub *g_pop_sub[MAXPOP];    /* per-item fly-out, or 0 */
static int g_pop_subopen = -1;             /* index of the item whose fly-out is open */
static int g_fly_x, g_fly_y, g_fly_w, g_fly_n;   /* geometry of the open fly-out */
static struct Window *g_ctx_win;

/* WM_CLOSE/DestroyWindow: terminate the app instance - run its cleanup, drop it
 * from the z-order, and free the slot so nothing of it keeps running. */
void wm_destroy(Window *win) {
    struct Window *w = (struct Window *)win;
    if (!w || !w->alive) return;
    if (w->onclose) w->onclose();           /* app frees its resources + nulls its window ref */
    for (int i = 0; i < g_nz; i++) if (g_z[i] == w) { for (int j = i; j < g_nz - 1; j++) g_z[j] = g_z[j + 1]; g_nz--; break; }
    if (g_menu_win == w) { g_menu_win = 0; g_menu_open = -1; }
    if (g_ctx_win == w) g_ctx_win = 0;
    w->alive = 0; w->visible = 0; w->minimized = 0;
    if (g_nstore > 0) g_nstore--;
}

static void (*g_pop_cb)(int);   /* when set, context_pick routes the action here */

static void popup_at_sub(int x, int y, const char **items, const int *acts,
                         const PopSub **subs, int n) {
    g_pop_cb = 0;               /* built-in popup unless wm_context_menu overrides */
    g_pop_subopen = -1;
    g_pop_n = n > MAXPOP ? MAXPOP : n;
    int w = 150;
    for (int i = 0; i < g_pop_n; i++) { g_pop_item[i] = items[i]; g_pop_act[i] = acts[i];
        g_pop_sub[i] = subs ? subs[i] : 0;
        int tw = (int)font_measure(items[i]) + 32; if (tw > w) w = tw; }
    int h = g_pop_n * 18 + 6;
    if (x + w > (int)fb_width())  x = (int)fb_width() - w;
    if (y + h > (int)fb_height()) y = (int)fb_height() - h;
    g_pop_x = x; g_pop_y = y; g_pop_w = w; g_pop_open = 1;
}
static void popup_at(int x, int y, const char **items, const int *acts, int n) {
    popup_at_sub(x, y, items, acts, 0, n);
}
/* a right-pointing triangle marking an item that opens a fly-out */
static void popup_arrow(int x, int iy, int on_sel) {
    static const int tw[7] = { 1, 2, 3, 4, 3, 2, 1 };
    u32 c = on_sel ? C_WHITE : C_DK;
    for (int k = 0; k < 7; k++) fb_fill_rect(x, iy + 4 + k, tw[k], 1, c);
}
static void draw_popup(void) {
    if (!g_pop_open) return;
    int x = g_pop_x, y = g_pop_y, w = g_pop_w, h = g_pop_n * 18 + 6;
    fb_fill_rect(x, y, w, h, C_FACE); wm_bevel_raised(x, y, w, h);
    int over_parent = 0;
    for (int i = 0; i < g_pop_n; i++) {
        int iy = y + 3 + i * 18;
        if (g_pop_act[i] == -1) { hline(x + 6, iy + 8, w - 12, C_SHADOW); hline(x + 6, iy + 9, w - 12, C_HILIGHT); continue; }
        int hassub = g_pop_sub[i] && g_pop_sub[i]->n > 0;
        int hov = g_cx >= x && g_cx < x + w && g_cy >= iy && g_cy < iy + 18;
        if (hov) { over_parent = 1; g_pop_subopen = hassub ? i : -1; }
        int lit = hov || (hassub && g_pop_subopen == i);
        if (lit) fb_fill_rect(x + 2, iy, w - 4, 18, C_SEL);
        fb_text_t(x + 14, iy + 3, g_pop_item[i], lit ? C_WHITE : C_DK);
        if (hassub) popup_arrow(x + w - 12, iy, lit);
    }
    /* mouse resting inside the open fly-out must keep its parent selected */
    if (!over_parent && g_pop_subopen >= 0 &&
        !(g_cx >= g_fly_x && g_cx < g_fly_x + g_fly_w && g_cy >= g_fly_y && g_cy < g_fly_y + g_fly_n * 18 + 6))
        g_pop_subopen = -1;
    if (g_pop_subopen >= 0) {
        const PopSub *s = g_pop_sub[g_pop_subopen];
        int fw = 150;
        for (int i = 0; i < s->n; i++) { int tw = (int)font_measure(s->items[i]) + 32; if (tw > fw) fw = tw; }
        int fh = s->n * 18 + 6;
        int fx = x + w - 3, fy = y + 3 + g_pop_subopen * 18 - 3;
        if (fx + fw > (int)fb_width())  fx = x - fw + 3;
        if (fy + fh > (int)fb_height()) fy = (int)fb_height() - fh;
        g_fly_x = fx; g_fly_y = fy; g_fly_w = fw; g_fly_n = s->n;
        fb_fill_rect(fx, fy, fw, fh, C_FACE); wm_bevel_raised(fx, fy, fw, fh);
        for (int i = 0; i < s->n; i++) {
            int iy = fy + 3 + i * 18;
            if (s->acts[i] == -1) { hline(fx + 6, iy + 8, fw - 12, C_SHADOW); hline(fx + 6, iy + 9, fw - 12, C_HILIGHT); continue; }
            int hov = g_cx >= fx && g_cx < fx + fw && g_cy >= iy && g_cy < iy + 18;
            if (hov) fb_fill_rect(fx + 2, iy, fw - 4, 18, C_SEL);
            fb_text_t(fx + 14, iy + 3, s->items[i], hov ? C_WHITE : C_DK);
        }
    }
}
static void arrange_cascade(void) {
    int n = 0;
    for (int i = 0; i < g_nz; i++) if (g_z[i]->visible) { g_z[i]->x = 40 + n * 24; g_z[i]->y = 40 + n * 24; n++; }
}
static void arrange_tile(void) {
    int n = 0; for (int i = 0; i < g_nz; i++) if (g_z[i]->visible) n++;
    if (!n) return;
    int cols = 1; while (cols * cols < n) cols++;
    int rows = (n + cols - 1) / cols;
    int sw = (int)fb_width(), sh = (int)fb_height() - TB_H;
    int cw = sw / cols, chh = sh / rows, k = 0;
    for (int i = 0; i < g_nz; i++) if (g_z[i]->visible) {
        g_z[i]->x = (k % cols) * cw; g_z[i]->y = (k / cols) * chh;
        g_z[i]->w = cw - 4; g_z[i]->h = chh - 4; k++;
    }
}
static void minimize_all(void) { for (int i = 0; i < g_nz; i++) g_z[i]->visible = 0; }

static void desk_icon_menu(int hit);   /* defined after open_user_item */
/* right-click a taskbar button -> per-window menu (Restore/Minimize/Maximize/Close) */
static struct Window *g_ctx_tbwin;
static void tbtn_cb(int act) {
    struct Window *w = g_ctx_tbwin; if (!w) return;
    if (w->external) {                                             /* foreign X app: route to debian95-wm */
        if (act == 0 || act == 2) { w->visible = 1; w->minimized = 0; ext_push(w, 1); }   /* Restore/Maximize -> raise */
        else if (act == 1)        { w->visible = 0; w->minimized = 1; ext_push(w, 2); }   /* Minimize */
        else if (act == 3)        { ext_push(w, 3); }                                      /* Close */
        return;
    }
    if (act == 0)      { w->visible = 1; w->minimized = 0; if (w->maxed) wm_toggle_max(w); bring_front(w); }  /* Restore */
    else if (act == 1) { w->visible = 0; w->minimized = 1; }                                                  /* Minimize */
    else if (act == 2) { w->visible = 1; w->minimized = 0; bring_front(w); if (!w->maxed) wm_toggle_max(w); } /* Maximize */
    else if (act == 3) { wm_destroy(w); }                                                                     /* Close */
}
static void zoom_to_taskbar(struct Window *w);     /* minimize/restore zoom (defined below) */
static void zoom_from_taskbar(struct Window *w);
/* the window system menu (title right-click, the caption icon, and Alt+Space).
 * Items fire the same context actions the dispatch below handles. */
static const char *M_win_i[] = { "Restore", "Move", "Size", "Minimize", "Maximize", "-", "Close" };
static const int   M_win_a[] = { -19, -17, -18, -20, -21, -1, -15 };
static void open_sysmenu_for(struct Window *w) {
    if (!w) return; g_ctx_win = w; bring_front(w);
    popup_at(w->x + 4, w->y + 3 + WM_TITLEH, M_win_i, M_win_a, 7);
}
void wm_sysmenu(void) { open_sysmenu_for(front_visible()); }   /* Alt+Space */

/* returns 1 if a right-click opened a context menu here */
static int context_rclick(void) {
    static const char *TBW_i[] = { "Address", "Links" };   /* Toolbars fly-out (deskbands) */
    static const int    TBW_a[] = { -30, -31 };
    static const PopSub TBW = { TBW_i, TBW_a, 2 };
    static const char *M_task[] = { "Toolbars", "Cascade", "Tile Windows", "Minimize All Windows", "-", "Properties" };
    static const int    A_task[] = { -40, -10, -11, -12, -1, 6 };   /* Properties -> Control Panel */
    static const PopSub *S_task[] = { &TBW, 0, 0, 0, 0, 0 };
    /* the desktop "New >" fly-out: acts 1..3 create a real Folder/Text/Bitmap */
    static const char *NW_i[] = { "Folder", "-", "Text Document", "Bitmap Image" };
    static const int    NW_a[] = { 1, -1, 2, 3 };
    static const PopSub NW = { NW_i, NW_a, 4 };
    static const char *M_desk[] = { "Arrange Icons", "Line up Icons", "Refresh", "-", "New", "-", "Properties" };
    static const int    A_desk[] = { -16, -16, -13, -1, 0, -1, 20 };   /* -16 = reset icon positions */
    static const PopSub *S_desk[] = { 0, 0, 0, 0, &NW, 0, 0 };
    struct Window *w = 0;
    for (int i = g_nz - 1; i >= 0; i--) if (g_z[i]->visible && hit_win(g_z[i], g_cx, g_cy)) { w = g_z[i]; break; }
    /* a window whose client wants right-clicks (games, custom menus) gets it directly */
    if (w && w->onrclick && !hit_title(w, g_cx, g_cy)) {
        bring_front(w);
        int cx, cy, cw, ch; wm_client_rect(w, &cx, &cy, &cw, &ch);
        w->onrclick(g_cx - cx, g_cy - cy);
        return 0;
    }
    if (w) { g_ctx_win = w; bring_front(w); popup_at(g_cx, g_cy, M_win_i, M_win_a, 7); }
    else if (g_cy >= (int)fb_height() - TB_H) {
        int tb = -1;                                  /* a taskbar button under the cursor? */
        for (int i = 0; i < g_ntbtn; i++)
            if (g_cx >= g_tbtn[i].x && g_cx < g_tbtn[i].x + g_tbtn[i].w) { tb = i; break; }
        if (tb >= 0) {
            g_ctx_tbwin = g_tbtn[tb].win;
            static const char *M[] = { "Restore", "Minimize", "Maximize", "-", "Close" };
            static const int   A[] = { 0, 1, 2, -1, 3 };
            wm_context_menu(M, A, 5, tbtn_cb);
        } else popup_at_sub(g_cx, g_cy, M_task, A_task, S_task, 6);
    }
    else {
        int hit = -1;                                 /* a desktop icon under the cursor? */
        for (int i = 0; i < g_nicon; i++)
            if (g_cx >= g_icon[i].x && g_cx < g_icon[i].x + g_icon[i].w &&
                g_cy >= g_icon[i].y && g_cy < g_icon[i].y + g_icon[i].h) { hit = i; break; }
        if (hit >= 0) desk_icon_menu(hit);
        else popup_at_sub(g_cx, g_cy, M_desk, A_desk, S_desk, 7);
    }
    return 1;
}
/* desktop "New >" — create a real, uniquely-named item INSIDE the Desktop
 * folder (like Win95's C:\WINDOWS\Desktop), never at the raw volume root. The
 * new item is dropped straight into inline-rename, exactly as Windows does. */
static void dk_set(char *d, const char *s) { int i = 0; for (; s[i]; i++) d[i] = s[i]; d[i] = 0; }
static void dk_cat(char *d, const char *s) { int l = 0; while (d[l]) l++; for (int i = 0; s[i]; i++) d[l++] = s[i]; d[l] = 0; }
static int  desk_exists(const char *nm) {
    FsEntry e[MAXUSER]; int n = FsListDir("Desktop", e, MAXUSER);
    for (int i = 0; i < n; i++) { int eq = 1; for (int k = 0;; k++) { char a=e[i].name[k],b=nm[k];
        if (a>='A'&&a<='Z')a+=32; if (b>='A'&&b<='Z')b+=32; if (a!=b){eq=0;break;} if(!a)break; } if (eq) return 1; }
    return 0;
}
/* build a unique desktop name: "base"[ (n)]"ext" */
static void uniq_name(const char *base, const char *ext, char *out) {
    dk_set(out, base); dk_cat(out, ext);
    for (int k = 2; k < 30 && desk_exists(out); k++) {
        out[0] = 0; dk_cat(out, base); dk_cat(out, " (");
        char d[4]; int p = 0; if (k >= 10) d[p++] = (char)('0'+k/10); d[p++] = (char)('0'+k%10); d[p] = 0;
        dk_cat(out, d); dk_cat(out, ")"); dk_cat(out, ext);
    }
}
static void desktop_new(int act) {
    FsMkdir("Desktop");                          /* ensure the desktop folder exists */
    char name[40], path[64];
    if (act == 1) {                              /* New Folder */
        uniq_name("New Folder", "", name);
        path[0] = 0; dk_cat(path, "Desktop/"); dk_cat(path, name); FsMkdir(path);
    } else if (act == 2) {                        /* New Text Document.txt */
        uniq_name("New Text Document", ".txt", name);
        path[0] = 0; dk_cat(path, "Desktop/"); dk_cat(path, name); FsWriteFile(path, (const u8 *)"", 0);
    } else if (act == 3) {                        /* New Bitmap Image.bmp (blank 1x1 24bpp) */
        static const u8 bmp[58] = {
            'B','M',58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,
            1,0,24,0,0,0,0,0,4,0,0,0,0x13,0x0B,0,0,0x13,0x0B,0,0,0,0,0,0,0,0,0,0,
            0xFF,0xFF,0xFF,0 };
        uniq_name("New Bitmap Image", ".bmp", name);
        path[0] = 0; dk_cat(path, "Desktop/"); dk_cat(path, name); FsWriteFile(path, bmp, sizeof bmp);
    } else return;
    dk_set(g_ren_pending, name);                   /* rename it as soon as it shows up */
}
static void open_user_item(int u) {
    static char path[64];
    if (u < 0 || u >= g_nuser) return;
    path[0] = 0; dk_cat(path, "Desktop/"); dk_cat(path, g_uname[u]);
    if (ends_ci(g_uname[u], ".lnk")) {           /* a shortcut: open its target */
        u8 *d; u32 n; char tgt[128]; tgt[0] = 0;
        if (FsReadAlloc(path, &d, &n) == 0) {
            u32 i = 0; while (i < n && d[i] != '\n') i++; if (i < n) i++;     /* skip "LNK" header */
            int k = 0; while (i < n && d[i] != '\n' && d[i] != '\r' && k < 127) tgt[k++] = (char)d[i++]; tgt[k] = 0;
            kfree(d);
        }
        if (tgt[0]) { if (ends_ci(tgt, ".bmp")) viewer_open(tgt); else app_launch_doc(4, tgt); }
        return;
    }
    if (g_uisdir[u])                     files_open();       /* Explorer */
    else if (ends_ci(g_uname[u], ".bmp")) viewer_open(path);  /* image viewer */
    else                                 app_launch_doc(4, path);  /* Notepad (module) */
}
/* desktop-icon right-click context menu (Open / Delete / Rename / Properties) */
static int g_ctx_icon = -1;
static void desk_icon_cb(int act) {
    int i = g_ctx_icon; if (i < 0 || i >= g_nicon) return;
    int u = g_icon[i].uidx;
    if (act == 1) { if (u < 0) g_action = g_icon[i].act; else open_user_item(u); }
    else if (act == 2) {                              /* Delete -> Recycle Bin (all selected) */
        unsigned m = g_iselmask ? g_iselmask : (u >= 0 ? (1u << i) : 0);
        for (int k = 0; k < g_nicon && k < 32; k++) if (m & (1u << k)) {
            int uu = g_icon[k].uidx;
            if (uu >= 0) { char path[80]; path[0] = 0; dk_cat(path, "Desktop/"); dk_cat(path, g_uname[uu]); recycle_add(path); }
        }
        g_iselmask = 0; g_sel_icon = -1;
    }
    else if (act == 90 && u >= 0 && !g_uisdir[u]) {   /* Open With... */
        extern void fm_openwith_for(const char *full, const char *name);
        char path[80]; path[0] = 0; dk_cat(path, "Desktop/"); dk_cat(path, g_uname[u]);
        fm_openwith_for(path, g_uname[u]);
    }
    else if (act == 8 && u >= 0) { dk_set(g_ren_pending, g_uname[u]); }   /* Rename (inline) */
    else if (act == 4) {                              /* Properties */
        char msg[80]; msg[0] = 0;
        if (u < 0) { dk_cat(msg, g_icon[i].key); dk_cat(msg, "\nSystem object"); }
        else { dk_cat(msg, g_uname[u]); dk_cat(msg, g_uisdir[u] ? "\nType: File Folder\nLocation: Desktop" : "\nType: Document\nLocation: Desktop"); }
        wm_messagebox("Properties", msg);
    }
}
static void desk_icon_menu(int hit) {
    g_ctx_icon = hit; g_sel_icon = hit;
    if (!(g_iselmask & (1u << hit))) g_iselmask = 0;   /* right-click outside the group -> just this icon */
    if (g_icon[hit].uidx >= 0) {                       /* a real desktop file/folder */
        if (!g_uisdir[g_icon[hit].uidx]) {             /* a file: offer Open With... */
            static const char *M[] = { "Open", "Open With...", "-", "Delete", "Rename", "-", "Properties" };
            static const int   A[] = { 1, 90, -1, 2, 8, -1, 4 };
            wm_context_menu(M, A, 7, desk_icon_cb);
        } else {
            static const char *M[] = { "Open", "-", "Delete", "Rename", "-", "Properties" };
            static const int   A[] = { 1, -1, 2, 8, -1, 4 };
            wm_context_menu(M, A, 6, desk_icon_cb);
        }
    } else {                                           /* My Computer / Network / Recycle */
        static const char *M[] = { "Open", "-", "Properties" };
        static const int   A[] = { 1, -1, 4 };
        wm_context_menu(M, A, 3, desk_icon_cb);
    }
}
static void ren_commit(void) {
    g_ren_active = 0;
    if (g_ren_uidx < 0 || g_ren_uidx >= g_nuser || !g_ren_buf[0]) return;
    if (desk_exists(g_ren_buf)) return;          /* unchanged or name taken -> keep old */
    char oldp[64], newp[64];
    oldp[0] = 0; dk_cat(oldp, "Desktop/"); dk_cat(oldp, g_uname[g_ren_uidx]);
    newp[0] = 0; dk_cat(newp, "Desktop/"); dk_cat(newp, g_ren_buf);
    FsRename(oldp, newp);
}
/* a left-click while a popup is open: pick an item (or close). Returns 1. */
static void desktop_new(int act);   /* fwd: creates a real file from the desktop "New >" */
static int context_pick(void) {
    int x = g_pop_x, y = g_pop_y, w = g_pop_w, picked = -1, act = 0;
    /* a click inside the open fly-out picks one of its items */
    if (g_pop_subopen >= 0 && g_cx >= g_fly_x && g_cx < g_fly_x + g_fly_w &&
        g_cy >= g_fly_y && g_cy < g_fly_y + g_fly_n * 18 + 6) {
        const PopSub *s = g_pop_sub[g_pop_subopen];
        for (int i = 0; i < s->n; i++) {
            int iy = g_fly_y + 3 + i * 18;
            if (s->acts[i] != -1 && g_cy >= iy && g_cy < iy + 18) { picked = i; act = s->acts[i]; break; }
        }
        g_pop_open = 0; g_pop_subopen = -1;
        if (picked < 0) return 1;
        if (g_pop_cb) { void (*cb)(int) = g_pop_cb; g_pop_cb = 0; cb(act); }
        else if (act == -30) { g_tb_address = !g_tb_address; if (!g_tb_address) g_addr_active = 0;
                               sys_set_setting("tb_address", g_tb_address ? "1" : "0"); }
        else if (act == -31) { g_tb_links = !g_tb_links;
                               sys_set_setting("tb_links", g_tb_links ? "1" : "0"); }
        else desktop_new(act);
        return 1;
    }
    for (int i = 0; i < g_pop_n; i++) {
        int iy = y + 3 + i * 18;
        if (g_pop_act[i] != -1 && g_cx >= x && g_cx < x + w && g_cy >= iy && g_cy < iy + 18) { picked = i; break; }
    }
    /* clicking a parent that owns a fly-out just holds the menu open */
    if (picked >= 0 && g_pop_sub[picked] && g_pop_sub[picked]->n > 0) { g_pop_subopen = picked; return 1; }
    g_pop_open = 0; g_pop_subopen = -1;
    if (picked >= 0 && g_pop_cb) {          /* app-supplied context menu */
        void (*cb)(int) = g_pop_cb; g_pop_cb = 0;
        cb(g_pop_act[picked]);
        return 1;
    }
    if (picked >= 0) {
        int a = g_pop_act[picked];
        if      (a == -10) arrange_cascade();
        else if (a == -11) arrange_tile();
        else if (a == -12) minimize_all();
        else if (a == -13) { /* refresh: recompose happens each frame */ }
        else if (a == -16) ipos_clear();          /* Arrange/Line up Icons: reset to the grid */
        else if (a == -17) { if (g_ctx_win) { bring_front(g_ctx_win); g_kbw = g_ctx_win; g_kbmode = 0; } }   /* Move (arrow keys) */
        else if (a == -18) { if (g_ctx_win) { bring_front(g_ctx_win); g_kbw = g_ctx_win; g_kbmode = 1; } }   /* Size (arrow keys) */
        else if (a == -19) { if (g_ctx_win) { struct Window *w = g_ctx_win;    /* Restore */
            if (w->maxed) wm_toggle_max(w); else if (w->minimized) { w->visible = 1; w->minimized = 0; bring_front(w); zoom_from_taskbar(w); } } }
        else if (a == -20) { if (g_ctx_win) { zoom_to_taskbar(g_ctx_win); g_ctx_win->minimized = 1; g_ctx_win->visible = 0; } }   /* Minimize */
        else if (a == -21) { if (g_ctx_win) { struct Window *w = g_ctx_win;                              /* Maximize */
            w->visible = 1; w->minimized = 0; bring_front(w); if (!w->maxed) wm_toggle_max(w); } }
        else if (a == -15) { if (g_ctx_win) g_ctx_win->visible = 0; }
        else if (a > 0)    g_action = a;
    }
    return 1;
}

/* apps open their own right-click menu at the cursor; picks route to cb(act) */
void wm_context_menu(const char **items, const int *acts, int n, void (*cb)(int)) {
    popup_at(g_cx, g_cy, items, acts, n);
    g_pop_cb = cb;
}
void wm_context_menu_sub(const char **items, const int *acts,
                         const WmContextSub *const *subs, int n, void (*cb)(int)) {
    popup_at_sub(g_cx, g_cy, items, acts, subs, n);
    g_pop_cb = cb;
}

/* the Win95 move/resize rubber band: a 1px dotted rectangle drawn by inverting
 * (XOR) every other border pixel, so it reads on any background. */
static void xor_px(int x, int y) {
    if (x < 0 || y < 0 || x >= (int)fb_width() || y >= (int)fb_height()) return;
    fb_pixel((u32)x, (u32)y, fb_get_pixel((u32)x, (u32)y) ^ 0xFFFFFF);
}
static void band_draw(int x, int y, int w, int h) {
    if (w < 2 || h < 2) return;
    for (int i = 0; i < w; i++) {
        if (((x + i + y) & 1) == 0) xor_px(x + i, y);
        if (((x + i + y + h - 1) & 1) == 0) xor_px(x + i, y + h - 1);
    }
    for (int j = 1; j < h - 1; j++) {
        if (((x + y + j) & 1) == 0) xor_px(x, y + j);
        if (((x + w - 1 + y + j) & 1) == 0) xor_px(x + w - 1, y + j);
    }
}

/* ---- minimize / maximize / restore zoom animation (the Win95 wireframe) ----
 * A dotted rectangle that zooms between a window and its taskbar button (on
 * minimize/restore) or between the restored and maximized rects (on maximize).
 * Driven a frame at a time by wm_anim_tick() from the main loop; drawn over the
 * composed frame, so a full recompose each frame erases the previous wireframe. */
static int g_anim_on, g_aframe, g_aframes;
static int g_ax0, g_ay0, g_aw0, g_ah0, g_ax1, g_ay1, g_aw1, g_ah1;
static void wm_begin_zoom(int fx, int fy, int fw, int fh, int tx, int ty, int tw, int th) {
    g_ax0 = fx; g_ay0 = fy; g_aw0 = fw; g_ah0 = fh;
    g_ax1 = tx; g_ay1 = ty; g_aw1 = tw; g_ah1 = th;
    g_aframe = 0; g_aframes = 7; g_anim_on = 1;
}
static int g_anim_hold;                        /* test: freeze mid-zoom for a screenshot */
void wm_anim_hold(int on) { g_anim_hold = on; }
int wm_anim_tick(void) {                       /* advance a frame; 1 while animating */
    if (!g_anim_on) return 0;
    if (g_anim_hold && g_aframe >= 3) return 1;
    if (++g_aframe >= g_aframes) g_anim_on = 0;
    return 1;
}
static void anim_draw(void) {
    if (!g_anim_on) return;
    int f = g_aframe, n = g_aframes;
    band_draw(g_ax0 + (g_ax1 - g_ax0) * f / n, g_ay0 + (g_ay1 - g_ay0) * f / n,
              g_aw0 + (g_aw1 - g_aw0) * f / n, g_ah0 + (g_ah1 - g_ah0) * f / n);
}
/* the taskbar button rectangle for a window (for the min/restore zoom target) */
static int tb_rect_for(struct Window *w, int *x, int *y, int *ww, int *hh) {
    for (int i = 0; i < g_ntbtn; i++)
        if (g_tbtn[i].win == w) { *x = g_tbtn[i].x; *y = g_tbtn[i].y; *ww = g_tbtn[i].w; *hh = g_tbtn[i].h; return 1; }
    return 0;
}
static void zoom_to_taskbar(struct Window *w) {            /* minimize */
    int bx, by, bw, bh; if (tb_rect_for(w, &bx, &by, &bw, &bh)) wm_begin_zoom(w->x, w->y, w->w, w->h, bx, by, bw, bh);
}
static void zoom_from_taskbar(struct Window *w) {          /* restore */
    int bx, by, bw, bh; if (tb_rect_for(w, &bx, &by, &bw, &bh)) wm_begin_zoom(bx, by, bw, bh, w->x, w->y, w->w, w->h);
}
void wm_test_min_front(void) {                             /* test hook: minimize the front window */
    struct Window *w = front_visible();
    if (w) { zoom_to_taskbar(w); w->minimized = 1; w->visible = 0; }
}
void wm_test_open_menu(int idx) {                          /* test hook: open a menu-bar dropdown */
    struct Window *w = front_visible();
    if (w && w->nmenus && idx >= 0 && idx < w->nmenus) { g_menu_win = w; g_menu_open = idx; }
}
void wm_test_marquee(void) {                               /* test hook: a marquee box over the left icons */
    g_marq_on = 1; g_marq_rx = 8; g_marq_ry = 5; g_marq_rw = 84; g_marq_rh = 485;
    int x1 = g_marq_rx + g_marq_rw, y1 = g_marq_ry + g_marq_rh;
    g_iselmask = 0;
    for (int i = 0; i < g_nicon && i < 32; i++)
        if (g_icon[i].x < x1 && g_icon[i].x + g_icon[i].w > g_marq_rx &&
            g_icon[i].y < y1 && g_icon[i].y + g_icon[i].h > g_marq_ry) g_iselmask |= (1u << i);
}

/* ---- compose ---- */
/* ---- "Welcome to Windows" logon (modal, real auth against /etc/shadow) ----
 * LOGIN mode authenticates the account with sys_auth(); on first run (no
 * password set) it enters SETUP mode to create the password (sys_set_password),
 * so the logon is genuinely secured by the system, not cosmetic. */
#define LG_ACCOUNT "administrator"        /* the real system account */
#define LG_W 388
#define LG_H 176
static int  g_logon_on, g_lg_mode;        /* mode: 0 = login, 1 = set password, 2 = locked, 3 = switch user */
static char g_lg_user[32] = "Administrator";
static char g_lg_pass[32], g_lg_conf[32];
static int  g_lg_ulen = 13, g_lg_plen, g_lg_clen, g_lg_field;   /* login: 0=user 1=pass; setup: 0=pass 1=confirm */
static int  g_lg_x, g_lg_y, g_lg_err;
/* ---- multi-user: the current profile + the account picker (mode 3) ---- */
static char g_cur_user[32] = "Administrator";
static int  g_profile_switched;           /* set on a successful switch; the platform re-applies the profile */
struct lg_user { char name[32]; int uid, is_admin; };
int  sys_user_list(struct lg_user *out, int max);
int  sys_auth(const char *user, const char *pass);
void sys_set_profile(const char *user);
static struct lg_user g_accts[16]; static int g_nacct, g_acct_sel;
const char *wm_current_user(void) { return g_cur_user; }
int  wm_take_profile_switched(void) { int v = g_profile_switched; g_profile_switched = 0; return v; }
static void draw_background(void);
static void lg_field_ptr(char **buf, int **len) {
    if (g_lg_mode == 1) { if (g_lg_field == 0) { *buf = g_lg_pass; *len = &g_lg_plen; } else { *buf = g_lg_conf; *len = &g_lg_clen; } }
    else { if (g_lg_field == 0) { *buf = g_lg_user; *len = &g_lg_ulen; } else { *buf = g_lg_pass; *len = &g_lg_plen; } }
}
static void lg_masked(const char *src, int n, char *out) { int m = n < 31 ? n : 31; for (int i = 0; i < m; i++) out[i] = '*'; out[m] = 0; }
static void draw_lg_field(int fx, int fy, int fw, const char *text, int caret) {
    fb_fill_rect(fx, fy, fw, 20, C_WHITE); wm_bevel_sunken(fx, fy, fw, 20);
    fb_text_t(fx + 4, fy + 4, text, C_DK);
    if (caret && wm_caret_on()) { int cw = (int)font_measure(text); fb_fill_rect(fx + 4 + cw, fy + 3, 1, 14, C_DK); }
}
static void draw_logon(void) {
    draw_background();
    int era = g_theme->era;
    /* per-era welcome backdrop: XP paints its blue Welcome wash, Vista/7 a dark
     * glass wash, over the wallpaper. Classic/Flat keep the plain desktop. The
     * logon box geometry + hit-testing below are unchanged across eras. */
    if (era == ERA_XP)        vgrad(0, 0, (int)fb_width(), (int)fb_height(), RGB(0x5A, 0x7E, 0xDE), RGB(0x2E, 0x5A, 0x9E));
    else if (era == ERA_AERO) vgrad(0, 0, (int)fb_width(), (int)fb_height(), RGB(0x1A, 0x2A, 0x48), RGB(0x04, 0x0C, 0x20));
    int w = LG_W, h = (g_lg_mode == 3) ? 210 : LG_H, x = ((int)fb_width() - w) / 2, y = ((int)fb_height() - h) / 2;
    g_lg_x = x; g_lg_y = y;
    if (era == ERA_XP || era == ERA_AERO) {           /* centered greeting above the box */
        const char *greet = g_lg_mode == 3 ? "To begin, click your user name" :
                            g_lg_mode == 2 ? "This computer is locked" :
                            g_lg_mode == 1 ? "Set up your password" : "Welcome";
        int gw = (int)fb_measure_bold(greet);
        fb_text_bold_t(((int)fb_width() - gw) / 2, y - 40, greet, C_WHITE);
        fb_fill_rect(((int)fb_width() - 220) / 2, y - 18, 220, 1, RGB(0xC0, 0xD0, 0xF0));
    }
    fb_fill_rect(x, y, w, h, C_FACE); wm_bevel_raised(x, y, w, h);
    int tx = x + 3, ty = y + 3, tw = w - 6, fh = (int)font_height();
    title_gradient(tx, ty, tw, WM_TITLEH);
    fb_text_bold_t(tx + 5, ty + (WM_TITLEH - fh) / 2,
        g_lg_mode == 1 ? "Set Debian95 Password" : g_lg_mode == 3 ? "Log On to Debian95" : "Welcome to Windows", C_WHITE);
    title_button(x + w - 3 - 18, ty + 2, 'x');
    { int hbx = x + w - 3 - 18 - 20;
      fb_fill_rect(hbx, ty + 2, 16, 14, C_FACE); wm_bevel_raised(hbx, ty + 2, 16, 14); fb_text_bold_t(hbx + 5, ty + 2, "?", C_DK); }
    /* authentic Win95 logon emblem: a gold key crossed over a small colour ID card */
    { u32 gold = RGB(0xF8, 0xD0, 0x38), hole = RGB(0x20, 0x30, 0x60), card = RGB(0x60, 0x80, 0xC0);
      int kx = x + 18, ky = y + 38;
      /* the card (behind the key) */
      fb_fill_rect(kx + 12, ky + 10, 23, 17, C_WHITE);              /* white border */
      fb_fill_rect(kx + 13, ky + 11, 21, 15, card);
      { static const u32 sq[6] = { RGB(0xE0,0x20,0x20), RGB(0xF0,0xC0,0x20), RGB(0x20,0xB0,0x20),
                                   RGB(0x20,0xC0,0xC0), RGB(0x30,0x40,0xE0), RGB(0xC0,0x30,0xC0) };
        for (int r = 0; r < 2; r++) for (int c = 0; c < 3; c++)
            fb_fill_rect(kx + 15 + c*6, ky + 13 + r*6, 4, 4, sq[r*3 + c]); }
      /* the gold key, diagonal over the card: bow (ring) upper-left */
      fb_fill_rect(kx + 2, ky + 2, 13, 13, gold); wm_bevel_raised(kx + 2, ky + 2, 13, 13);
      fb_fill_rect(kx + 5, ky + 5, 7, 7, hole);
      /* shaft: gold blocks along the diagonal down to the lower-right */
      for (int i = 0; i < 10; i++) { wm_bevel_raised(kx + 11 + i*2, ky + 11 + i*2, 5, 5); fb_fill_rect(kx + 12 + i*2, ky + 12 + i*2, 3, 3, gold); }
      /* teeth near the shaft end */
      fb_fill_rect(kx + 31, ky + 31, 5, 3, gold);
      fb_fill_rect(kx + 33, ky + 27, 3, 5, gold); }
    int fx = x + 108, fw = w - 108 - 104;
    char mask[32];
    if (g_lg_mode == 1) {                         /* SET PASSWORD (first run) */
        fb_text_t(x + 66, y + 40, "Set a password to protect your", C_DK);
        fb_text_t(x + 66, y + 56, "Debian95 account.", C_DK);
        fb_text_t(x + 12, y + 86, "New password:", C_DK);
        lg_masked(g_lg_pass, g_lg_plen, mask); draw_lg_field(fx, y + 82, fw, mask, g_lg_field == 0);
        fb_text_t(x + 12, y + 112, "Confirm:", C_DK);
        lg_masked(g_lg_conf, g_lg_clen, mask); draw_lg_field(fx, y + 108, fw, mask, g_lg_field == 1);
    } else if (g_lg_mode == 3) {                   /* SWITCH USER (account picker, shadow auth) */
        fb_text_t(x + 66, y + 40, "Select your user name and type", C_DK);
        fb_text_t(x + 66, y + 56, "your password to log on.", C_DK);
        fb_text_t(x + 18, y + 86, "User name:", C_DK);
        int lx = fx, ly = y + 82, lh = 58, rh = 14;                 /* account list */
        fb_fill_rect(lx, ly, fw, lh, C_WHITE); wm_bevel_sunken(lx, ly, fw, lh);
        int rows = (lh - 2) / rh;
        for (int r = 0; r < rows && r < g_nacct; r++) {
            int ay = ly + 1 + r * rh, sel = (r == g_acct_sel);
            if (sel) fb_fill_rect(lx + 1, ay, fw - 2, rh, RGB(0,0,0x80));
            const char *nm = g_accts[r].name;
            fb_text_t(lx + 4, ay + (rh - fh)/2, nm, sel ? C_WHITE : C_DK);
            if (g_accts[r].is_admin) fb_text_t(lx + fw - 46, ay + (rh - fh)/2, "(Admin)", sel ? C_WHITE : RGB(0x60,0x60,0x60));
        }
        fb_text_t(x + 18, y + 150, "Password:", C_DK);
        lg_masked(g_lg_pass, g_lg_plen, mask); draw_lg_field(fx, y + 146, fw, mask, g_lg_field == 1);
    } else {                                       /* LOG ON / UNLOCK */
        if (g_lg_mode == 2) { fb_text_t(x + 66, y + 40, "This computer is in use and has been", C_DK);
                              fb_text_t(x + 66, y + 56, "locked. Type your password to unlock.", C_DK); }
        else { fb_text_t(x + 66, y + 40, "Type a user name and password to log on", C_DK);
               fb_text_t(x + 66, y + 56, "to Windows.", C_DK); }
        fb_text_t(x + 18, y + 86, "User name:", C_DK);
        draw_lg_field(fx, y + 82, fw, g_lg_user, g_lg_field == 0);
        fb_text_t(x + 18, y + 112, "Password:", C_DK);
        lg_masked(g_lg_pass, g_lg_plen, mask); draw_lg_field(fx, y + 108, fw, mask, g_lg_field == 1);
    }
    if (g_lg_err) fb_text_t(x + 12, g_lg_mode == 3 ? y + 170 : y + 136,
        g_lg_err == 2 ? "Could not create the encrypted volume." :
        (g_lg_mode == 1 ? "Passwords do not match." : "Incorrect password."), RGB(0xC0, 0, 0));
    int bw = 78, bx = x + w - bw - 16;
    cc_defbutton(bx, y + 82, bw, 24, "OK", 0);
    cc_button(bx, y + 112, bw, 24, g_lg_mode ? "Cancel" : "Cancel", 0);
}
static void logon_submit(void) {
    if (g_lg_mode == 1) {                          /* create the password */
        if (g_lg_plen == 0 || g_lg_plen != g_lg_clen) { g_lg_err = 1; return; }
        for (int i = 0; i < g_lg_plen; i++) if (g_lg_pass[i] != g_lg_conf[i]) { g_lg_err = 1; return; }
        /* first run: LUKS-format the encrypted /data with this password, mount
         * it, and overlay the writable data dir - the password is the disk key */
        if (sys_data_format(g_lg_pass) != 0) { g_lg_err = 2; return; }
        g_lg_pass[0] = 0; g_lg_plen = 0; g_lg_conf[0] = 0; g_lg_clen = 0;
        g_logon_on = 0; wm_boot_start();           /* set + logged in -> populate desktop */
        return;
    }
    if (g_lg_mode == 2) {                          /* LOCKED: verify only, then dismiss the overlay */
        if (sys_data_verify(g_lg_pass) == 1) { g_lg_pass[0] = 0; g_lg_plen = 0; g_logon_on = 0; }
        else { g_lg_err = 1; g_lg_pass[0] = 0; g_lg_plen = 0; g_lg_field = 1; }
        return;
    }
    if (g_lg_mode == 3) {                          /* SWITCH USER: shadow auth, then load the profile */
        if (g_acct_sel < 0 || g_acct_sel >= g_nacct) { g_lg_err = 1; return; }
        const char *u = g_accts[g_acct_sel].name;
        if (sys_auth(u, g_lg_pass) == 1) {
            int k = 0; for (; u[k] && k < 31; k++) g_cur_user[k] = u[k]; g_cur_user[k] = 0;
            sys_set_profile(u); g_profile_switched = 1;              /* platform re-applies theme/wallpaper */
            g_lg_pass[0] = 0; g_lg_plen = 0; g_logon_on = 0;
        } else { g_lg_err = 1; g_lg_pass[0] = 0; g_lg_plen = 0; }
        return;
    }
    /* log on = unlock the encrypted /data: if it opens, the password is correct
     * (the LUKS passphrase IS the authentication; no /etc/shadow needed) */
    if (sys_data_unlock(g_lg_pass) == 1) { g_lg_pass[0] = 0; g_lg_plen = 0; g_logon_on = 0; wm_boot_start(); }
    else { g_lg_err = 1; g_lg_pass[0] = 0; g_lg_plen = 0; g_lg_field = 1; }
}
static void logon_click(int mx, int my) {
    int x = g_lg_x, y = g_lg_y, w = LG_W, bw = 78, bx = x + w - bw - 16;
    int fx = x + 108, fw = w - 108 - 104;
    if (mx >= bx && mx < bx + bw) {
        if (my >= y + 82 && my < y + 106) { logon_submit(); return; }                 /* OK */
        if (my >= y + 108 && my < y + 132) {                                           /* Cancel */
            if (g_lg_mode == 0) return;                                                /* login: no bypass */
            if (g_lg_mode == 3) { g_logon_on = 0; g_lg_err = 0; return; }              /* switch user: cancel keeps the session */
            g_lg_err = 0; return;
        }
    }
    if (g_lg_mode == 3) {                                                              /* account picker */
        int ly = y + 82, lh = 58, rh = 14;
        if (mx >= fx && mx < fx + fw && my >= ly && my < ly + lh) {
            int r = (my - ly - 1) / rh; if (r >= 0 && r < g_nacct) { g_acct_sel = r; g_lg_field = 1; g_lg_err = 0; }
            return;
        }
        if (mx >= fx && mx < fx + fw && my >= y + 146 && my < y + 166) g_lg_field = 1;
        return;
    }
    if (mx >= fx && mx < fx + fw) { if (my >= y + 82 && my < y + 102) g_lg_field = 0; else if (my >= y + 108 && my < y + 128) g_lg_field = 1; }
}
static void logon_key(char c) {
    if (c == '\n' || c == '\r') { logon_submit(); return; }
    if (c == '\t') { g_lg_field ^= 1; return; }
    char *buf; int *len; lg_field_ptr(&buf, &len);
    g_lg_err = 0;
    if (c == '\b') { if (*len) buf[--(*len)] = 0; return; }
    if (c >= 32 && *len < 30) { buf[(*len)++] = c; buf[*len] = 0; }
}
void wm_logon_begin(void) {
    g_logon_on = 1; g_lg_field = 0; g_lg_err = 0;
    g_lg_mode = sys_data_is_luks() ? 0 : 1;   /* /data already encrypted -> log on; else first-run set */
}
/* lock the session: re-auth with the password while /data stays mounted. */
void wm_logon_lock(void) {
    { char *getenv(const char *); if (!sys_data_is_luks() && !getenv("DEBIAN95_LOCKTEST")) return; }  /* no password -> nothing to lock to */
    g_logon_on = 1; g_lg_field = 1; g_lg_err = 0; g_lg_mode = 2;   /* mode 2 = locked (verify only); focus password */
    g_lg_pass[0] = 0; g_lg_plen = 0;
}
/* Log Off: switch user. Show the account picker and shadow-authenticate the pick;
 * the encrypted /data stays mounted (this switches profile, not the machine). */
void wm_logon_switch_user(void) {
    g_nacct = sys_user_list(g_accts, 16); g_acct_sel = 0;
    for (int i = 0; i < g_nacct; i++) {                            /* preselect the current user */
        int eq = 1; for (int k = 0;; k++) { char a = g_accts[i].name[k], b = g_cur_user[k]; if (a != b) { eq = 0; break; } if (!a) break; }
        if (eq) { g_acct_sel = i; break; }
    }
    g_logon_on = 1; g_lg_mode = 3; g_lg_field = 1; g_lg_err = 0; g_lg_pass[0] = 0; g_lg_plen = 0;
}
int  wm_logon_active(void) { return g_logon_on; }
static int logon_is_active(void) { return g_logon_on; }
/* is a text caret currently visible somewhere (so the platform keeps blinking it)? */
int wm_wants_caret(void) {
    if (g_logon_on || g_ren_active) return 1;
    struct Window *f = front_visible();
    return f && f->text_cursor;
}

/* ---- screen saver (Mystify-style bouncing polyline; activates after idle) ---- */
#define SS_PTS 4
#define SS_TRAILS 6
static int g_ss_active, g_ss_init;
static struct { int x, y, dx, dy; } g_ss_p[SS_PTS];
static u32 g_ss_trail[SS_TRAILS][SS_PTS * 2];   /* recent polyline vertices */
static int g_ss_head;
static void ss_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 - x0, dy = y1 - y0, adx = dx<0?-dx:dx, ady = dy<0?-dy:dy, n = adx>ady?adx:ady; if (!n) n = 1;
    for (int i = 0; i <= n; i++) fb_pixel(x0 + dx*i/n, y0 + dy*i/n, c);
}
/* selectable saver: 0=blank, 1=Mystify, 2=Starfield (Display Screen Saver tab) */
static int g_ss_type = 1;
#define SS_STARS 160
static struct { int x, y, z; } g_star[SS_STARS];
#define FO_N 10
static struct { int x, y, z, dx, dy, dz; u32 col; } g_fo[FO_N];   /* 3D Flying Objects */
static int ss_has(const char *h, const char *n) {                /* case-sensitive substring */
    for (; *h; h++) { const char *a = h, *b = n; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}
static unsigned g_ss_rng = 2463534242u;
static unsigned ss_rand(void) { g_ss_rng ^= g_ss_rng << 13; g_ss_rng ^= g_ss_rng >> 17; g_ss_rng ^= g_ss_rng << 5; return g_ss_rng; }
/* Pipes (3D-pipes-style, 2D): a coloured pipe that grows cell-by-cell, turning at
 * random and bouncing off the edges; accumulates (no per-frame clear) until it
 * fills, then resets. */
#define PIPE_CELL 22
static int g_pi_x, g_pi_y, g_pi_dx, g_pi_dy, g_pi_seg, g_pi_started; static u32 g_pi_col;
static void ss_ball(int cx, int cy, int r, u32 c) {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)
        if (i*i + j*j <= r*r) fb_pixel(cx + i, cy + j, (i*i + j*j > (r-2)*(r-2)) ? ((c >> 1) & 0x7F7F7F) : c);
}
static void pipes_reset(int W, int H) {
    fb_fill_rect(0, 0, W, H, RGB(0,0,0));
    g_pi_x = (int)(ss_rand() % (W / PIPE_CELL)) * PIPE_CELL + PIPE_CELL/2;
    g_pi_y = (int)(ss_rand() % (H / PIPE_CELL)) * PIPE_CELL + PIPE_CELL/2;
    static const int D[4][2] = { {1,0},{-1,0},{0,1},{0,-1} }; int d = ss_rand() % 4;
    g_pi_dx = D[d][0]; g_pi_dy = D[d][1];
    g_pi_col = RGB(0x50 + ss_rand()%0xB0, 0x50 + ss_rand()%0xB0, 0x50 + ss_rand()%0xB0);
    g_pi_seg = 0;
}
static void pipes_tick(int W, int H) {
    if (!g_pi_started) { pipes_reset(W, H); g_pi_started = 1; }
    ss_ball(g_pi_x, g_pi_y, 8, g_pi_col);                       /* joint ball */
    if (ss_rand() % 5 == 0) {                                   /* random turn */
        if (g_pi_dx) { g_pi_dx = 0; g_pi_dy = (ss_rand() & 1) ? 1 : -1; }
        else         { g_pi_dy = 0; g_pi_dx = (ss_rand() & 1) ? 1 : -1; }
    }
    int nx = g_pi_x + g_pi_dx * PIPE_CELL, ny = g_pi_y + g_pi_dy * PIPE_CELL;
    if (nx < PIPE_CELL || nx >= W - PIPE_CELL) { g_pi_dx = -g_pi_dx; nx = g_pi_x + g_pi_dx * PIPE_CELL; }
    if (ny < PIPE_CELL || ny >= H - PIPE_CELL) { g_pi_dy = -g_pi_dy; ny = g_pi_y + g_pi_dy * PIPE_CELL; }
    if (g_pi_dx) { int x0 = g_pi_x < nx ? g_pi_x : nx; fb_fill_rect(x0 - 4, g_pi_y - 5, PIPE_CELL + 8, 10, g_pi_col); }
    else         { int y0 = g_pi_y < ny ? g_pi_y : ny; fb_fill_rect(g_pi_x - 5, y0 - 4, 10, PIPE_CELL + 8, g_pi_col); }
    g_pi_x = nx; g_pi_y = ny;
    if (++g_pi_seg > (W / PIPE_CELL) * (H / PIPE_CELL) * 3 / 4) g_pi_started = 0;   /* full -> reset */
}
/* Flying Windows: the Win95 flag logo flying outward toward the viewer (starfield
 * projection, drawing a 4-colour flag whose size grows as it nears). */
static void flag_glyph(int x, int y, int s) {
    int h = s / 2; if (h < 1) h = 1;
    fb_fill_rect(x,     y,     h, h, RGB(0xE0,0x30,0x30));      /* red   */
    fb_fill_rect(x + h, y,     h, h, RGB(0x30,0xC0,0x30));      /* green */
    fb_fill_rect(x,     y + h, h, h, RGB(0x30,0x60,0xE0));      /* blue  */
    fb_fill_rect(x + h, y + h, h, h, RGB(0xF0,0xD0,0x30));      /* yellow*/
}
void wm_set_screensaver_by_name(const char *n) {   /* map a saver name -> renderer */
    if (!n || !n[0] || n[0] == '(') { g_ss_type = 1; return; }
    if      (ss_has(n, "Blank"))          g_ss_type = 0;
    else if (ss_has(n, "Marquee"))        g_ss_type = 5;
    else if (ss_has(n, "Starfield"))      g_ss_type = 2;
    else if (ss_has(n, "Pipes"))          g_ss_type = 3;
    else if (ss_has(n, "Flying Windows")) g_ss_type = 4;
    else if (ss_has(n, "Flying Objects")) g_ss_type = 6;
    else if (ss_has(n, "3D Text"))        g_ss_type = 7;
    else                                   g_ss_type = 1;   /* Mystify Your Mind */
}
void wm_screensaver_start(void) {
    g_ss_active = 1; g_pi_started = 0;                     /* pipes restart fresh each activation */
    if (!g_ss_init) { for (int i = 0; i < SS_PTS; i++) { g_ss_p[i].x = 120 + i*90; g_ss_p[i].y = 90 + i*70;
        g_ss_p[i].dx = 3 + (i % 3); g_ss_p[i].dy = 2 + (i % 2); } g_ss_head = 0;
        for (int i = 0; i < SS_STARS; i++) { g_star[i].x = (int)(ss_rand() % 2000) - 1000; g_star[i].y = (int)(ss_rand() % 2000) - 1000; g_star[i].z = 1 + (int)(ss_rand() % 1000); }
        for (int i = 0; i < FO_N; i++) {                   /* 3D Flying Objects: random balls in a 3D box */
            g_fo[i].x = (int)(ss_rand()%1600)-800; g_fo[i].y = (int)(ss_rand()%1600)-800; g_fo[i].z = 400 + (int)(ss_rand()%1400);
            g_fo[i].dx = 6+(int)(ss_rand()%10); g_fo[i].dy = 5+(int)(ss_rand()%10); g_fo[i].dz = 6+(int)(ss_rand()%12);
            if (ss_rand()&1) g_fo[i].dx=-g_fo[i].dx; if (ss_rand()&1) g_fo[i].dy=-g_fo[i].dy;
            g_fo[i].col = RGB(0x40+ss_rand()%0xC0, 0x40+ss_rand()%0xC0, 0x40+ss_rand()%0xC0);
        }
        g_ss_init = 1; }
}
void wm_screensaver_stop(void) { g_ss_active = 0; }
int  wm_screensaver_active(void) { return g_ss_active; }
int  wm_screensaver_tick(void) {
    if (!g_ss_active) return 0;
    int W = (int)fb_width(), H = (int)fb_height();
    if (g_ss_type == 3) { pipes_tick(W, H); return 1; }    /* Pipes: accumulates, no clear */
    fb_fill_rect(0, 0, W, H, RGB(0, 0, 0));
    if (g_ss_type == 0) return 1;                          /* Blank Screen */
    if (g_ss_type == 4) {                                  /* Flying Windows */
        int cx = W / 2, cy = H / 2;
        for (int i = 0; i < SS_STARS; i++) {
            g_star[i].z -= 10; if (g_star[i].z <= 1) { g_star[i].x = (int)(ss_rand()%2000)-1000; g_star[i].y = (int)(ss_rand()%2000)-1000; g_star[i].z = 1000; }
            int sx = cx + g_star[i].x * 256 / g_star[i].z, sy = cy + g_star[i].y * 256 / g_star[i].z;
            int s = 6000 / g_star[i].z; if (s < 2) s = 2; if (s > 44) s = 44;
            if (sx < -s || sx >= W || sy < -s || sy >= H) continue;
            flag_glyph(sx, sy, s);
        }
        return 1;
    }
    if (g_ss_type == 2) {                                  /* Starfield Simulation */
        int cx = W / 2, cy = H / 2;
        for (int i = 0; i < SS_STARS; i++) {
            g_star[i].z -= 12; if (g_star[i].z <= 1) { g_star[i].x = (int)(ss_rand() % 2000) - 1000; g_star[i].y = (int)(ss_rand() % 2000) - 1000; g_star[i].z = 1000; }
            int sx = cx + g_star[i].x * 256 / g_star[i].z;
            int sy = cy + g_star[i].y * 256 / g_star[i].z;
            if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;
            int b = 255 - g_star[i].z / 4; if (b < 40) b = 40; if (b > 255) b = 255;
            u32 c = RGB(b, b, b);
            fb_pixel(sx, sy, c);
            if (g_star[i].z < 400) { fb_pixel(sx+1, sy, c); fb_pixel(sx, sy+1, c); }   /* nearer = bigger */
        }
        return 1;
    }
    if (g_ss_type == 5) {                                  /* Scrolling Marquee */
        static int mx = -999999, my = 120, mdy = 2; static unsigned mc = 0;
        const char *msg = "Debian95 Desktop Edition";
        int tw = (int)fb_measure(msg) * 2;
        if (mx < -tw || mx > W) mx = W;
        mx -= 7; my += mdy; if (my < 30 || my > H - 60) mdy = -mdy;
        u32 col = RGB(0x60 + mc % 0xA0, 0x80, 0xFF - mc % 0x90); mc += 5;
        fb_text2x_t((u32)mx, (u32)my, msg, col);
        return 1;
    }
    if (g_ss_type == 6) {                                  /* 3D Flying Objects (shaded balls) */
        int cx = W / 2, cy = H / 2;
        for (int i = 0; i < FO_N; i++) {
            g_fo[i].x += g_fo[i].dx; g_fo[i].y += g_fo[i].dy; g_fo[i].z += g_fo[i].dz;
            if (g_fo[i].x < -900 || g_fo[i].x > 900) g_fo[i].dx = -g_fo[i].dx;
            if (g_fo[i].y < -900 || g_fo[i].y > 900) g_fo[i].dy = -g_fo[i].dy;
            if (g_fo[i].z < 300  || g_fo[i].z > 1900) g_fo[i].dz = -g_fo[i].dz;
            int z = g_fo[i].z, sx = cx + g_fo[i].x * 512 / z, sy = cy + g_fo[i].y * 512 / z;
            int r = 9000 / z; if (r < 3) r = 3; if (r > 60) r = 60;
            ss_ball(sx, sy, r, g_fo[i].col);
        }
        return 1;
    }
    if (g_ss_type == 7) {                                  /* 3D Text (extruded, bouncing) */
        static int tx = 200, ty = 200, tdx = 4, tdy = 3;
        const char *txt = "Debian95";
        int tw = (int)fb_measure(txt) * 2, th = 32;
        tx += tdx; ty += tdy;
        if (tx < 0 || tx > W - tw) tdx = -tdx;
        if (ty < 0 || ty > H - th) tdy = -tdy;
        for (int k = 6; k > 0; k--) fb_text2x_t((u32)(tx - k), (u32)(ty - k), txt, RGB(0, 0x30 - k*3, 0x30 - k*3));  /* extrusion */
        fb_text2x_t((u32)tx, (u32)ty, txt, RGB(0x30, 0xE8, 0xE8));                                                   /* cyan face */
        return 1;
    }
    for (int i = 0; i < SS_PTS; i++) {                     /* Mystify Your Mind */
        g_ss_p[i].x += g_ss_p[i].dx; g_ss_p[i].y += g_ss_p[i].dy;
        if (g_ss_p[i].x < 0 || g_ss_p[i].x >= W) { g_ss_p[i].dx = -g_ss_p[i].dx; g_ss_p[i].x += g_ss_p[i].dx; }
        if (g_ss_p[i].y < 0 || g_ss_p[i].y >= H) { g_ss_p[i].dy = -g_ss_p[i].dy; g_ss_p[i].y += g_ss_p[i].dy; }
    }
    for (int i = 0; i < SS_PTS; i++) { g_ss_trail[g_ss_head][i*2] = g_ss_p[i].x; g_ss_trail[g_ss_head][i*2+1] = g_ss_p[i].y; }
    g_ss_head = (g_ss_head + 1) % SS_TRAILS;
    for (int t = 0; t < SS_TRAILS; t++) {
        int idx = (g_ss_head + t) % SS_TRAILS; u32 *v = g_ss_trail[idx];
        u32 col = RGB(0x10 + t*10, 0x40 + t*30, 0x80 + t*20);
        for (int i = 0; i < SS_PTS; i++) { int j = (i+1) % SS_PTS; ss_line((int)v[i*2], (int)v[i*2+1], (int)v[j*2], (int)v[j*2+1], col); }
    }
    return 1;
}

/* first-run setup (OOBE): the wizard shows on a clean branded screen; the desktop,
 * icons, taskbar and Start stay hidden until the user finishes, like real Windows. */
static int g_setup_on;
void wm_setup_begin(void) { g_setup_on = 1; }
void wm_setup_end(void)   { g_setup_on = 0; wm_boot_start(); }   /* reveal + populate the desktop */
int  wm_setup_active(void){ return g_setup_on; }

/* ---- shutdown sequence: the Win95 "shutting down" then "It is now safe to turn
 * off your computer" full-screen. The desktop loop draws wm_shutdown_draw() and,
 * once wm_shutdown_phase() reaches 2, performs the real poweroff/reboot. ---- */
static int g_sd_on, g_sd_restart; static u32 g_sd_t0;
#define SD_WAIT_MS 1800      /* "shutting down" hold */
#define SD_SAFE_MS 4500      /* "safe to turn off" hold before the real poweroff */
void wm_shutdown_begin(int restart) { g_sd_on = 1; g_sd_restart = restart; g_sd_t0 = wm_now_ms(); }
int  wm_shutdown_active(void)  { return g_sd_on; }
int  wm_shutdown_restart(void) { return g_sd_restart; }
int  wm_shutdown_phase(void) {
    u32 e = wm_now_ms() - g_sd_t0;
    if (e < SD_WAIT_MS) return 0;                 /* "shutting down" */
    if (g_sd_restart)   return 2;                 /* reboot: no safe-to-turn-off screen */
    if (e < SD_WAIT_MS + SD_SAFE_MS) return 1;    /* "safe to turn off" shown */
    return 2;                                     /* then power off */
}
static void sd_center2x(const char *s, int y, u32 fg) {
    int w = (int)fb_measure(s) * 2, x = ((int)fb_width() - w) / 2; if (x < 0) x = 0;
    fb_text2x_t(x, y, s, fg);
}
void wm_shutdown_draw(void) {
    int W = (int)fb_width(), H = (int)fb_height(), cy = H / 2, era = g_theme->era;
    if (wm_shutdown_phase() == 1) {               /* "it is now safe to turn off" */
        if (era == ERA_XP || era == ERA_AERO) {   /* XP/Vista/7: logo screen, not the DOS-amber one */
            if (era == ERA_XP) vgrad(0, 0, W, H, RGB(0x2E,0x5A,0x9E), RGB(0x12,0x30,0x60));
            else               vgrad(0, 0, W, H, RGB(0x10,0x1A,0x30), RGB(0x02,0x06,0x12));
            sd_center2x("Debian95",                      cy - 44, RGB(0xFF,0xFF,0xFF));
            sd_center2x("It is now safe to turn off",    cy + 2,  RGB(0xD8,0xE4,0xFF));
            sd_center2x("your computer.",                cy + 34, RGB(0xD8,0xE4,0xFF));
        } else {                                  /* 95/98/Flat: authentic amber on black */
            fb_clear(RGB(0x00,0x00,0x00));
            sd_center2x("It is now safe to turn off", cy - 28, RGB(0xFC,0xA8,0x00));
            sd_center2x("your computer.",            cy + 4,  RGB(0xFC,0xA8,0x00));
        }
    } else {                                      /* shutting down / rebooting : sky + white */
        for (int y = 0; y < H; y++) { int t = y * 255 / (H ? H : 1);
            fb_fill_rect(0, y, W, 1, RGB(0x0C + t/6, 0x30 + t/4, 0x74 + t/6)); }
        sd_center2x(g_sd_restart ? "Debian95 is restarting..." : "Debian95 is shutting down...",
                    cy - 12, RGB(0xFF,0xFF,0xFF));
    }
}

/* ---- desktop gadget sidebar (Vista-style, Win95-dressed): live clock + CPU /
 * memory / network meters down the right edge. Data refreshed ~1Hz. ---- */
#define GADGET_W 128
static int g_gadgets_on;
static struct lg_sysmon g_gm;     /* struct + sys_sysmon declared in ke.h */
void wm_toggle_gadgets(void) { g_gadgets_on = !g_gadgets_on; sys_set_setting("gadgets", g_gadgets_on ? "1" : "0"); }
void wm_set_gadgets(int on) { g_gadgets_on = on ? 1 : 0; }
int  wm_gadgets_tick(void) { if (!g_gadgets_on) return 0; sys_sysmon(&g_gm); return 1; }
static int g_putu(char *b, unsigned v) { char t[12]; int m = 0; if (!v) t[m++] = '0'; while (v) { t[m++] = (char)('0'+v%10); v/=10; } int k = 0; while (m) b[k++] = t[--m]; b[k] = 0; return k; }
static void gadget_meter(int x, int y, int w, int pct, u32 col) {
    fb_fill_rect(x, y, w, 10, C_WHITE); wm_bevel_sunken(x, y, w, 10);
    int fw = (w - 2) * pct / 100; if (fw < 0) fw = 0; if (fw > w - 2) fw = w - 2;
    fb_fill_rect(x + 1, y + 1, fw, 8, col);
}
static void draw_gadgets(void) {
    if (!g_gadgets_on) return;
    int W = (int)fb_width(), gx = W - GADGET_W, gh = (int)fb_height() - TB_H;
    fb_fill_rect(gx, 0, GADGET_W, gh, RGB(0x2A,0x2A,0x3A));          /* slate panel */
    fb_fill_rect(gx, 0, 1, gh, RGB(0x60,0x60,0x70));
    u32 fg = C_WHITE, dim = RGB(0xA8,0xA8,0xB8);
    int x = gx + 10, y = 14, mw = GADGET_W - 20;
    /* clock gadget */
    int hh = 0, mm = 0; rtc_time(&hh, &mm);
    char clk[8]; int k = 0; clk[k++] = (char)('0'+hh/10); clk[k++] = (char)('0'+hh%10); clk[k++] = ':'; clk[k++] = (char)('0'+mm/10); clk[k++] = (char)('0'+mm%10); clk[k] = 0;
    fb_text2x_t(x + 10, y, clk, fg); y += 30;
    { int yy, mo, dd, dow, h2, m2, s2; char d[20]; int j = 0;
      sys_date(&yy, &mo, &dd, &dow, &h2, &m2, &s2);
      static const char *DOW[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
      const char *dn = (dow >= 0 && dow < 7) ? DOW[dow] : "";
      for (const char *s = dn; *s && j < 14; s++) d[j++] = *s;
      d[j++] = ' '; j += g_putu(d + j, (unsigned)mo); d[j++] = '/'; j += g_putu(d + j, (unsigned)dd); d[j] = 0;
      fb_text_t(x + 14, y, d, dim); y += 18; }
    /* CPU gadget */
    { char b[8]; int j = g_putu(b, (unsigned)g_gm.cpu_pct); b[j++] = '%'; b[j] = 0;
      fb_text_t(x, y, "CPU", fg); fb_text_t(x + mw - (int)font_measure(b), y, b, dim); y += 13;
      gadget_meter(x, y, mw, g_gm.cpu_pct, RGB(0x40,0xC8,0x40)); y += 20; }
    /* memory gadget */
    { char b[8]; int j = g_putu(b, (unsigned)g_gm.mem_pct); b[j++] = '%'; b[j] = 0;
      fb_text_t(x, y, "Memory", fg); fb_text_t(x + mw - (int)font_measure(b), y, b, dim); y += 13;
      gadget_meter(x, y, mw, g_gm.mem_pct, RGB(0x40,0x88,0xE8)); y += 14;
      char mem[24]; int q = g_putu(mem, (unsigned)g_gm.mem_used_mb); mem[q++]='/'; q += g_putu(mem+q,(unsigned)g_gm.mem_total_mb); mem[q++]=' ';mem[q++]='M';mem[q++]='B';mem[q]=0;
      fb_text_t(x, y, mem, dim); y += 20; }
    /* network gadget */
    { fb_text_t(x, y, "Network", fg); y += 13;
      char n[24]; int q = 0; const char *p = "Dn ";
      for (; *p; p++) n[q++] = *p; q += g_putu(n+q, (unsigned)g_gm.net_rx_kbs); p = " KB/s"; for (; *p; p++) n[q++]=*p; n[q]=0;
      fb_text_t(x, y, n, dim); y += 13;
      q = 0; p = "Up "; for (; *p; p++) n[q++] = *p; q += g_putu(n+q, (unsigned)g_gm.net_tx_kbs); p = " KB/s"; for (; *p; p++) n[q++]=*p; n[q]=0;
      fb_text_t(x, y, n, dim); }
}

void wm_compose(void) {
    if (g_logon_on) { draw_logon(); return; }              /* modal: no desktop behind it */
    wm_load_icons();          /* pull shell32 icons once, when the FS is ready */
    draw_background();         /* the configured wallpaper always renders (even during setup) */
    if (!g_setup_on) draw_icons();
    else { g_nicon = 0; g_ntbtn = 0; }   /* OOBE: no desktop icons / no phantom hit rects */
    if (!g_setup_on) draw_gadgets();     /* right-edge gadget sidebar (windows draw over it) */

    struct Window *fv = front_visible();
    for (int i = 0; i < g_nz; i++)
        if (g_z[i]->visible && !g_z[i]->external) draw_window(g_z[i], g_z[i] == fv);

    if (!g_setup_on) {                                     /* hide the shell chrome during setup */
        draw_menu_dropdown();
        draw_taskbar();
        draw_startmenu();
        draw_volume();
        draw_popup();
    }
    draw_altswitch();                                   /* Alt+Tab cool-switch box */
    if (!g_setup_on) draw_tooltip();                    /* hover info-tip, above the chrome */
    if (g_band_on) band_draw(g_bx, g_by, g_bw, g_bh);   /* rubber-band over everything */
    anim_draw();                                        /* minimize/maximize zoom wireframe */
    if (g_marq_on) band_draw(g_marq_rx, g_marq_ry, g_marq_rw, g_marq_rh);   /* marquee box */
    /* NOTE: the cursor is NOT drawn here - it is composited onto the scanout by
     * wm_cursor_overlay() after fb_present(), so plain moves stay damage-limited. */
}

void desktop_show(void) {
    if (!fb_available()) return;
    fb_clear(C_DESKTOP);
    fb_text2x_t(12, 10, "Debian95", C_WHITE);
    draw_taskbar();
    draw_cursor();
    fb_present();
}

/* ---- interaction ---- */
static int hit_title(struct Window *w, int x, int y) {
    return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + 3 + WM_TITLEH;
}
static int hit_close(struct Window *w, int x, int y) {
    int bx = w->x + w->w - 3 - 18, by = w->y + 5;
    return x >= bx && x < bx + 16 && y >= by && y < by + 14;
}
static int hit_help(struct Window *w, int x, int y) {         /* the '?' caption button */
    if (!w->help) return 0;
    int bx = w->x + w->w - 3 - 18 - 51, by = w->y + 5;
    return x >= bx && x < bx + 16 && y >= by && y < by + 14;
}
static int hit_minimize(struct Window *w, int x, int y) {
    int bx = w->x + w->w - 3 - 18 - 34, by = w->y + 5;   /* the '_' title button */
    return x >= bx && x < bx + 16 && y >= by && y < by + 14;
}
static int hit_maximize(struct Window *w, int x, int y) {
    int bx = w->x + w->w - 3 - 18 - 17, by = w->y + 5;   /* the 'o' title button */
    return x >= bx && x < bx + 16 && y >= by && y < by + 14;
}
static int hit_caption_icon(struct Window *w, int x, int y) {   /* the top-left window icon */
    return x >= w->x + 3 && x < w->x + 20 && y >= w->y + 3 && y < w->y + 3 + WM_TITLEH;
}
static void wm_toggle_max(struct Window *w) {
    int ox = w->x, oy = w->y, ow = w->w, oh = w->h;       /* zoom from the current rect */
    if (!w->maxed) {                                      /* maximise: fill the work area */
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        w->x = 0; w->y = 0; w->w = (int)fb_width(); w->h = (int)fb_height() - TB_H;
        w->maxed = 1;
    } else {                                              /* restore */
        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
        w->maxed = 0;
    }
    wm_begin_zoom(ox, oy, ow, oh, w->x, w->y, w->w, w->h);
}
/* Win95 double-click: the same target clicked twice within the threshold
 * (adjustable from the Mouse applet's Double-click speed slider). */
void wm_set_dclick_ms(int ms) { g_dclick_ms = ms < 100 ? 100 : ms > 900 ? 900 : (u32)ms; }
/* blinking text caret (real Win95 ~530ms). Text fields draw their caret only
 * when this is on; the platform recomposes while wm_wants_caret() so it blinks. */
int wm_caret_on(void) { return ((wm_now_ms() / 530) & 1) == 0; }
static int is_dblclick(const void *tgt) {
    static const void *last; static u32 last_ms;
    u32 now = wm_now_ms();
    if (tgt == last && now - last_ms < g_dclick_ms) { last = 0; return 1; }
    last = tgt; last_ms = now; return 0;
}
static int hit_win(struct Window *w, int x, int y) {
    return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h;
}

static int hit_grip(struct Window *w, int x, int y) {
    return x >= w->x + w->w - 14 && x < w->x + w->w &&
           y >= w->y + w->h - 14 && y < w->y + w->h;
}

/* absolute pointer (a tablet, e.g. virtio-tablet / VMware / VBox integration):
 * set the cursor to (x,y) directly, then run the normal interaction logic. */
int wm_mouse_abs(int x, int y, u8 buttons) {
    g_cx = x; g_cy = y;
    if (g_cx < 0) g_cx = 0;
    if (g_cy < 0) g_cy = 0;
    if (g_cx > (int)fb_width() - 1)  g_cx = (int)fb_width() - 1;
    if (g_cy > (int)fb_height() - 1) g_cy = (int)fb_height() - 1;
    return wm_mouse(0, 0, buttons);
}
int wm_mouse(int dx, int dy, u8 buttons) {
    static struct Window *drag, *resize;
    static int dox, doy;
    static u8 prev;
    static int icon_drag = -1, icon_moved, idgx, idgy, icon_px0, icon_py0;   /* desktop-icon drag */
    static char icon_key[40];

    if (g_swap_btn) buttons = (u8)((buttons & ~3) | ((buttons & 1) << 1) | ((buttons & 2) >> 1));
    g_cx += dx; g_cy -= dy;
    if (g_cx < 0) g_cx = 0;
    if (g_cy < 0) g_cy = 0;
    if (g_cx > (int)fb_width() - 1)  g_cx = (int)fb_width() - 1;
    if (g_cy > (int)fb_height() - 1) g_cy = (int)fb_height() - 1;

    if (logon_is_active()) {                           /* logon modal owns all input */
        if ((buttons & 1) && !(prev & 1)) logon_click(g_cx, g_cy);
        prev = buttons;
        return buttons ? 1 : 2;
    }

    if (drag) {                                        /* move: rubber-band until release */
        int aerosnap = (g_theme->era == ERA_AERO);
        int sw = (int)fb_width(), shh = (int)fb_height() - TB_H;
        /* Aero Snap target under the cursor: top=maximise, left/right=half tile */
        int srx = -1, sry = 0, srw = 0, srh = shh;
        if (aerosnap) {
            if (g_cy <= 2)          { srx = 0;      srw = sw; }
            else if (g_cx <= 2)     { srx = 0;      srw = sw / 2; }
            else if (g_cx >= sw - 2){ srx = sw / 2; srw = sw - sw / 2; }
        }
        if (buttons & 1) {
            g_band_on = 1;
            if (srx >= 0) { g_bx = srx; g_by = sry; g_bw = srw; g_bh = srh; }   /* snap preview band */
            else          { g_bx = g_cx - dox; g_by = g_cy - doy; g_bw = drag->w; g_bh = drag->h; }
        } else {
            if (g_band_on) {
                if (srx >= 0) {                                /* commit the Aero snap */
                    if (!drag->maxed) { drag->rx = drag->x; drag->ry = drag->y; drag->rw = drag->w; drag->rh = drag->h; }
                    int ox = drag->x, oy = drag->y, ow = drag->w, oh = drag->h;
                    drag->x = srx; drag->y = sry; drag->w = srw; drag->h = srh; drag->maxed = 1;
                    wm_begin_zoom(ox, oy, ow, oh, drag->x, drag->y, drag->w, drag->h);
                } else { drag->x = g_bx; drag->y = g_by; }
            }
            drag = 0; g_band_on = 0;
        }
    }
    if (resize) {                                      /* size: rubber-band until release */
        if (buttons & 1) {
            int nw = g_cx - resize->x + 2, nh = g_cy - resize->y + 2;
            if (nw < 160) nw = 160; if (nh < 90) nh = 90;
            g_band_on = 1; g_bx = resize->x; g_by = resize->y; g_bw = nw; g_bh = nh;
        } else { if (g_band_on) { resize->w = g_bw; resize->h = g_bh; } resize = 0; g_band_on = 0; }
    }

    /* ongoing desktop-icon drag: move the icon live, persist its position on drop */
    if (icon_drag >= 0) {
        if (buttons & 1) {
            int nx = g_cx - idgx, ny = g_cy - idgy;
            if (nx < 0) nx = 0; if (ny < 40) ny = 40;
            if (nx > (int)fb_width() - 48) nx = (int)fb_width() - 48;
            if (ny > (int)fb_height() - TB_H - 48) ny = (int)fb_height() - TB_H - 48;
            if ((g_cx - icon_px0) * (g_cx - icon_px0) + (g_cy - icon_py0) * (g_cy - icon_py0) > 16) icon_moved = 1;
            if (icon_moved) {
                int oi = ipos_find(icon_key);
                if (oi < 0 && g_niover < 24) { oi = g_niover++; int j = 0; for (; icon_key[j] && j < 39; j++) g_iover[oi].key[j] = icon_key[j]; g_iover[oi].key[j] = 0; }
                if (oi >= 0) { g_iover[oi].x = nx; g_iover[oi].y = ny; }
            }
            prev = buttons; return icon_moved ? 1 : 0;
        }
        if (icon_moved) {
            int oi = ipos_find(icon_key);        /* snap the drop to the nearest 75px grid
                                                  * cell (Win95 "Align to Grid") so the desktop
                                                  * stays tidy; holes are allowed. */
            if (oi >= 0) {
                int col = (g_iover[oi].x - ICON_LEFT + ICON_CELL_W / 2) / ICON_CELL_W;
                int row = (g_iover[oi].y - ICON_TOP  + ICON_CELL_H / 2) / ICON_CELL_H;
                if (col < 0) col = 0; if (row < 0) row = 0;
                g_iover[oi].x = ICON_LEFT + col * ICON_CELL_W;
                g_iover[oi].y = ICON_TOP  + row * ICON_CELL_H;
            }
            ipos_save();
        }
        icon_drag = -1; icon_moved = 0; prev = buttons; return 1;
    }

    /* desktop rubber-band (marquee) multi-select: drag on empty desktop to select
     * every icon the box touches; release keeps the selection. */
    if (g_marq_on) {
        if (buttons & 1) {
            int x0 = g_marq_x0 < g_cx ? g_marq_x0 : g_cx, x1 = g_marq_x0 > g_cx ? g_marq_x0 : g_cx;
            int y0 = g_marq_y0 < g_cy ? g_marq_y0 : g_cy, y1 = g_marq_y0 > g_cy ? g_marq_y0 : g_cy;
            g_marq_rx = x0; g_marq_ry = y0; g_marq_rw = x1 - x0; g_marq_rh = y1 - y0;
            g_iselmask = 0;
            for (int i = 0; i < g_nicon && i < 32; i++)
                if (g_icon[i].x < x1 && g_icon[i].x + g_icon[i].w > x0 &&
                    g_icon[i].y < y1 && g_icon[i].y + g_icon[i].h > y0) g_iselmask |= (1u << i);
            prev = buttons; return 1;
        }
        g_marq_on = 0; prev = buttons; return 1;        /* release: keep the selection */
    }

    /* right-click opens a context menu */
    if ((buttons & 2) && !(prev & 2) && !g_pop_open) { context_rclick(); prev = buttons; return 1; }
    /* a click while a context menu is open picks an item (or dismisses) */
    if ((buttons & 1) && !(prev & 1) && g_pop_open) { context_pick(); prev = buttons; return 1; }
    /* menu-bar interaction (open dropdown, pick an item) takes priority */
    if ((buttons & 1) && !(prev & 1) && menubar_click()) { prev = buttons; return 1; }
    /* tray volume: click the speaker or interact with an open popup */
    if ((buttons & 1) && !(prev & 1) && vol_click(g_cx, g_cy)) { prev = buttons; return 1; }
    if ((buttons & 1) && !(prev & 1) && clock_click(g_cx, g_cy)) { prev = buttons; return 1; }
    if ((buttons & 1) && !(prev & 1) && sru_click(g_cx, g_cy)) { prev = buttons; return 1; }

    if ((buttons & 1) && !(prev & 1)) {
        if (g_tb_address) {                          /* Address deskband: click focuses the field, elsewhere defocuses */
            int on_box = (g_addr_bw > 0 && g_cx >= g_addr_bx && g_cx < g_addr_bx + g_addr_bw
                          && g_cy >= g_addr_by && g_cy < g_addr_by + 20);
            g_addr_active = on_box;
            if (on_box) { prev = buttons; return 1; }
        }
        if (start_hit(g_cx, g_cy)) {
            g_start_open = !g_start_open;
            g_sm_path[0] = g_sm_path[1] = g_sm_path[2] = -1;
        } else if (g_start_open) {
            /* click in the (cascading) menu: open a submenu, run a leaf, else close */
            int hit = -1;
            for (int i = 0; i < g_nmi; i++)
                if (g_cx >= g_mi[i].x && g_cx < g_mi[i].x + g_mi[i].w &&
                    g_cy >= g_mi[i].y && g_cy < g_mi[i].y + g_mi[i].h) { hit = i; break; }
            if (hit >= 0 && g_mi[hit].is_sub) {
                g_sm_path[g_mi[hit].level] = g_mi[hit].idx;
                for (int d = g_mi[hit].level + 1; d < 3; d++) g_sm_path[d] = -1;
            } else if (hit >= 0 && g_mi[hit].act >= MRU_ACT_BASE && g_mi[hit].act < MRU_ACT_BASE + MRU_MAX) {
                mru_open(g_mi[hit].act - MRU_ACT_BASE);       /* open a recent document */
                g_start_open = 0; g_sm_path[0] = g_sm_path[1] = g_sm_path[2] = -1;
            } else if (hit >= 0 && g_mi[hit].act > 0) {
                g_action = g_mi[hit].act;
                g_start_open = 0; g_sm_path[0] = g_sm_path[1] = g_sm_path[2] = -1;
            } else {
                g_start_open = 0; g_sm_path[0] = g_sm_path[1] = g_sm_path[2] = -1;
            }
        } else {
            int handled = 0;
            /* Win7 Show Desktop peek corner (far-right of the superbar) */
            if (g_peek_x && g_cx >= g_peek_x && g_cy >= (int)fb_height() - TB_H) { ql_show_desktop(); handled = 1; }
            /* Links deskband buttons: launch a link in the browser, or open the editor */
            for (int i = 0; i < g_nlkbtn && !handled; i++)
                if (g_cx >= g_lkbtn_x[i] && g_cx < g_lkbtn_x[i] + g_lkbtn_w[i] &&
                    g_cy >= g_lkbtn_y && g_cy < g_lkbtn_y + g_lkbtn_h) {
                    if (g_lkbtn_act[i] == -1) { extern void links_manage_open(void); links_manage_open(); }
                    else if (!app_launch_doc(28, g_lk_url[g_lkbtn_act[i]])) wm_post_action(28);
                    handled = 1;
                }
            /* Quick Launch: Show Desktop toggles minimise-all; launchers post an action */
            for (int i = 0; i < g_nql && !handled; i++)
                if (g_cx >= g_ql[i].x && g_cx < g_ql[i].x + g_ql[i].w &&
                    g_cy >= g_ql[i].y && g_cy < g_ql[i].y + g_ql[i].h) {
                    if (g_ql[i].act == -20) ql_show_desktop(); else g_action = g_ql[i].act;
                    handled = 1;
                }
            /* taskbar buttons restore/focus their window */
            for (int i = 0; i < g_ntbtn && !handled; i++) {
                if (g_cx >= g_tbtn[i].x && g_cx < g_tbtn[i].x + g_tbtn[i].w &&
                    g_cy >= g_tbtn[i].y && g_cy < g_tbtn[i].y + g_tbtn[i].h) {
                    struct Window *tw = g_tbtn[i].win;
                    if (tw->external) {                                    /* foreign X app: "go to that app" */
                        tw->visible = 1; tw->minimized = 0; ext_push(tw, 1);   /* raise/activate (restore if hidden) */
                    } else if (tw->visible && tw == front_visible()) { zoom_to_taskbar(tw); tw->visible = 0; tw->minimized = 1; }  /* active -> minimize */
                    else { int wasmin = tw->minimized; tw->visible = 1; tw->minimized = 0; bring_front(tw); if (wasmin) zoom_from_taskbar(tw); }  /* else restore/raise */
                    handled = 1; break;
                }
            }
            if (!handled) {
                if (g_start_open) g_start_open = 0;
                for (int i = g_nz - 1; i >= 0 && !handled; i--) {
                    struct Window *w = g_z[i];
                    if (!w->visible || !hit_win(w, g_cx, g_cy)) continue;
                    bring_front(w); g_sel_icon = -1; g_iselmask = 0;   /* clicking a window clears icon selection */
                    if (hit_caption_icon(w, g_cx, g_cy)) { open_sysmenu_for(w); handled = 1; break; }  /* caption icon: system menu */
                    if (hit_help(w, g_cx, g_cy)) { wm_messagebox(w->title, w->help); handled = 1; break; }  /* ?: context help */
                    else if (hit_close(w, g_cx, g_cy)) { wm_destroy(w); handled = 1; break; }  /* X: terminate */
                    else if (hit_minimize(w, g_cx, g_cy)) { zoom_to_taskbar(w); w->minimized = 1; w->visible = 0; }  /* _: to taskbar */
                    else if (hit_maximize(w, g_cx, g_cy)) wm_toggle_max(w);           /* o: maximise/restore */
                    else if (hit_grip(w, g_cx, g_cy) && !w->maxed) resize = w;
                    else if (hit_title(w, g_cx, g_cy)) {                                /* dbl-click title = maximise */
                        if (is_dblclick(w)) { wm_toggle_max(w); drag = 0; }
                        else if (!w->maxed) { drag = w; dox = g_cx - w->x; doy = g_cy - w->y; }
                    }
                    else if (w->onclick) {
                        int cx, cy, cw, ch; wm_client_rect(w, &cx, &cy, &cw, &ch);
                        w->onclick(g_cx - cx, g_cy - cy);
                    }
                    handled = 1;
                }
                if (!handled) {   /* bare desktop: single-click selects an icon, a
                                     click on the already-selected icon opens it */
                    if (g_ren_active) ren_commit();                                    /* clicking away commits a rename */
                    int hit = -1;
                    for (int i = 0; i < g_nicon; i++)
                        if (g_cx >= g_icon[i].x && g_cx < g_icon[i].x + g_icon[i].w &&
                            g_cy >= g_icon[i].y && g_cy < g_icon[i].y + g_icon[i].h) { hit = i; break; }
                    if (hit < 0) { g_sel_icon = -1; g_iselmask = 0;                    /* deselect + start marquee */
                                   g_marq_on = 1; g_marq_x0 = g_cx; g_marq_y0 = g_cy;
                                   g_marq_rx = g_cx; g_marq_ry = g_cy; g_marq_rw = 0; g_marq_rh = 0; }
                    else {                                                             /* select; double-click opens */
                        g_sel_icon = hit;
                        if (!(g_iselmask & (1u << hit))) g_iselmask = 0;               /* plain click -> single select */
                        icon_drag = hit; icon_moved = 0;                                /* arm a possible drag */
                        idgx = g_cx - g_icon[hit].x; idgy = g_cy - g_icon[hit].y;
                        icon_px0 = g_cx; icon_py0 = g_cy;
                        { int j = 0; for (; g_icon[hit].key[j] && j < 39; j++) icon_key[j] = g_icon[hit].key[j]; icon_key[j] = 0; }
                        if (is_dblclick(&g_icon[hit])) {
                            int u = g_icon[hit].uidx;
                            if (u < 0) g_action = g_icon[hit].act;                     /* fixed shortcut */
                            else { open_user_item(u); }                                /* desktop document/folder */
                            g_sel_icon = -1;
                        }
                    }
                }
            }
        }
    }
    u8 had = prev;
    prev = buttons;

    /* deliver motion to the focused window (used by Paint) */
    struct Window *fw = front_visible();
    if (fw && fw->onmotion) {
        int cx, cy, cw, ch; wm_client_rect(fw, &cx, &cy, &cw, &ch);
        fw->onmotion(g_cx - cx, g_cy - cy, buttons);
    }
    /* return 2 for a pure cursor move (no content changed) so the platform can
     * do a damage-limited cursor update instead of a full recompose+present. */
    if (buttons || had) return 1;
    if (drag || resize || g_band_on) return 1;
    if (g_start_open || g_pop_open || g_vol_open || (g_menu_win && g_menu_open >= 0)) return 1;
    if (fw && fw->onmotion) return 1;          /* app tracks motion (Paint) -> full */
    return 2;
}
