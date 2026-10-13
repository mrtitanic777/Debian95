/* Debian95 — ComCtl32-style common controls (clean-room, classic 3D theme). */
#include <debian95/comctl.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/rtl.h>
#include <debian95/theme.h>

/* control colours from the active theme (theme.c) */
#define FACE   (g_theme->face)
#define HI     (g_theme->white)
#define SH     (g_theme->shadow)
#define DK     (g_theme->black)
#define WHITE  (g_theme->white)
#define GRAYTX (g_theme->gray_text)
#define SELBLU (g_theme->sel)

static int th(void) { return (int)font_height(); }

/* classic property-sheet tab strip across the top of a page; the active tab sits
 * a touch higher/taller so it "connects" to the page body the caller draws. */
void cc_tabs(int x, int y, int tabw, const char **labels, int n, int active) {
    int h = th() + 8;
    for (int i = 0; i < n; i++) {
        int tx = x + i * tabw, a = (i == active);
        int ty = a ? y : y + 3, hh = a ? h + 3 : h;
        fb_fill_rect(tx, ty, tabw, hh, FACE);
        fb_fill_rect(tx, ty, tabw, 1, HI);                 /* top edge     */
        fb_fill_rect(tx, ty, 1, hh, HI);                   /* left edge    */
        fb_fill_rect(tx + tabw - 1, ty, 1, hh, DK);        /* right edge   */
        int tw = (int)font_measure(labels[i]);
        fb_text_t(tx + (tabw - tw) / 2, ty + 4, labels[i], DK);
    }
}
/* which tab a click at (mx,my) hit, or -1 */
int cc_tab_hit(int x, int y, int tabw, int n, int mx, int my) {
    if (my < y || my > y + th() + 11) return -1;
    int i = (mx - x) / tabw;
    return (mx >= x && i >= 0 && i < n) ? i : -1;
}

/* variable-width tabs: each sized to its label + padding (like real Win95, so a
 * long caption is never crowded against the divider). Returns the total width. */
static int tabw_of(const char *label) { int w = (int)font_measure(label) + 20; return w < 36 ? 36 : w; }
int cc_tabs_var(int x, int y, const char **labels, int n, int active) {
    int h = th() + 8, tx = x;
    for (int i = 0; i < n; i++) {
        int tw = tabw_of(labels[i]), a = (i == active), ty = a ? y : y + 3, hh = a ? h + 3 : h;
        fb_fill_rect(tx, ty, tw, hh, FACE);
        fb_fill_rect(tx, ty, tw, 1, HI);
        fb_fill_rect(tx, ty, 1, hh, HI);
        fb_fill_rect(tx + tw - 1, ty, 1, hh, DK);
        int lw = (int)font_measure(labels[i]);
        fb_text_t(tx + (tw - lw) / 2, ty + 4, labels[i], DK);
        tx += tw;
    }
    return tx - x;
}
int cc_tab_hit_var(int x, int y, const char **labels, int n, int mx, int my) {
    if (my < y || my > y + th() + 11) return -1;
    int tx = x;
    for (int i = 0; i < n; i++) { int tw = tabw_of(labels[i]); if (mx >= tx && mx < tx + tw) return i; tx += tw; }
    return -1;
}

/* Win95 focus rectangle: a 1px dotted black rectangle inset into the control. */
static void focus_rect(int x, int y, int w, int h) {
    for (int i = 0; i < w; i += 2) { fb_pixel(x + i, y, DK); fb_pixel(x + i, y + h - 1, DK); }
    for (int j = 0; j < h; j += 2) { fb_pixel(x, y + j, DK); fb_pixel(x + w - 1, y + j, DK); }
}

static u32 cc_mix(u32 a, u32 b, int t) {
    int r=(((a>>16)&0xff)*(255-t)+((b>>16)&0xff)*t)/255;
    int g=(((a>>8)&0xff)*(255-t)+((b>>8)&0xff)*t)/255;
    int bl=((a&0xff)*(255-t)+(b&0xff)*t)/255; return RGB(r,g,bl);
}
/* A push button. `pressed` sinks it; `focused` adds the dotted focus rectangle. */
void cc_button_ex(int x, int y, int w, int h, const char *text, int pressed, int focused) {
    int tw = (int)font_measure(text);
    int tx = x + (w - tw) / 2, ty = y + (h - th()) / 2;

    if (g_theme->style == STYLE_XP) {                 /* Luna: glossy vertical gradient, rounded */
        u32 top = pressed ? g_theme->shadow : g_theme->white;
        u32 bot = pressed ? g_theme->face : g_theme->face;
        for (int j = 0; j < h; j++) fb_fill_rect(x, y + j, w, 1, cc_mix(top, bot, h>1?(j*255)/(h-1):0));
        fb_rect(x, y, w, h, focused ? g_theme->accent : g_theme->shadow);
        fb_pixel(x,y,FACE); fb_pixel(x+w-1,y,FACE); fb_pixel(x,y+h-1,FACE); fb_pixel(x+w-1,y+h-1,FACE);
        fb_text_t(tx + (pressed?1:0), ty + (pressed?1:0), text, DK);
        return;
    }
    if (g_theme->style == STYLE_FLAT) {               /* flat: fill + 1px border, accent when pressed */
        fb_fill_rect(x, y, w, h, pressed ? g_theme->accent : FACE);
        fb_rect(x, y, w, h, focused ? g_theme->accent : SH);
        fb_text_t(tx, ty, text, pressed ? WHITE : DK);
        return;
    }
    /* classic 95/98 */
    fb_fill_rect(x, y, w, h, FACE);
    if (pressed) wm_bevel_sunken(x, y, w, h); else wm_bevel_raised(x, y, w, h);
    fb_text_t(tx + (pressed ? 1 : 0), ty + (pressed ? 1 : 0), text, DK);
    if (focused) focus_rect(x + 4, y + 4, w - 8, h - 8);
}
void cc_button(int x, int y, int w, int h, const char *text, int pressed) {
    cc_button_ex(x, y, w, h, text, pressed, 0);
}

/* The default push button: a 1px black frame around a focused raised button
 * (the OK/Yes button in a Win95 dialog). */
void cc_defbutton(int x, int y, int w, int h, const char *text, int pressed) {
    fb_rect(x, y, w, h, DK);
    cc_button_ex(x + 1, y + 1, w - 2, h - 2, text, pressed, 1);
}

void cc_checkbox(int x, int y, const char *text, int checked) {
    int b = 13;
    fb_fill_rect(x, y, b, b, WHITE);
    wm_bevel_sunken(x, y, b, b);
    if (checked) {                          /* bold check mark */
        for (int i = 0; i < 2; i++) {
            fb_pixel(x + 3, y + 5 + i, DK); fb_pixel(x + 4, y + 6 + i, DK);
            fb_pixel(x + 5, y + 7 + i, DK); fb_pixel(x + 6, y + 6 + i, DK);
            fb_pixel(x + 7, y + 5 + i, DK); fb_pixel(x + 8, y + 4 + i, DK);
            fb_pixel(x + 9, y + 3 + i, DK);
        }
    }
    fb_text_t(x + b + 6, y + (b - th()) / 2, text, DK);
}

void cc_radio(int x, int y, const char *text, int on) {
    int d = 12; int cx = x + d / 2, cy = y + d / 2;
    for (int yy = 0; yy < d; yy++)          /* white disk + dark ring */
        for (int xx = 0; xx < d; xx++) {
            int dx = xx - d / 2, dy = yy - d / 2, r2 = dx * dx + dy * dy;
            if (r2 <= 30) fb_pixel(x + xx, y + yy, WHITE);
            else if (r2 <= 40) fb_pixel(x + xx, y + yy, SH);
        }
    /* upper-left shadow arc for a slight 3D bevel */
    fb_pixel(cx - 3, cy - 2, SH); fb_pixel(cx - 2, cy - 3, SH);
    fb_pixel(cx + 2, cy + 3, HI); fb_pixel(cx + 3, cy + 2, HI);
    if (on)                                 /* filled center dot */
        for (int yy = -2; yy <= 2; yy++)
            for (int xx = -2; xx <= 2; xx++)
                if (xx * xx + yy * yy <= 5) fb_pixel(cx + xx, cy + yy, DK);
    fb_text_t(x + d + 6, y + (d - th()) / 2, text, DK);
}

/* etched (engraved) frame with a label breaking the top border */
void cc_groupbox(int x, int y, int w, int h, const char *text) {
    int top = y + th() / 2;
    /* engraved rectangle: shadow line then hilight line offset by 1px */
    fb_fill_rect(x, top, w, 1, SH);         fb_fill_rect(x, top + 1, w, 1, HI);
    fb_fill_rect(x, top, 1, h - (top - y), SH); fb_fill_rect(x + 1, top + 1, 1, h - (top - y) - 2, HI);
    fb_fill_rect(x + w - 1, top, 1, h - (top - y), HI); fb_fill_rect(x + w - 2, top + 1, 1, h - (top - y) - 2, SH);
    fb_fill_rect(x, y + h - 1, w, 1, HI);   fb_fill_rect(x, y + h - 2, w, 1, SH);
    if (text && text[0]) {                  /* label over a gray gap */
        int tw = (int)font_measure(text);
        fb_fill_rect(x + 8, y, tw + 6, th(), FACE);
        fb_text_t(x + 11, y, text, DK);
    }
}

void cc_label(int x, int y, const char *text) { fb_text_t(x, y, text, DK); }

void cc_edit(int x, int y, int w, int h, const char *text, int focused) {
    fb_fill_rect(x, y, w, h, WHITE);
    wm_bevel_sunken(x, y, w, h);
    fb_text_t(x + 4, y + (h - th()) / 2, text, DK);
    if (focused) fb_fill_rect(x + 4 + (int)font_measure(text) + 1, y + 3, 1, h - 6, DK);  /* caret */
}

/* Win95 vertical scroll bar: up/down arrow buttons, a dithered gray track, and
 * a raised thumb. thumb_top/thumb_h are within the track (between the arrows). */
#define CC_SBW 16
void cc_scrollbar_v(int x, int y, int h, int thumb_top, int thumb_h) {
    int t0 = y + CC_SBW, t1 = y + h - CC_SBW;            /* track span */
    /* dithered gray track (COLOR_SCROLLBAR: 50% white/face checker) */
    for (int j = t0; j < t1; j++)
        for (int i = x; i < x + CC_SBW; i++)
            fb_pixel(i, j, ((i + j) & 1) ? HI : FACE);
    /* up arrow button */
    fb_fill_rect(x, y, CC_SBW, CC_SBW, FACE); wm_bevel_raised(x, y, CC_SBW, CC_SBW);
    for (int i = 0; i < 4; i++) fb_fill_rect(x + 8 - i, y + 5 + i, 2 * i + 1, 1, DK);
    /* down arrow button */
    int dy = y + h - CC_SBW;
    fb_fill_rect(x, dy, CC_SBW, CC_SBW, FACE); wm_bevel_raised(x, dy, CC_SBW, CC_SBW);
    for (int i = 0; i < 4; i++) fb_fill_rect(x + 5 + i, dy + 5 + i, 2 * (3 - i) + 1, 1, DK);
    /* thumb */
    if (thumb_h < 8) thumb_h = 8;
    if (t0 + thumb_top + thumb_h > t1) thumb_top = t1 - t0 - thumb_h;
    if (thumb_top < 0) thumb_top = 0;
    fb_fill_rect(x, t0 + thumb_top, CC_SBW, thumb_h, FACE);
    wm_bevel_raised(x, t0 + thumb_top, CC_SBW, thumb_h);
}

void cc_progress(int x, int y, int w, int h, int pct) {
    fb_fill_rect(x, y, w, h, WHITE);
    wm_bevel_sunken(x, y, w, h);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    int fillw = (w - 4) * pct / 100;
    for (int i = 0; i + 2 < fillw; i += 8)   /* segmented blocks, classic look */
        fb_fill_rect(x + 2 + i, y + 2, 6, h - 4, SELBLU);
}

/* ---------------- the common-controls demo dialog ---------------- */
static Window *g_dlg;
static int g_chk[3] = { 1, 0, 1 };
static int g_radio = 0;
static int g_prog = 60;

/* layout (client-relative) — paint and hit-test share these */
#define MARGIN  10
#define CK_X    24
#define CK_Y0   30      /* first checkbox */
#define GAP     8
#define ROW     20
#define GB2_Y   96      /* Theme group top */
#define RD_Y0  118      /* first radio */
#define NAME_Y 188
#define INST_Y 214
#define BTN_W   76
#define BTN_H   25

static const char *CK_TXT[3] = { "Enable sound", "Show the clock", "Full screen" };
static const char *RD_TXT[3] = { "Classic", "Blue", "High contrast" };

/* buttons are anchored to the bottom-right of the client area */
static void btn_geom(int cw, int ch, int *okx, int *cnx, int *by) {
    *by  = ch - BTN_H - MARGIN;
    *cnx = cw - BTN_W - MARGIN;
    *okx = *cnx - BTN_W - GAP;
}

static void dlg_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);

    cc_groupbox(cx + MARGIN, cy + 8, cw - 2 * MARGIN, 78, "Options");
    for (int i = 0; i < 3; i++)
        cc_checkbox(cx + CK_X, cy + CK_Y0 + i * ROW, CK_TXT[i], g_chk[i]);

    cc_groupbox(cx + MARGIN, cy + GB2_Y, cw - 2 * MARGIN, 82, "Theme");
    for (int i = 0; i < 3; i++)
        cc_radio(cx + CK_X, cy + RD_Y0 + i * ROW, RD_TXT[i], g_radio == i);

    cc_label(cx + 12, cy + NAME_Y + 3, "Name:");
    cc_edit(cx + 56, cy + NAME_Y, cw - 56 - MARGIN, 20, "Debian95 User", 1);

    cc_label(cx + 12, cy + INST_Y + 2, "Install:");
    cc_progress(cx + 56, cy + INST_Y, cw - 56 - MARGIN, 16, g_prog);

    int okx, cnx, by; btn_geom(cw, ch, &okx, &cnx, &by);
    cc_defbutton(cx + okx, cy + by, BTN_W, BTN_H, "OK", 0);
    cc_button(cx + cnx, cy + by, BTN_W, BTN_H, "Cancel", 0);
}

static int hit(int rx, int ry, int x, int y, int w, int h) {
    return rx >= x && rx < x + w && ry >= y && ry < y + h;
}

static void dlg_click(int rx, int ry) {
    int cx, cy, cw, ch; wm_client_rect(g_dlg, &cx, &cy, &cw, &ch);
    for (int i = 0; i < 3; i++)
        if (hit(rx, ry, CK_X, CK_Y0 + i * ROW - 2, cw - CK_X - MARGIN, 16)) { g_chk[i] = !g_chk[i]; return; }
    for (int i = 0; i < 3; i++)
        if (hit(rx, ry, CK_X, RD_Y0 + i * ROW - 2, cw - CK_X - MARGIN, 16)) { g_radio = i; return; }
    int okx, cnx, by; btn_geom(cw, ch, &okx, &cnx, &by);
    if (hit(rx, ry, okx, by, BTN_W, BTN_H)) { g_prog += 10; if (g_prog > 100) g_prog = 0; return; }
    if (hit(rx, ry, cnx, by, BTN_W, BTN_H)) { if (g_dlg) wm_hide(g_dlg); return; }
}

void controls_open(void) {
    if (!g_dlg)
        g_dlg = wm_add_custom(300, 110, 300, 330, "Debian95 Controls", dlg_paint, 0, dlg_click);
    wm_show(g_dlg);
}
