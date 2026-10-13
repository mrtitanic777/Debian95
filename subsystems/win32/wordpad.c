/* Debian95 — WordPad. A Win32-idiom document editor (WndProc + GDI) with a
 * toolbar, a real multi-line text Model (line buffer + cursor, insert/delete/
 * newline/arrow keys, vertical scrolling), a File/Edit/Format/Help menu, and
 * Open/Save through the shared COMDLG32 dialog. Distinct from Notepad: toolbar +
 * scrolling document view. Demonstrates the "build the text Model first" pattern. */
#include <debian95/win32.h>
#include <debian95/wm.h>       /* clipboard, MRU, caret */
#include <debian95/font.h>
#include <debian95/nt.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>      /* strlen, memcpy */
#include <debian95/icons.h>    /* IC_DOC, toolbar_std */
#include <debian95/fb.h>       /* fb_blit for the real toolbar icons */

#define WL 300
#define WC 256
#define FACE  RGB(0xC0,0xC0,0xC0)
#define INK   RGB(0x10,0x10,0x10)
#define WHITE RGB(0xFF,0xFF,0xFF)

/* ---- Model ---- */
static char T[WL][WC];
static int  NL, CR, CC, TOP;         /* line count, cursor row/col, first visible line */
static char NAME[16], TITLE[40];
static int  g_vis = 1;               /* visible text rows (set in paint) */
static int  SR = -1, SC;             /* selection anchor (SR<0 = none); the caret is (CR,CC) */

static int sel_active(void) { return SR >= 0 && (SR != CR || SC != CC); }
static void sel_order(int *ar, int *ac, int *br, int *bc) {   /* anchor,caret -> ordered a<=b */
    if (SR < CR || (SR == CR && SC <= CC)) { *ar = SR; *ac = SC; *br = CR; *bc = CC; }
    else { *ar = CR; *ac = CC; *br = SR; *bc = SC; }
}
static int sel_get(char *out, int max) {                      /* selected text, lines joined by \n */
    int ar, ac, br, bc; sel_order(&ar, &ac, &br, &bc); int n = 0;
    for (int r = ar; r <= br; r++) { int c0 = (r == ar) ? ac : 0, c1 = (r == br) ? bc : (int)strlen(T[r]);
        for (int c = c0; c < c1 && n < max - 1; c++) out[n++] = T[r][c];
        if (r < br && n < max - 1) out[n++] = '\n'; }
    out[n] = 0; return n;
}
static void sel_delete(void) {                                /* remove the selected range */
    int ar, ac, br, bc; sel_order(&ar, &ac, &br, &bc);
    if (ar == br) { int len = (int)strlen(T[ar]); for (int c = bc; c <= len; c++) T[ar][ac + (c - bc)] = T[ar][c]; }
    else {
        char tail[WC]; int tn = 0; { int len = (int)strlen(T[br]); for (int c = bc; c <= len && tn < WC - 1; c++) tail[tn++] = T[br][c]; }
        int alen = ac; T[ar][alen] = 0; for (int i = 0; i < tn && alen < WC - 1; i++) T[ar][alen++] = tail[i]; T[ar][alen] = 0;
        int removed = br - ar; for (int r = ar + 1; r + removed < NL; r++) memcpy(T[r], T[r + removed], WC); NL -= removed; if (NL < 1) NL = 1;
    }
    CR = ar; CC = ac; SR = -1;
}
static int px_of(int line, int col) { char pre[WC]; int k = 0; for (; k < col && T[line][k]; k++) pre[k] = T[line][k]; pre[k] = 0; return (int)font_measure(pre); }

static void wp_clear(void) { for (int i = 0; i < WL; i++) T[i][0] = 0; NL = 1; CR = CC = TOP = 0; SR = -1; }
static void wp_title(const char *name) {
    int t = 0, s = 0; while (name[s] && t < 20) TITLE[t++] = name[s++];
    const char *suf = " - WordPad"; for (int i = 0; suf[i] && t < 39; i++) TITLE[t++] = suf[i]; TITLE[t] = 0;
}
static void wp_load(const char *name) {
    wp_clear();
    int i = 0; for (; name[i] && i < 15; i++) NAME[i] = name[i]; NAME[i] = 0;
    u8 *d; u32 sz; if (FsReadAlloc(name, &d, &sz) != 0) return;
    int r = 0, c = 0;
    for (u32 k = 0; k < sz && r < WL; k++) { char ch = (char)d[k];
        if (ch == '\r') continue;
        if (ch == '\n') { T[r][c] = 0; r++; c = 0; if (r < WL) T[r][0] = 0; continue; }
        if (ch == '\t') ch = ' ';
        if (c < WC - 1 && ch >= 32) T[r][c++] = ch; }
    if (r < WL) T[r][c] = 0; NL = r + 1; CR = CC = TOP = 0;
    kfree(d);
}
static void wp_save(void) {
    static u8 out[WL * 64]; u32 n = 0;
    for (int r = 0; r < NL; r++) { for (int i = 0; T[r][i] && n < sizeof(out); i++) out[n++] = (u8)T[r][i];
        if (r < NL - 1 && n < sizeof(out)) out[n++] = '\n'; }
    FsWriteFile(NAME, out, n);
}
static void wp_scroll(void) { if (CR < TOP) TOP = CR; if (CR >= TOP + g_vis) TOP = CR - g_vis + 1; if (TOP < 0) TOP = 0; }

/* nearest caret column for a pixel offset px into the line's text (proportional font) */
static int col_at(int line, int px) {
    if (px <= 0) return 0;
    char pre[WC]; int prevw = 0;
    for (int c = 0; T[line][c]; c++) {
        pre[c] = T[line][c]; pre[c + 1] = 0; int w = (int)font_measure(pre);
        if (px < (prevw + w) / 2) return c;
        prevw = w;
    }
    return (int)strlen(T[line]);
}

static void wp_key(char c) {
    unsigned char uc = (unsigned char)c;
    if (uc >= 0x91 && uc <= 0x94) SR = -1;                       /* keyboard nav clears the selection */
    else if (sel_active() && (c == '\b' || c == '\n' || c == '\r' || (c >= 32 && c < 127))) {
        int bs = (c == '\b'); sel_delete();                     /* typing/backspace replaces the selection */
        if (bs) { wp_scroll(); return; }
    }
    if (uc == 0x91) { if (CR > 0) CR--; }                        /* up */
    else if (uc == 0x92) { if (CR < NL - 1) CR++; }              /* down */
    else if (uc == 0x93) { if (CC > 0) CC--; else if (CR > 0) { CR--; CC = (int)strlen(T[CR]); } }  /* left */
    else if (uc == 0x94) { int len=(int)strlen(T[CR]); if (CC<len) CC++; else if (CR<NL-1){CR++;CC=0;} }  /* right */
    else if (c == '\n' || c == '\r') {
        if (NL < WL) { for (int r = NL; r > CR + 1; r--) memcpy(T[r], T[r-1], WC);
            int i = 0; while (T[CR][CC+i]) { T[CR+1][i] = T[CR][CC+i]; i++; } T[CR+1][i] = 0; T[CR][CC] = 0;
            NL++; CR++; CC = 0; }
    } else if (c == 8) {                                          /* backspace */
        if (CC > 0) { int len=(int)strlen(T[CR]); for (int i=CC-1;i<len;i++) T[CR][i]=T[CR][i+1]; CC--; }
        else if (CR > 0) { int pl=(int)strlen(T[CR-1]); int cl=(int)strlen(T[CR]);   /* join with previous line */
            if (pl+cl < WC-1) { for(int i=0;i<=cl;i++) T[CR-1][pl+i]=T[CR][i];
                for(int r=CR;r<NL-1;r++) memcpy(T[r],T[r+1],WC); NL--; CR--; CC=pl; } }
    } else if (c >= 32 && c < 127) {                              /* insert char */
        int len=(int)strlen(T[CR]); if (len<WC-2 && CC<=len) { for(int i=len;i>=CC;i--) T[CR][i+1]=T[CR][i]; T[CR][CC]=c; CC++; }
    }
    { int len=(int)strlen(T[CR]); if (CC>len) CC=len; }
    wp_scroll();
}

/* ---- toolbar ---- */
enum { TB_NEW=1, TB_OPEN, TB_SAVE, TB_CUT, TB_COPY, TB_PASTE };
static const struct { const char *lab; int id; int std; } TB[] = {
    { "New", TB_NEW, TBI_NEW }, { "Open", TB_OPEN, TBI_OPEN }, { "Save", TB_SAVE, TBI_SAVE },
    { "Cut", TB_CUT, TBI_CUT }, { "Copy", TB_COPY, TBI_COPY }, { "Paste", TB_PASTE, TBI_PASTE }
};
#define NTB 6
#define TBH 28
static void tb_rect(int i, int *x, int *w) { *x = 6 + i * 26; *w = 24; if (i >= 3) *x += 8; }  /* icon buttons; gap after Save */

/* ---- Edit actions via the shared clipboard ---- */
static char g_selbuf[8192];
static void wp_copy(void) {
    if (sel_active()) { sel_get(g_selbuf, sizeof g_selbuf); wm_clipboard_set(g_selbuf); }
    else wm_clipboard_set(T[CR]);
}
static void wp_cut(void)  {
    if (sel_active()) { sel_get(g_selbuf, sizeof g_selbuf); wm_clipboard_set(g_selbuf); sel_delete(); }
    else { wm_clipboard_set(T[CR]); T[CR][0] = 0; CC = 0; }
}
static void wp_paste(void){ const char *c = wm_clipboard_get(); if (c) for (int i = 0; c[i]; i++) if (c[i] >= 32) wp_key(c[i]); }

static void do_tb(int id) {
    switch (id) {
        case TB_NEW:  wp_clear(); { const char *u="(untitled)"; int i=0; for(;u[i]&&i<15;i++)NAME[i]=u[i]; NAME[i]=0; } wp_title("Document"); break;
        case TB_OPEN: { static OPENFILENAME o; o.title="Open"; o.filter=0; o.file[0]=0;
                        o.onok = (void(*)(const char*))0; /* set below */
                        extern void wp_open_cb(const char*); o.onok = wp_open_cb; GetOpenFileName(&o); } break;
        case TB_SAVE: { static OPENFILENAME o; o.title="Save As"; o.filter="*.TXT"; int i=0; for(;NAME[i]&&i<40;i++)o.file[i]=NAME[i]; o.file[i]=0;
                        extern void wp_saveas_cb(const char*); o.onok = wp_saveas_cb; GetSaveFileName(&o); } break;
        case TB_CUT:  wp_cut(); break;
        case TB_COPY: wp_copy(); break;
        case TB_PASTE:wp_paste(); break;
    }
}
void wp_open_cb(const char *name)  { wp_load(name); wm_add_recent(name); wp_title(name); }
void wp_saveas_cb(const char *name){ int i=0; for(;name[i]&&i<15;i++)NAME[i]=name[i]; NAME[i]=0; wp_save(); wp_title(name); }

/* ---- View ---- */
static void wp_paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    int cw = ps.rcPaint.right, ch = ps.rcPaint.bottom;
    int ox, oy; GetClientOrigin(hwnd, &ox, &oy, 0, 0);
    RECT full = { 0, 0, cw, ch }; HBRUSH f = CreateSolidBrush(FACE); FillRect(hdc, &full, f); DeleteObject(f);
    /* toolbar: real COMCTL32 standard button images (fall back to a text label) */
    for (int i = 0; i < NTB; i++) { int x, w; tb_rect(i, &x, &w);
        RECT r = { x, 3, x + w, 3 + 22 }; HBRUSH b = CreateSolidBrush(FACE); FillRect(hdc, &r, b); DeleteObject(b);
        DrawEdge3D(hdc, &r, 1);
        u32 img[16*16];
        if (toolbar_std(TB[i].std, 16, FACE, img) == 0) fb_blit(ox + x + (w-16)/2, oy + 3 + (22-16)/2, 16, 16, img);
        else { SetTextColor(hdc, INK); int lw = (int)font_measure(TB[i].lab); TextOut(hdc, x + (w-lw)/2, 3 + (22-(int)font_height())/2, TB[i].lab, 8); } }
    /* text page */
    int ty = TBH + 2, tx = 4, tw = cw - 8, th = ch - ty - 16;
    RECT page = { tx, ty, tx + tw, ty + th }; HBRUSH w = CreateSolidBrush(WHITE); FillRect(hdc, &page, w); DeleteObject(w);
    DrawEdge3D(hdc, &page, 0);
    int lh = (int)font_height() + 3; g_vis = (th - 8) / lh; if (g_vis < 1) g_vis = 1;
    int ar = 0, ac = 0, br = 0, bc = 0, sel = sel_active(); if (sel) sel_order(&ar, &ac, &br, &bc);
    for (int r = 0; r < g_vis && TOP + r < NL; r++) {
        int L = TOP + r, yy = ty + 4 + r * lh;
        SetTextColor(hdc, INK); TextOut(hdc, tx + 5, yy, T[L], WC - 1);
        if (sel && L >= ar && L <= br) {                        /* draw the selection highlight */
            int s0 = (L == ar) ? ac : 0, s1 = (L == br) ? bc : (int)strlen(T[L]);
            int xs = tx + 5 + px_of(L, s0), xe = tx + 5 + px_of(L, s1);
            if (L < br) xe += 5;                               /* show the trailing newline selected */
            RECT sr = { xs, yy, xe, yy + lh - 2 };
            HBRUSH sb = CreateSolidBrush(RGB(0,0,0x80)); FillRect(hdc, &sr, sb); DeleteObject(sb);
            char sub[WC]; int m = 0; for (int c = s0; c < s1 && T[L][c]; c++) sub[m++] = T[L][c]; sub[m] = 0;
            SetTextColor(hdc, WHITE); TextOut(hdc, xs, yy, sub, WC - 1);
        }
    }
    /* caret */
    if (CR >= TOP && CR < TOP + g_vis && wm_caret_on()) {
        char pre[WC]; int k = 0; while (k < CC && T[CR][k]) { pre[k] = T[CR][k]; k++; } pre[k] = 0;
        int caretx = tx + 5 + (int)font_measure(pre), carety = ty + 4 + (CR - TOP) * lh;
        RECT cr = { caretx, carety, caretx + 2, carety + (int)font_height() };
        HBRUSH cb = CreateSolidBrush(RGB(0,0,0xC0)); FillRect(hdc, &cr, cb); DeleteObject(cb);
    }
    /* status: Ln, Col */
    SetTextColor(hdc, INK);
    { char st[40]; int p = 0; const char *a="Ln "; for(int i=0;a[i];i++)st[p++]=a[i];
      int v=CR+1; char t[8]; int n=0; if(!v)t[n++]='0'; while(v){t[n++]=(char)('0'+v%10);v/=10;} while(n)st[p++]=t[--n];
      const char *b=", Col "; for(int i=0;b[i];i++)st[p++]=b[i];
      v=CC+1; n=0; if(!v)t[n++]='0'; while(v){t[n++]=(char)('0'+v%10);v/=10;} while(n)st[p++]=t[--n]; st[p]=0;
      TextOut(hdc, 6, ch - 13, st, 40); }
    EndPaint(hwnd, hdc);
}

enum { IDM_NEW=1, IDM_OPEN=2, IDM_SAVE=3, IDM_SAVEAS=4, IDM_EXIT=5, IDM_CUT=10, IDM_COPY=11, IDM_PASTE=12, IDM_ABOUT=20 };
static const MenuItemW32 M_file[] = { {"New",IDM_NEW},{"Open...",IDM_OPEN},{"Save",IDM_SAVE},{"Save As...",IDM_SAVEAS},{"",-1},{"Exit",IDM_EXIT} };
static const MenuItemW32 M_edit[] = { {"Cut",IDM_CUT},{"Copy",IDM_COPY},{"Paste",IDM_PASTE} };
static const MenuItemW32 M_help[] = { {"About WordPad",IDM_ABOUT} };
static const MenuW32 WP_MENU[] = { {"File",M_file,6},{"Edit",M_edit,3},{"Help",M_help,1} };

static LRESULT CALLBACK WP_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetMenuW32(hwnd, WP_MENU, 3); SetWindowIcon(hwnd, IC_DOC); return 0;
        case WM_PAINT: wp_paint(hwnd); return 0;
        case WM_CHAR: {
            char c = (char)wParam;
            switch (c) {                                     /* standard Win32 accelerators */
                case 14: do_tb(TB_NEW);  return 0;           /* Ctrl+N */
                case 15: do_tb(TB_OPEN); return 0;           /* Ctrl+O */
                case 19: do_tb(TB_SAVE); return 0;           /* Ctrl+S (Save As) */
                case 24: wp_cut();   return 0;               /* Ctrl+X */
                case 3:  wp_copy();  return 0;               /* Ctrl+C */
                case 22: wp_paste(); return 0;               /* Ctrl+V */
            }
            wp_key(c); return 0;
        }
        case WM_LBUTTONDOWN: {
            int rx = GET_X_LPARAM(lParam), ry = GET_Y_LPARAM(lParam);
            if (ry >= 3 && ry < 25) { for (int i = 0; i < NTB; i++) { int x, w; tb_rect(i, &x, &w); if (rx >= x && rx < x + w) { do_tb(TB[i].id); return 0; } } }
            else if (ry >= TBH + 2) {                              /* click positions the caret + sets the anchor */
                int lh = (int)font_height() + 3; int r = TOP + (ry - (TBH + 6)) / lh;
                if (r >= 0 && r < NL) { CR = r; CC = col_at(CR, rx - 9); SR = CR; SC = CC; }   /* text starts at tx+5 = 9 */
            }
            return 0;
        }
        case WM_MOUSEMOVE: {                                        /* drag to extend the selection */
            if (!(wParam & MK_LBUTTON)) return 0;
            int rx = GET_X_LPARAM(lParam), ry = GET_Y_LPARAM(lParam);
            if (ry >= TBH + 2 && SR >= 0) {
                int lh = (int)font_height() + 3; int r = TOP + (ry - (TBH + 6)) / lh;
                if (r < 0) r = 0; if (r >= NL) r = NL - 1;
                CR = r; CC = col_at(CR, rx - 9);
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_NEW:  do_tb(TB_NEW); break;
                case IDM_OPEN: do_tb(TB_OPEN); break;
                case IDM_SAVE: wp_save(); break;
                case IDM_SAVEAS: do_tb(TB_SAVE); break;
                case IDM_EXIT: DestroyWindow(hwnd); break;
                case IDM_CUT: wp_cut(); break;
                case IDM_COPY: wp_copy(); break;
                case IDM_PASTE: wp_paste(); break;
                case IDM_ABOUT: MessageBoxW32(hwnd, "Debian95 WordPad\nA Win32 document editor\n(WndProc + GDI + toolbar).", "About WordPad", MB_ICONINFO); break;
            }
            return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void wordpad_open(const char *name) {
    if (name && name[0]) { wp_load(name); wm_add_recent(name); wp_title(name); }
    else { wp_clear(); const char *u="(untitled)"; int i=0; for(;u[i]&&i<15;i++)NAME[i]=u[i]; NAME[i]=0; wp_title("Document"); }
    HWND h = CreateAppWindow(120, 100, 600, 400, TITLE, WP_WndProc, 0);
    (void)h;
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
static void wordpad_open_new(void) { wordpad_open(0); }
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 27, "WordPad", IC_DOC, 0, wordpad_open_new, -1, wordpad_open };
#endif
