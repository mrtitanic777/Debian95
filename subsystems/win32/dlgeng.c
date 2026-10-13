/* Debian95 — declarative dialog + property-sheet engine. Controls are DATA
 * (a clean-room DLGITEMTEMPLATE / .rc model): ONE flattened control array drives
 * paint, hit-testing and input, so "draw" and "click" coordinates can never drift
 * apart. The engine owns layout (optionally in Dialog Units, like a real .rc),
 * painting, selection, scrollbars, the default push button, Esc-cancel, Tab focus,
 * the property-sheet tab strip and its OK/Cancel/Apply row. Apps only supply a
 * template + a DlgProc and fill/read control state by id. A property sheet is the
 * same mechanism with items tagged by page (ids unique across the sheet) and a
 * per-page DlgProc; pages use the sheet HWND for all DlgItem calls. */
#include <debian95/win32.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/mm.h>

#define MAXCTL     64
#define MAXPAGE     8
#define LV_MAXCOL   4
#define LIST_MAXROW 512
#define SBW        13

/* Authentic Win95 vertical scroll bar: raised arrow buttons with PIXEL-drawn
 * triangles (never font glyphs, which a loaded proportional font may lack), a
 * dithered white/face track, and a raised thumb. sy..sy+sh spans the whole bar;
 * thumb_y/thumb_h are absolute within it. */
static void dlg_sb_v(int sx, int sy, int sh, int thumb_y, int thumb_h) {
    for (int j = sy + SBW; j < sy + sh - SBW; j++)                 /* dithered track */
        for (int i = sx; i < sx + SBW; i++)
            fb_pixel(i, j, ((i + j) & 1) ? RGB(0xFF,0xFF,0xFF) : RGB(0xC0,0xC0,0xC0));
    fb_fill_rect(sx, sy, SBW, SBW, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(sx, sy, SBW, SBW);   /* up button */
    { int cx = sx + SBW/2, cy = sy + SBW/2;
      for (int i = 0; i < 4; i++) fb_fill_rect(cx - i, cy + i - 2, 2*i + 1, 1, RGB(0,0,0)); }
    int dy = sy + sh - SBW;                                        /* down button */
    fb_fill_rect(sx, dy, SBW, SBW, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(sx, dy, SBW, SBW);
    { int cx = sx + SBW/2, cy = dy + SBW/2;
      for (int i = 0; i < 4; i++) fb_fill_rect(cx - i, cy + 2 - i, 2*i + 1, 1, RGB(0,0,0)); }
    if (thumb_h > 0) {                                             /* raised thumb */
        fb_fill_rect(sx, thumb_y, SBW, thumb_h, RGB(0xC0,0xC0,0xC0));
        wm_bevel_raised(sx, thumb_y, SBW, thumb_h);
    }
}

typedef struct {
    int  ncol; char title[LV_MAXCOL][20]; int colw[LV_MAXCOL];
    int  header, nrow, sel, top;
    char cell[LIST_MAXROW][LV_MAXCOL][40];
} ListCtl;

#define TREE_MAXNODE 256
typedef struct {
    char text[TREE_MAXNODE][40];
    unsigned char depth[TREE_MAXNODE], expandable[TREE_MAXNODE], expanded[TREE_MAXNODE];
    int n, sel, top;
} TreeCtl;

typedef struct {
    int   is_sheet;
    DLGITEM items[MAXCTL]; int nitems; int itempage[MAXCTL];
    int   dlu, bx, by;
    /* property-sheet fields */
    int   npages, active; const char *tabtitle[MAXPAGE]; WNDPROC pageproc[MAXPAGE];
    const char *caption;
    /* plain-dialog proc (is_sheet == 0) */
    WNDPROC proc;
    /* per-control state */
    int   checked[MAXCTL], enabled[MAXCTL], len[MAXCTL];
    int   tbpos[MAXCTL], tbmin[MAXCTL], tbmax[MAXCTL];   /* trackbars */
    int   caly[MAXCTL], calm[MAXCTL], cald[MAXCTL];      /* month calendars */
    char  text[MAXCTL][96];
    ListCtl *list[MAXCTL];
    TreeCtl *tree[MAXCTL];
    int   focus, copen;           /* copen = index of the open combo dropdown, or -1 */
    HWND  hwnd;                    /* this dialog's window (for owner-draw callbacks) */
} DlgState;

static int find_id(DlgState *s, int id) { for (int i = 0; i < s->nitems; i++) if (s->items[i].id == id) return i; return -1; }
static int btn_kind(const DLGITEM *it) { return (int)(it->style & 0x0F); }
static int is_list(u16 c) { return c == WC_LISTBOX || c == WC_LISTVIEW; }
static int has_listdata(u16 c) { return c == WC_LISTBOX || c == WC_LISTVIEW || c == WC_COMBO; }
#define COMBO_MAXDROP 8

/* ---- Dialog Units ---- */
static void base_units(int *bx, int *by) {
    const char *s = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    u32 w = font_measure(s); *bx = (int)((w + 26) / 52); if (*bx < 1) *bx = 1;
    *by = (int)font_height(); if (*by < 1) *by = 8;
}
static int mdx(DlgState *s, int v) { return s->dlu ? v * s->bx / 4 : v; }
static int mdy(DlgState *s, int v) { return s->dlu ? v * s->by / 8 : v; }
static int rowh(void) { return (int)font_height() + 4; }
static const char *cal_month_name(int m) {
    static const char *n[] = { "January","February","March","April","May","June","July","August","September","October","November","December" };
    return (m >= 1 && m <= 12) ? n[m-1] : "";
}
static int cal_dow(int y, int m, int d) { static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4}; if (m < 3) y -= 1; return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7; }
static int cal_dim(int y, int m) { static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31}; if (m == 2 && ((y%4==0 && y%100!=0)||y%400==0)) return 29; return d[m-1]; }
static int tabstrip_h(void) { return (int)font_height() + 10; }

/* the page-area origin (where a page's item coords are anchored) */
static void page_origin(DlgState *s, int ox, int oy, int *pox, int *poy) {
    if (s->is_sheet) { *pox = ox + 8; *poy = oy + tabstrip_h() + 8; }
    else { *pox = ox; *poy = oy; }
}
/* OK/Cancel/Apply geometry (shared by paint + hit-test so they never drift) */
static void sheet_buttons(int ox, int oy, int cw, int ch, int *bx, int *by, int *bw) {
    *bw = 74; *by = oy + ch - 32; *bx = ox + cw - (*bw + 8) * 3 - 4;
}

/* ---- list paint + geometry ---- */
static int list_visible(int h, int header) { int b = h - 2 - (header ? rowh() : 0); int v = b / rowh(); return v < 0 ? 0 : v; }
static void list_clamp(ListCtl *L, int vis) {
    if (L->sel >= L->nrow) L->sel = L->nrow - 1;
    int maxtop = L->nrow - vis; if (maxtop < 0) maxtop = 0;
    if (L->top > maxtop) L->top = maxtop; if (L->top < 0) L->top = 0;
}
static void paint_list(ListCtl *L, int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(x, y, w, h);
    int fh = (int)font_height(), rh = rowh(), listx = x + 2, listw = w - 4;
    int need_sb = L->nrow > list_visible(h, L->header); if (need_sb) listw -= SBW;
    int ry = y + 2;
    if (L->header) {
        fb_fill_rect(listx, ry, listw, rh, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(listx, ry, listw, rh);
        int cx = listx + 4; for (int c = 0; c < L->ncol; c++) { fb_text_t(cx, ry + (rh-fh)/2, L->title[c], RGB(0,0,0)); cx += L->colw[c]; }
        ry += rh;
    }
    int vis = list_visible(h, L->header);
    for (int r = 0; r < vis; r++) {
        int row = L->top + r; if (row >= L->nrow) break;
        int yy = ry + r * rh, sel = (row == L->sel);
        if (sel) fb_fill_rect(listx, yy, listw, rh, RGB(0,0,0x80));
        u32 fg = sel ? RGB(0xFF,0xFF,0xFF) : RGB(0,0,0);
        int cx = listx + 4;
        for (int c = 0; c < L->ncol; c++) { fb_text_t(cx, yy + (rh-fh)/2, L->cell[row][c], fg); cx += (L->ncol==1) ? listw : L->colw[c]; }
    }
    if (need_sb) {
        int sx = x + w - SBW - 1, sy = y + 1, sh = h - 2;
        int track = sh - 2*SBW, maxtop = L->nrow - vis; if (maxtop < 1) maxtop = 1;
        int th = track * vis / L->nrow; if (th < 8) th = 8; if (th > track) th = track;
        int ty = sy + SBW + (track - th) * L->top / maxtop;
        dlg_sb_v(sx, sy, sh, ty, th);
    }
}

/* compute the visible nodes of a tree (collapsed subtrees hidden), pre-order */
static int tree_visible(TreeCtl *T, int *vis, int max) {
    int nv = 0, hide = 1000;
    for (int i = 0; i < T->n; i++) {
        if (T->depth[i] > hide) continue;
        if (nv < max) vis[nv++] = i;
        hide = (T->expandable[i] && !T->expanded[i]) ? T->depth[i] : 1000;
    }
    return nv;
}
static void paint_tree(TreeCtl *T, int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(x, y, w, h);
    int fh = (int)font_height(), rh = fh + 3;
    int vis[TREE_MAXNODE], nv = tree_visible(T, vis, TREE_MAXNODE);
    int rows = (h - 4) / rh, need_sb = nv > rows, listw = w - 4 - (need_sb ? SBW : 0);
    for (int r = 0; r < rows; r++) {
        int vi = T->top + r; if (vi >= nv) break; int node = vis[vi];
        int yy = y + 2 + r * rh, ix = x + 4 + T->depth[node] * 13, sel = (node == T->sel);
        if (sel) fb_fill_rect(x + 2, yy, listw, rh, RGB(0,0,0x80));
        if (T->expandable[node]) {                       /* crisp Win95 [+]/[-] box */
            int bx = ix, by = yy + (rh - 9)/2;
            fb_fill_rect(bx, by, 9, 9, RGB(0xFF,0xFF,0xFF));
            fb_rect(bx, by, 9, 9, RGB(0x80,0x80,0x80));                          /* gray outline */
            fb_fill_rect(bx + 2, by + 4, 5, 1, RGB(0,0,0));                       /* minus */
            if (!T->expanded[node]) fb_fill_rect(bx + 4, by + 2, 1, 5, RGB(0,0,0));/* plus */
        }
        fb_text_t(ix + 14, yy + (rh - fh)/2, T->text[node], sel ? RGB(0xFF,0xFF,0xFF) : RGB(0,0,0));
    }
    if (need_sb) {
        int sx = x + w - SBW - 1, sy = y + 1, sh = h - 2;
        int track = sh - 2*SBW, maxtop = nv - rows; if (maxtop < 1) maxtop = 1;
        int th = track * rows / nv; if (th < 8) th = 8; if (th > track) th = track;
        int ty = sy + SBW + (track - th) * (T->top < maxtop ? T->top : maxtop) / maxtop;
        dlg_sb_v(sx, sy, sh, ty, th);
    }
}

/* paint one control */
static void paint_ctl(DlgState *s, int i, int pox, int poy) {
    const DLGITEM *it = &s->items[i];
    int x = pox + mdx(s, it->x), y = poy + mdy(s, it->y), w = mdx(s, it->cx), h = mdy(s, it->cy);
    if (it->wclass == WC_STATIC) {
        if (it->style & SS_ETCHEDHORZ) { fb_fill_rect(x, y, w, 1, RGB(0x80,0x80,0x80)); fb_fill_rect(x, y+1, w, 1, RGB(0xFF,0xFF,0xFF)); }
        else cc_label(x, y, s->text[i]);
    } else if (it->wclass == WC_EDIT) {
        if (it->style & ES_PASSWORD) { char d[96]; int n = s->len[i] < 95 ? s->len[i] : 95; for (int k=0;k<n;k++) d[k]='*'; d[n]=0; cc_edit(x, y, w, h, d, s->focus==i); }
        else cc_edit(x, y, w, h, s->text[i], s->focus==i);
    } else if (it->wclass == WC_OWNERDRAW) {
        RECT rc = { x, y, x + w, y + h };
        WNDPROC p = s->is_sheet ? s->pageproc[s->itempage[i]] : s->proc;
        if (p) p(s->hwnd, WM_DRAWITEM, (WPARAM)it->id, (LPARAM)&rc);
    } else if (is_list(it->wclass)) {
        if (s->list[i]) paint_list(s->list[i], x, y, w, h);
    } else if (it->wclass == WC_TREEVIEW) {
        if (s->tree[i]) paint_tree(s->tree[i], x, y, w, h);
    } else if (it->wclass == WC_COMBO) {
        int fh = (int)font_height(); ListCtl *L = s->list[i];
        fb_fill_rect(x, y, w, h, RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(x, y, w, h);
        const char *cur = (L && L->sel >= 0 && L->sel < L->nrow) ? L->cell[L->sel][0] : "";
        fb_text_t(x + 4, y + (h - fh)/2, cur, RGB(0,0,0));
        int bw = h, bx = x + w - bw;                                  /* drop button */
        fb_fill_rect(bx, y + 1, bw - 1, h - 2, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(bx, y + 1, bw - 1, h - 2);
        { int cx = bx + bw/2, cy = y + h/2;                           /* pixel down-triangle */
          for (int k = 0; k < 4; k++) fb_fill_rect(cx - k, cy + 2 - k, 2*k + 1, 1, RGB(0,0,0)); }
    } else if (it->wclass == WC_MONTHCAL) {
        int fh = (int)font_height(), cw = w / 7, hh = fh + 4;
        fb_fill_rect(x, y, w, h, RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(x, y, w, h);
        /* header: prev/next arrows (pixel triangles, not font glyphs) + "Month Year" */
        { int ax = x + 8, ay = y + 2 + fh/2;
          for (int k = 0; k < 4; k++) fb_fill_rect(ax + k - 2, ay - k, 1, 2*k + 1, RGB(0,0,0)); }     /* left */
        { int ax = x + w - 8, ay = y + 2 + fh/2;
          for (int k = 0; k < 4; k++) fb_fill_rect(ax + 2 - k, ay - k, 1, 2*k + 1, RGB(0,0,0)); }     /* right */
        { char hdr[24]; const char *mn = cal_month_name(s->calm[i]); int k = 0; for (; mn[k] && k < 15; k++) hdr[k] = mn[k];
          hdr[k++] = ' '; int yy = s->caly[i]; char t[8]; int j = 0; while (yy) { t[j++] = (char)('0'+yy%10); yy/=10; } while (j) hdr[k++] = t[--j]; hdr[k] = 0;
          fb_text_t(x + (w - (int)font_measure(hdr))/2, y + 2, hdr, RGB(0,0,0)); }
        /* weekday row */
        static const char *wd[] = { "Su","Mo","Tu","We","Th","Fr","Sa" };
        for (int c = 0; c < 7; c++) fb_text_t(x + c*cw + (cw-12)/2, y + hh, wd[c], RGB(0x40,0x40,0x40));
        /* day grid */
        int first = cal_dow(s->caly[i], s->calm[i], 1), dim = cal_dim(s->caly[i], s->calm[i]);
        for (int day = 1; day <= dim; day++) { int cell = first + day - 1, r = cell/7, c = cell%7;
            int dx = x + c*cw, dy = y + hh + (fh+1) + r*(fh+1), sel = (day == s->cald[i]);
            if (sel) fb_fill_rect(dx + 1, dy, cw - 1, fh + 1, RGB(0,0,0x80));
            char b[4]; int k = 0; if (day >= 10) b[k++] = (char)('0'+day/10); b[k++] = (char)('0'+day%10); b[k] = 0;
            fb_text_t(dx + (cw - (day>=10?12:6))/2, dy, b, sel ? RGB(0xFF,0xFF,0xFF) : RGB(0,0,0)); }
    } else if (it->wclass == WC_TRACKBAR) {
        int span = s->tbmax[i] - s->tbmin[i]; if (span < 1) span = 1;
        if (it->style & TBS_VERT) {
            int tx = x + w/2 - 2; fb_fill_rect(tx, y + 2, 4, h - 4, RGB(0x80,0x80,0x80)); wm_bevel_sunken(tx, y + 2, 4, h - 4);
            int thy = y + 2 + (s->tbmax[i] - s->tbpos[i]) * (h - 4 - 12) / span;   /* top = max */
            fb_fill_rect(x + w/2 - 8, thy, 16, 12, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(x + w/2 - 8, thy, 16, 12);
        } else {
            int ty = y + h/2 - 2; fb_fill_rect(x + 2, ty, w - 4, 4, RGB(0x80,0x80,0x80)); wm_bevel_sunken(x + 2, ty, w - 4, 4);
            int thx = x + 2 + (s->tbpos[i] - s->tbmin[i]) * (w - 4 - 12) / span;
            fb_fill_rect(thx, y + h/2 - 8, 12, 16, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(thx, y + h/2 - 8, 12, 16);
        }
    } else if (it->wclass == WC_TABFRAME) {
        fb_fill_rect(x, y + rowh(), w, h - rowh(), RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(x, y + rowh(), w, h - rowh());
        fb_fill_rect(x, y, 44, rowh() + 2, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(x, y, 44, rowh() + 2);   /* a tab stub */
    } else if (it->wclass == WC_BUTTON) {
        const char *lbl = s->text[i];                    /* mutable: SetDlgItemText can relabel buttons */
        switch (btn_kind(it)) {
            case BS_DEFPUSHBUTTON: cc_defbutton(x, y, w, h, lbl, 0); break;
            case BS_CHECKBOX: case BS_AUTOCHECKBOX: cc_checkbox(x, y, lbl, s->checked[i]); break;
            case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON: cc_radio(x, y, lbl, s->checked[i]); break;
            case BS_GROUPBOX: cc_groupbox(x, y, w, h, lbl); break;
            default: cc_button(x, y, w, h, lbl, 0); break;
        }
    }
}

static void dlg_paint(DlgState *s, int ox, int oy, int cw, int ch) {
    fb_fill_rect(ox, oy, cw, ch, RGB(0xC0,0xC0,0xC0));
    if (s->is_sheet) {
        const char *labels[MAXPAGE]; for (int p = 0; p < s->npages; p++) labels[p] = s->tabtitle[p];
        cc_tabs_var(ox + 4, oy + 4, labels, s->npages, s->active);
        int py = oy + tabstrip_h(), ph = ch - tabstrip_h() - 40;
        fb_fill_rect(ox + 4, py, cw - 8, ph, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(ox + 4, py, cw - 8, ph);
    }
    int pox, poy; page_origin(s, ox, oy, &pox, &poy);
    for (int i = 0; i < s->nitems; i++) {
        if (s->is_sheet && s->itempage[i] != s->active) continue;
        paint_ctl(s, i, pox, poy);
    }
    if (s->is_sheet) {
        int bx, by, bw; sheet_buttons(ox, oy, cw, ch, &bx, &by, &bw);
        cc_defbutton(bx, by, bw, 24, "OK", 0);
        cc_button(bx + bw + 8, by, bw, 24, "Cancel", 0);
        cc_button(bx + (bw + 8) * 2, by, bw, 24, "Apply", 0);
    }
    if (s->copen >= 0 && (!s->is_sheet || s->itempage[s->copen] == s->active)) {   /* the open combo list, on top */
        int i = s->copen; const DLGITEM *it = &s->items[i]; ListCtl *L = s->list[i];
        int x = pox + mdx(s, it->x), y = poy + mdy(s, it->y), w = mdx(s, it->cx), h = mdy(s, it->cy);
        int fh = (int)font_height(), rh = fh + 2, n = L->nrow < COMBO_MAXDROP ? L->nrow : COMBO_MAXDROP;
        int dy = y + h, dh = n * rh + 2;
        fb_fill_rect(x, dy, w, dh, RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(x, dy, w, dh);
        for (int r = 0; r < n; r++) { int row = r, yy = dy + 2 + r * rh, sel = (row == L->sel);
            if (sel) fb_fill_rect(x + 2, yy, w - 4, rh, RGB(0,0,0x80));
            fb_text_t(x + 4, yy, L->cell[row][0], sel ? RGB(0xFF,0xFF,0xFF) : RGB(0,0,0)); }
    }
}

/* dispatch a WM_COMMAND from control i to the owning proc (page or plain) */
static void dispatch_cmd(DlgState *s, HWND h, int i, int notify) {
    WNDPROC p = s->is_sheet ? s->pageproc[s->itempage[i]] : s->proc;
    if (p) p(h, WM_COMMAND, MAKEWPARAM(s->items[i].id, notify), 0);
}
static void page_activate(DlgState *s, HWND h, int page) {
    s->active = page; s->focus = -1;
    if (s->pageproc[page]) s->pageproc[page](h, WM_INITDIALOG, 0, 0);
}
static void sheet_apply(DlgState *s, HWND h) {   /* persist every page (OK/Apply) */
    for (int p = 0; p < s->npages; p++) if (s->pageproc[p]) s->pageproc[p](h, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
}

/* set a trackbar's position from a pixel. For horizontal, pass axis=px & extent=w
 * with base=itemx; for vertical, axis=py & extent=h with base=itemy (inverted). */
static void track_set_v(DlgState *s, HWND h, int i, int base, int extent, int coord, int vert) {
    int span = s->tbmax[i] - s->tbmin[i]; if (span < 1) span = 1;
    int rel = coord - (base + 2 + 6), usable = extent - 4 - 12; if (usable < 1) usable = 1;
    int frac = rel * span / usable; int pos = vert ? (s->tbmax[i] - frac) : (s->tbmin[i] + frac);
    if (pos < s->tbmin[i]) pos = s->tbmin[i]; if (pos > s->tbmax[i]) pos = s->tbmax[i];
    if (pos != s->tbpos[i]) { s->tbpos[i] = pos; dispatch_cmd(s, h, i, TBN_CHANGED); }
}
static void track_set(DlgState *s, HWND h, int i, int itemx, int w, int px) { track_set_v(s, h, i, itemx, w, px, 0); }
static int hit_ctl(DlgState *s, HWND h, int i, int pox, int poy, int px, int py) {
    const DLGITEM *it = &s->items[i];
    int x = pox + mdx(s, it->x), y = poy + mdy(s, it->y), w = mdx(s, it->cx), hh = mdy(s, it->cy);
    if (px < x || px >= x + w || py < y || py >= y + hh) return 0;
    if (!s->enabled[i]) return 1;
    if (it->wclass == WC_TREEVIEW) {
        TreeCtl *T = s->tree[i]; if (!T) return 1; s->focus = i;
        int fh = (int)font_height(), rh = fh + 3;
        int vis[TREE_MAXNODE], nv = tree_visible(T, vis, TREE_MAXNODE);
        int rows = (hh - 4) / rh, need_sb = nv > rows;
        if (need_sb && px >= x + w - SBW - 1) {                 /* scrollbar arrows */
            if (py < y + 1 + SBW) { if (T->top > 0) T->top--; }
            else if (py >= y + hh - 1 - SBW) { if (T->top < nv - rows) T->top++; }
            return 1;
        }
        int r = (py - (y + 2)) / rh, vi = T->top + r;
        if (r >= 0 && vi < nv) { int node = vis[vi], ix = x + 4 + T->depth[node] * 13;
            if (T->expandable[node] && px >= ix && px < ix + 11) { T->expanded[node] = !T->expanded[node]; }   /* [+]/[-] */
            else { T->sel = node; dispatch_cmd(s, h, i, TVN_SELCHANGED); }
        }
        return 1;
    }
    if (it->wclass == WC_COMBO) { s->copen = (s->copen == i) ? -1 : i; return 1; }
    if (it->wclass == WC_MONTHCAL) {
        int fh = (int)font_height(), cw = w / 7, hh = fh + 4;
        if (py < y + hh) {                                  /* header: prev/next month */
            if (px < x + w/2) { if (--s->calm[i] < 1) { s->calm[i] = 12; s->caly[i]--; } }
            else { if (++s->calm[i] > 12) { s->calm[i] = 1; s->caly[i]++; } }
            int dm = cal_dim(s->caly[i], s->calm[i]); if (s->cald[i] > dm) s->cald[i] = dm;
            dispatch_cmd(s, h, i, MCN_SELCHANGE); return 1;
        }
        int gridY = y + hh + (fh+1), first = cal_dow(s->caly[i], s->calm[i], 1), dim = cal_dim(s->caly[i], s->calm[i]);
        for (int day = 1; day <= dim; day++) { int cell = first + day - 1, r = cell/7, c = cell%7;
            int dx = x + c*cw, dy = gridY + r*(fh+1);
            if (px >= dx && px < dx + cw && py >= dy && py < dy + fh + 1) { s->cald[i] = day; dispatch_cmd(s, h, i, MCN_SELCHANGE); return 1; } }
        return 1;
    }
    if (it->wclass == WC_TRACKBAR) { s->focus = i;
        if (it->style & TBS_VERT) track_set_v(s, h, i, y, hh, py, 1); else track_set_v(s, h, i, x, w, px, 0); return 1; }
    if (it->wclass == WC_EDIT) { s->focus = i; return 1; }
    if (is_list(it->wclass)) {
        ListCtl *L = s->list[i]; if (!L) return 1; s->focus = i;
        int vis = list_visible(hh, L->header), need_sb = L->nrow > vis;
        if (need_sb && px >= x + w - SBW - 1) {
            int sy = y + 1, sh = hh - 2;
            if (py < sy + SBW) { if (L->top > 0) L->top--; }
            else if (py >= y + hh - 1 - SBW) L->top++;
            else { if (py < sy + sh/2) L->top -= vis; else L->top += vis; }
            list_clamp(L, vis); return 1;
        }
        int rowsY = y + 2 + (L->header ? rowh() : 0), row = L->top + (py - rowsY) / rowh();
        if (row >= 0 && row < L->nrow && py >= rowsY) { L->sel = row; if (it->style & LBS_NOTIFY) dispatch_cmd(s, h, i, LBN_SELCHANGE); }
        return 1;
    }
    if (it->wclass == WC_BUTTON) {
        int k = btn_kind(it);
        if (k == BS_AUTOCHECKBOX || k == BS_CHECKBOX) s->checked[i] = !s->checked[i];
        else if (k == BS_AUTORADIOBUTTON || k == BS_RADIOBUTTON) {
            for (int j = 0; j < s->nitems; j++)
                if (s->items[j].wclass == WC_BUTTON && (!s->is_sheet || s->itempage[j] == s->active) &&
                    (btn_kind(&s->items[j]) == BS_AUTORADIOBUTTON || btn_kind(&s->items[j]) == BS_RADIOBUTTON))
                    s->checked[j] = (j == i);
        }
        if (k != BS_GROUPBOX) dispatch_cmd(s, h, i, 0);
        return 1;
    }
    return 1;
}

static LRESULT CALLBACK DlgEngineProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg);
    switch (msg) {
        case WM_CREATE: {
            s = (DlgState *)kmalloc(sizeof(DlgState));
            for (int i = 0; i < MAXCTL; i++) { s->checked[i]=0; s->enabled[i]=1; s->text[i][0]=0; s->len[i]=0; s->list[i]=0; s->tree[i]=0; s->itempage[i]=0;
                s->tbpos[i]=0; s->tbmin[i]=0; s->tbmax[i]=100; s->caly[i]=2026; s->calm[i]=1; s->cald[i]=1; }
            s->focus = -1; s->copen = -1; s->is_sheet = 0; s->nitems = 0; s->npages = 1; s->active = 0; s->proc = 0; s->hwnd = hdlg;
            base_units(&s->bx, &s->by); s->dlu = 0;
            SetWindowUserData(hdlg, s);
            return 0;
        }
        case WM_PAINT: {
            int ox, oy, cw, ch; GetClientOrigin(hdlg, &ox, &oy, &cw, &ch);
            dlg_paint(s, ox, oy, cw, ch);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int ox, oy, cw, ch; GetClientOrigin(hdlg, &ox, &oy, &cw, &ch);
            int px = ox + GET_X_LPARAM(lParam), py = oy + GET_Y_LPARAM(lParam);
            if (s->copen >= 0) {                              /* a combo dropdown is open: it eats this click */
                int i = s->copen; const DLGITEM *it = &s->items[i]; ListCtl *L = s->list[i];
                int pox2, poy2; page_origin(s, ox, oy, &pox2, &poy2);
                int x = pox2 + mdx(s, it->x), y = poy2 + mdy(s, it->y), w = mdx(s, it->cx), h = mdy(s, it->cy);
                int fh = (int)font_height(), rh = fh + 2, n = L->nrow < COMBO_MAXDROP ? L->nrow : COMBO_MAXDROP, dy = y + h;
                if (px >= x && px < x + w && py >= dy + 2 && py < dy + 2 + n * rh) {
                    int row = (py - dy - 2) / rh; if (row >= 0 && row < L->nrow && row != L->sel) { L->sel = row; dispatch_cmd(s, hdlg, i, CBN_SELCHANGE); }
                }
                s->copen = -1; return 0;
            }
            if (s->is_sheet) {
                const char *labels[MAXPAGE]; for (int p = 0; p < s->npages; p++) labels[p] = s->tabtitle[p];
                int t = cc_tab_hit_var(4, 4, labels, s->npages, px - ox, py - oy);
                if (t >= 0) { if (t != s->active) page_activate(s, hdlg, t); return 0; }
                int bx, by, bw; sheet_buttons(ox, oy, cw, ch, &bx, &by, &bw);
                if (py >= by && py < by + 24) {
                    if (px >= bx && px < bx + bw) { sheet_apply(s, hdlg); DestroyWindow(hdlg); return 0; }         /* OK */
                    if (px >= bx + bw + 8 && px < bx + 2*bw + 8) { DestroyWindow(hdlg); return 0; }                 /* Cancel */
                    if (px >= bx + 2*(bw + 8) && px < bx + 3*bw + 16) { sheet_apply(s, hdlg); return 0; }           /* Apply */
                }
            }
            int pox, poy; page_origin(s, ox, oy, &pox, &poy);
            for (int i = 0; i < s->nitems; i++) {
                if (s->is_sheet && s->itempage[i] != s->active) continue;
                if (hit_ctl(s, hdlg, i, pox, poy, px, py)) return 0;
            }
            return 0;
        }
        case WM_COMMAND: {   /* a menu-bar command (SetMenuW32) arrives here -> forward to the app */
            WNDPROC p = s->is_sheet ? s->pageproc[s->active] : s->proc;
            if (p) p(hdlg, WM_COMMAND, wParam, lParam);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if ((wParam & MK_LBUTTON) && s->focus >= 0 && s->items[s->focus].wclass == WC_TRACKBAR) {
                int ox, oy, cw, ch; GetClientOrigin(hdlg, &ox, &oy, &cw, &ch); (void)cw; (void)ch;
                int pox, poy; page_origin(s, ox, oy, &pox, &poy);
                int i = s->focus;
                if (s->items[i].style & TBS_VERT) { int y = poy + mdy(s, s->items[i].y), hh = mdy(s, s->items[i].cy);
                    track_set_v(s, hdlg, i, y, hh, oy + GET_Y_LPARAM(lParam), 1); }
                else { int x = pox + mdx(s, s->items[i].x), w = mdx(s, s->items[i].cx);
                    track_set_v(s, hdlg, i, x, w, ox + GET_X_LPARAM(lParam), 0); }
            }
            return 0;
        }
        case WM_CHAR: {
            char c = (char)wParam;
            if (c == '\r' || c == '\n') {
                for (int i = 0; i < s->nitems; i++)
                    if (s->items[i].wclass == WC_BUTTON && btn_kind(&s->items[i]) == BS_DEFPUSHBUTTON && s->enabled[i]
                        && (!s->is_sheet || s->itempage[i] == s->active)) { dispatch_cmd(s, hdlg, i, 0); return 0; }
                if (s->is_sheet) { sheet_apply(s, hdlg); DestroyWindow(hdlg); }
                return 0;
            }
            if (c == 27) {
                if (s->is_sheet) { DestroyWindow(hdlg); return 0; }
                int i = find_id(s, IDCANCEL); if (i >= 0) dispatch_cmd(s, hdlg, i, 0); return 0;
            }
            if (c == '\t') {
                int n = s->nitems, start = s->focus;
                for (int step = 1; step <= n; step++) { int j = (start + step) % n; if (j < 0) j += n;
                    u16 wc = s->items[j].wclass; if ((wc == WC_EDIT || is_list(wc)) && s->enabled[j] && (!s->is_sheet || s->itempage[j]==s->active)) { s->focus = j; break; } }
                return 0;
            }
            if (s->focus < 0) return 0;
            int i = s->focus;
            if (s->items[i].wclass == WC_EDIT) {
                if (c == '\b') { if (s->len[i] > 0) s->text[i][--s->len[i]] = 0; }
                else if (c >= ' ' && c < 127 && s->len[i] < 94) { s->text[i][s->len[i]++] = c; s->text[i][s->len[i]] = 0; }
            }
            return 0;
        }
        case WM_DESTROY:
            if (s) {
                if (s->is_sheet) { for (int p = 0; p < s->npages; p++) if (s->pageproc[p]) s->pageproc[p](hdlg, WM_DESTROY, 0, 0); }
                else if (s->proc) s->proc(hdlg, WM_DESTROY, 0, 0);
                for (int i = 0; i < MAXCTL; i++) { if (s->list[i]) kfree(s->list[i]); if (s->tree[i]) kfree(s->tree[i]); }
                kfree(s);
            }
            return 0;
        default: return DefWindowProc(hdlg, msg, wParam, lParam);
    }
}

/* seed a control's mutable state from its template + allocate list backing */
static void seed_ctl(DlgState *s, int i) {
    const DLGITEM *it = &s->items[i];
    if (has_listdata(it->wclass)) {
        ListCtl *L = (ListCtl *)kmalloc(sizeof(ListCtl));
        L->ncol = 1; L->header = (it->wclass == WC_LISTVIEW); L->nrow = 0; L->sel = -1; L->top = 0;
        for (int c = 0; c < LV_MAXCOL; c++) { L->title[c][0] = 0; L->colw[c] = 60; }
        s->list[i] = L;
    } else if (it->wclass == WC_TREEVIEW) {
        TreeCtl *T = (TreeCtl *)kmalloc(sizeof(TreeCtl)); T->n = 0; T->sel = -1; T->top = 0; s->tree[i] = T;
    } else if (it->text && (it->wclass == WC_EDIT || it->wclass == WC_STATIC || it->wclass == WC_BUTTON)) {
        int k = 0; const char *p = it->text; for (; p[k] && k < 94; k++) s->text[i][k] = p[k]; s->text[i][k] = 0; s->len[i] = k;
    }
}

HWND CreateDialogL(const DLGTEMPLATE_L *tmpl, WNDPROC dlgProc, void *param) {
    int bx, by; base_units(&bx, &by);
    int dlu = (tmpl->flags & DS_DLU) != 0;
    int pxw = dlu ? tmpl->cx * bx / 4 : tmpl->cx, pxh = dlu ? tmpl->cy * by / 8 : tmpl->cy;
    HWND h = CreateAppWindow((int)((fb_width() - (u32)(pxw+8))/2), (int)((fb_height() - (u32)(pxh+28))/2),
                             pxw + 8, pxh + 28, tmpl->caption, DlgEngineProc, 0);
    if (!h) return 0;
    DlgState *s = (DlgState *)GetWindowUserData(h);
    s->is_sheet = 0; s->proc = dlgProc; s->dlu = dlu; s->bx = bx; s->by = by;
    s->nitems = tmpl->nitems < MAXCTL ? tmpl->nitems : MAXCTL;
    for (int i = 0; i < s->nitems; i++) { s->items[i] = tmpl->items[i]; s->itempage[i] = 0; seed_ctl(s, i); }
    dlgProc(h, WM_INITDIALOG, 0, (LPARAM)param);
    return h;
}

HWND PropertySheetL(const PROPSHEETHEADER_L *psh, int startpage) {
    int bx, by; base_units(&bx, &by);
    int dlu = (psh->flags & DS_DLU) != 0;
    int pxw = dlu ? psh->cx * bx / 4 : psh->cx, pxh = dlu ? psh->cy * by / 8 : psh->cy;
    /* window client must fit tab strip + page + button row */
    int cw = pxw + 16, chh = pxh + tabstrip_h() + 8 + 40;
    HWND h = CreateAppWindow((int)((fb_width() - (u32)(cw+8))/2), (int)((fb_height() - (u32)(chh+28))/2),
                             cw + 8, chh + 28, psh->caption, DlgEngineProc, 0);
    if (!h) return 0;
    DlgState *s = (DlgState *)GetWindowUserData(h);
    s->is_sheet = 1; s->dlu = dlu; s->bx = bx; s->by = by; s->caption = psh->caption;
    s->npages = psh->npages < MAXPAGE ? psh->npages : MAXPAGE;
    s->nitems = 0;
    for (int p = 0; p < s->npages; p++) {
        s->tabtitle[p] = psh->pages[p].title; s->pageproc[p] = psh->pages[p].proc;
        const DLGTEMPLATE_L *pt = psh->pages[p].page;
        for (int i = 0; i < pt->nitems && s->nitems < MAXCTL; i++) {
            int k = s->nitems++; s->items[k] = pt->items[i]; s->itempage[k] = p; seed_ctl(s, k);
        }
    }
    if (startpage < 0 || startpage >= s->npages) startpage = 0;
    page_activate(s, h, startpage);
    return h;
}
HWND PropSheetPageDlg(HWND hsheet, int pageindex) { (void)pageindex; return hsheet; }   /* pages share the sheet HWND */

void EndDialogL(HWND hdlg) { DestroyWindow(hdlg); }

int GetDlgItemTextW32(HWND hdlg, int id, char *buf, int max) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id), k = 0;
    if (i >= 0) for (; s->text[i][k] && k < max-1; k++) buf[k] = s->text[i][k];
    buf[k] = 0; return k;
}
void SetDlgItemTextW32(HWND hdlg, int id, const char *str) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id);
    if (i < 0) return; int k = 0; for (; str[k] && k < 94; k++) s->text[i][k] = str[k]; s->text[i][k] = 0; s->len[i] = k;
}
void CheckDlgButton(HWND hdlg, int id, int check) { DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id); if (i >= 0) s->checked[i] = check ? 1 : 0; }
int  IsDlgButtonChecked(HWND hdlg, int id) { DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id); return i >= 0 ? s->checked[i] : 0; }
void EnableDlgItem(HWND hdlg, int id, int enable) { DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id); if (i >= 0) s->enabled[i] = enable ? 1 : 0; }

static ListCtl *lc(HWND hdlg, int id) { DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id); return (i >= 0) ? s->list[i] : 0; }
void DlgListReset(HWND hdlg, int id) { ListCtl *L = lc(hdlg, id); if (L) { L->nrow = 0; L->sel = -1; L->top = 0; } }
int  DlgListAdd(HWND hdlg, int id, const char *text) { ListCtl *L = lc(hdlg, id); if (!L || L->nrow >= LIST_MAXROW) return -1;
    int r = L->nrow++, k = 0; for (; text[k] && k < 39; k++) L->cell[r][0][k] = text[k]; L->cell[r][0][k] = 0; return r; }
int  DlgListCount(HWND hdlg, int id) { ListCtl *L = lc(hdlg, id); return L ? L->nrow : 0; }
int  DlgListGetSel(HWND hdlg, int id) { ListCtl *L = lc(hdlg, id); return L ? L->sel : -1; }
void DlgListSetSel(HWND hdlg, int id, int row) { ListCtl *L = lc(hdlg, id); if (L && row >= -1 && row < L->nrow) L->sel = row; }
void DlgLVColumns(HWND hdlg, int id, const char *const *titles, const int *widths, int n) {
    ListCtl *L = lc(hdlg, id); if (!L) return; DlgState *s = (DlgState *)GetWindowUserData(hdlg);
    if (n > LV_MAXCOL) n = LV_MAXCOL; L->ncol = n; L->header = 1;
    for (int c = 0; c < n; c++) { int k = 0; for (; titles[c][k] && k < 19; k++) L->title[c][k] = titles[c][k]; L->title[c][k] = 0; L->colw[c] = mdx(s, widths[c]); }
}
int  DlgLVAddRow(HWND hdlg, int id, const char *const *cells, int ncells) {
    ListCtl *L = lc(hdlg, id); if (!L || L->nrow >= LIST_MAXROW) return -1;
    int r = L->nrow++; if (ncells > L->ncol) ncells = L->ncol;
    for (int c = 0; c < L->ncol; c++) { const char *t = (c < ncells) ? cells[c] : ""; int k = 0; for (; t[k] && k < 39; k++) L->cell[r][c][k] = t[k]; L->cell[r][c][k] = 0; }
    return r;
}

void DlgTrackRange(HWND hdlg, int id, int lo, int hi) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id);
    if (i >= 0) { s->tbmin[i] = lo; s->tbmax[i] = hi > lo ? hi : lo + 1;
        if (s->tbpos[i] < lo) s->tbpos[i] = lo; if (s->tbpos[i] > s->tbmax[i]) s->tbpos[i] = s->tbmax[i]; }
}
void DlgTrackPos(HWND hdlg, int id, int pos) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id);
    if (i >= 0) { if (pos < s->tbmin[i]) pos = s->tbmin[i]; if (pos > s->tbmax[i]) pos = s->tbmax[i]; s->tbpos[i] = pos; }
}
int  DlgTrackGetPos(HWND hdlg, int id) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id); return i >= 0 ? s->tbpos[i] : 0;
}
void DlgCalSet(HWND hdlg, int id, int y, int mo, int d) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id);
    if (i >= 0) { s->caly[i] = y; s->calm[i] = (mo<1?1:mo>12?12:mo); int dm = cal_dim(s->caly[i], s->calm[i]); s->cald[i] = (d<1?1:d>dm?dm:d); }
}
void DlgCalGet(HWND hdlg, int id, int *y, int *mo, int *d) {
    DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id);
    if (i >= 0) { if (y) *y = s->caly[i]; if (mo) *mo = s->calm[i]; if (d) *d = s->cald[i]; }
}

static TreeCtl *tc(HWND hdlg, int id) { DlgState *s = (DlgState *)GetWindowUserData(hdlg); int i = find_id(s, id); return (i >= 0) ? s->tree[i] : 0; }
void DlgTreeReset(HWND hdlg, int id) { TreeCtl *T = tc(hdlg, id); if (T) { T->n = 0; T->sel = -1; T->top = 0; } }
int  DlgTreeAdd(HWND hdlg, int id, const char *text, int depth, int expandable) {
    TreeCtl *T = tc(hdlg, id); if (!T || T->n >= TREE_MAXNODE) return -1;
    int r = T->n++; int k = 0; for (; text[k] && k < 39; k++) T->text[r][k] = text[k]; T->text[r][k] = 0;
    T->depth[r] = (unsigned char)depth; T->expandable[r] = expandable ? 1 : 0; T->expanded[r] = 0; return r;
}
int  DlgTreeGetSel(HWND hdlg, int id) { TreeCtl *T = tc(hdlg, id); return T ? T->sel : -1; }
void DlgTreeSetSel(HWND hdlg, int id, int node) { TreeCtl *T = tc(hdlg, id); if (T && node >= -1 && node < T->n) T->sel = node; }
void DlgTreeExpand(HWND hdlg, int id, int node, int on) { TreeCtl *T = tc(hdlg, id); if (T && node >= 0 && node < T->n) T->expanded[node] = on ? 1 : 0; }
