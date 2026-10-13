/* Debian95 — a small text editor app (a managed window). Loads a file into an
 * in-memory buffer and lets you edit it with the keyboard. (Saving to disk
 * awaits FAT write support.) One editor instance for now. */
#include <debian95/wm.h>
#include <debian95/icons.h>
#include <debian95/comctl.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/nt.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include <debian95/win32.h>     /* COMDLG32: GetOpenFileName / GetSaveFileName */

#define ELINES 64
#define ECOLS  128

static char   g_buf[ELINES][ECOLS];
static int    g_nl;          /* number of lines */
static int    g_cr, g_cc;    /* cursor row / col */
static char   g_title[40];
static char   g_name[16];
static int    g_saved;       /* 1 = last action was a successful save */
static int    g_wrap;        /* Edit > Word Wrap */
static Window *g_win;
static void ed_close(void) {  g_win = 0; }

void rtc_time(int *hh, int *mm);   /* arch clock */
void sys_date(int *y, int *mo, int *d, int *dow, int *hh, int *mm, int *ss);   /* shims */
static void ed_key(char c);
/* build "H:MM MM/DD/YYYY" (classic Notepad F5 / .LOG stamp) and type it in */
static void ed_insert_timestamp(void) {
    int y = 2026, mo = 1, d = 1, dow = 0, hh = 0, mm = 0, ss = 0;
    sys_date(&y, &mo, &d, &dow, &hh, &mm, &ss);
    char s[32]; int k = 0;
    if (hh >= 10) s[k++] = (char)('0' + hh / 10); s[k++] = (char)('0' + hh % 10); s[k++] = ':';
    s[k++] = (char)('0' + mm / 10); s[k++] = (char)('0' + mm % 10); s[k++] = ' ';
    if (mo >= 10) s[k++] = (char)('0' + mo / 10); s[k++] = (char)('0' + mo % 10); s[k++] = '/';
    if (d  >= 10) s[k++] = (char)('0' + d  / 10); s[k++] = (char)('0' + d  % 10); s[k++] = '/';
    s[k++] = (char)('0' + (y / 1000) % 10); s[k++] = (char)('0' + (y / 100) % 10);
    s[k++] = (char)('0' + (y / 10) % 10); s[k++] = (char)('0' + y % 10); s[k] = 0;
    for (int i = 0; s[i]; i++) ed_key(s[i]);
}

static void ed_clear(void) {
    for (int i = 0; i < ELINES; i++) g_buf[i][0] = 0;
    g_nl = 1; g_cr = 0; g_cc = 0;
}

static void ed_load(const char *name) {
    ed_clear();
    u8 *data; u32 sz;
    if (FsReadAlloc(name, &data, &sz) != 0) return;
    int r = 0, c = 0;
    for (u32 i = 0; i < sz && r < ELINES; i++) {
        char ch = (char)data[i];
        if (ch == '\r') continue;
        if (ch == '\n') { g_buf[r][c] = 0; r++; c = 0; if (r < ELINES) g_buf[r][0] = 0; continue; }
        if (ch == '\t') ch = ' ';
        if (c < ECOLS - 1 && ch >= 32) g_buf[r][c++] = ch;
    }
    if (r < ELINES) g_buf[r][c] = 0;
    g_nl = r + 1;
    g_cr = 0; g_cc = 0;
    kfree(data);
}

static void ed_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, RGB(0xFF, 0xFF, 0xFF));   /* white page */
    wm_bevel_sunken(cx, cy, cw, ch);
    int lh = font_height() + 3;
    int textw = cw - 14 - 16;                              /* leave room for the scroll bar */
    int row = 0;
    for (int i = 0; i < g_nl && (4 + row * lh) < ch - 16; i++) {
        if (!g_wrap) {
            fb_text_t(cx + 6, cy + 4 + row * lh, g_buf[i], RGB(0x10, 0x10, 0x10));
            row++;
        } else {
            /* soft-wrap the logical line into as many display rows as needed */
            char seg[ECOLS]; int s = 0;
            for (int k = 0; g_buf[i][k]; k++) {
                seg[s] = g_buf[i][k]; seg[s + 1] = 0;
                if ((int)font_measure(seg) > textw && s > 0) {
                    seg[s] = 0;
                    fb_text_t(cx + 6, cy + 4 + row * lh, seg, RGB(0x10, 0x10, 0x10));
                    row++; seg[0] = g_buf[i][k]; seg[1] = 0; s = 0;
                }
                s++;
            }
            fb_text_t(cx + 6, cy + 4 + row * lh, seg, RGB(0x10, 0x10, 0x10));
            row++;
        }
    }
    /* caret */
    char cur[ECOLS];
    int k = 0; while (k < g_cc && g_buf[g_cr][k]) { cur[k] = g_buf[g_cr][k]; k++; }
    cur[k] = 0;
    int caretx = cx + 6 + (int)font_measure(cur);
    int carety = cy + 4 + g_cr * lh;
    if (wm_caret_on()) fb_fill_rect(caretx, carety, 2, font_height(), RGB(0x00, 0x00, 0xC0));

    /* vertical scroll bar down the right edge (Win95 Notepad always shows one) */
    int sbh = ch - 18, sbx = cx + cw - 18;
    int vis = (sbh - 32) / lh; if (vis < 1) vis = 1;
    int total = g_nl < 1 ? 1 : g_nl;
    int track = sbh - 32; if (track < 8) track = 8;
    int th_h = total <= vis ? track : track * vis / total;
    cc_scrollbar_v(sbx, cy + 2, sbh, 0, th_h);

    /* status footer: "Ln X, Col Y" (Win95 Notepad shows the caret position) */
    fb_fill_rect(cx + 2, cy + ch - 14, cw - 4, 12, RGB(0xC0, 0xC0, 0xC0));
    char st[40]; int p = 0;
    const char *lnp = "Ln "; for (int i = 0; lnp[i]; i++) st[p++] = lnp[i];
    { int v = g_cr + 1; char t[8]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) st[p++] = t[--n]; }
    const char *cl = ", Col "; for (int i = 0; cl[i]; i++) st[p++] = cl[i];
    { int v = g_cc + 1; char t[8]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) st[p++] = t[--n]; }
    st[p] = 0;
    fb_text_t(cx + 6, cy + ch - 13, st, RGB(0x10, 0x10, 0x10));
    if (g_saved) fb_text_t(cx + cw - 90, cy + ch - 13, "saved", RGB(0x10, 0x10, 0x10));
}

static void ed_save(void) {
    /* flatten the line buffer into bytes with newlines */
    static u8 out[ELINES * ECOLS];
    u32 n = 0;
    for (int r = 0; r < g_nl; r++) {
        for (int i = 0; g_buf[r][i] && n < sizeof(out); i++) out[n++] = (u8)g_buf[r][i];
        if (r < g_nl - 1 && n < sizeof(out)) out[n++] = '\n';
    }
    g_saved = (FsWriteFile(g_name, out, n) == 0);
}

static void ed_key(char c) {
    unsigned char uc = (unsigned char)c;
    if (uc == 0x91) { if (g_cr > 0) g_cr--; goto clamp; }           /* up */
    if (uc == 0x92) { if (g_cr < g_nl - 1) g_cr++; goto clamp; }    /* down */
    if (uc == 0x93) {                                                /* left */
        if (g_cc > 0) g_cc--;
        else if (g_cr > 0) { g_cr--; g_cc = (int)strlen(g_buf[g_cr]); }
        return;
    }
    if (uc == 0x94) {                                                /* right */
        int len = (int)strlen(g_buf[g_cr]);
        if (g_cc < len) g_cc++;
        else if (g_cr < g_nl - 1) { g_cr++; g_cc = 0; }
        return;
    }

    if (uc == 0x85) { ed_insert_timestamp(); return; }   /* F5: insert time/date */
    g_saved = 0;
    if (c == 19) { ed_save(); return; }        /* Ctrl+S */
    if (c == '\n') {
        if (g_nl < ELINES) {
            for (int r = g_nl; r > g_cr + 1; r--)
                memcpy(g_buf[r], g_buf[r - 1], ECOLS);
            /* split current line at cursor */
            int i = 0; while (g_buf[g_cr][g_cc + i]) { g_buf[g_cr + 1][i] = g_buf[g_cr][g_cc + i]; i++; }
            g_buf[g_cr + 1][i] = 0;
            g_buf[g_cr][g_cc] = 0;
            g_nl++; g_cr++; g_cc = 0;
        }
    } else if (c == 8) {
        if (g_cc > 0) {
            int len = (int)strlen(g_buf[g_cr]);
            for (int i = g_cc - 1; i < len; i++) g_buf[g_cr][i] = g_buf[g_cr][i + 1];
            g_cc--;
        }
    } else if (c >= 32 && c < 127) {
        int len = (int)strlen(g_buf[g_cr]);
        if (len < ECOLS - 2 && g_cc <= len) {
            for (int i = len; i >= g_cc; i--) g_buf[g_cr][i + 1] = g_buf[g_cr][i];
            g_buf[g_cr][g_cc] = c;
            g_cc++;
        }
    }
    return;
clamp:
    { int len = (int)strlen(g_buf[g_cr]); if (g_cc > len) g_cc = len; }
}

/* click to position the caret (row + nearest column). Non-wrap mode only —
 * wrapped rows don't map 1:1 to lines, so clicks there fall through. */
static int ed_col_at(int line, int px) {
    if (px <= 0) return 0; char pre[ECOLS]; int prev = 0;
    for (int c = 0; g_buf[line][c]; c++) { pre[c] = g_buf[line][c]; pre[c+1] = 0; int w = (int)font_measure(pre);
        if (px < (prev + w) / 2) return c; prev = w; }
    return (int)strlen(g_buf[line]);
}
static void ed_click(int rx, int ry) {
    if (g_wrap) return;
    int lh = (int)font_height() + 3; int r = (ry - 4) / lh;
    if (r < 0) r = 0; if (r >= g_nl) r = g_nl - 1; if (r < 0) return;
    g_cr = r; g_cc = ed_col_at(r, rx - 6);            /* text is drawn at cx + 6 */
}

/* ---- Find dialog (Search > Find...) ---- */
static Window *g_fw; static char g_find[40]; static int g_findlen;
static void find_next(void) {
    if (!g_findlen) return;
    int r = g_cr, c = g_cc + 1;
    for (int pass = 0; pass < 2; pass++) {
        for (; r < g_nl; r++, c = 0) {
            const char *line = g_buf[r]; int len = (int)strlen(line);
            for (; c <= len - g_findlen; c++) {
                int k = 0; while (k < g_findlen && line[c + k] == g_find[k]) k++;
                if (k == g_findlen) { g_cr = r; g_cc = c; return; }
            }
        }
        r = 0; c = 0;                                      /* wrap to the top */
    }
}
static void fw_close(void) { g_fw = 0; }
static void fw_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, RGB(0xC0, 0xC0, 0xC0));
    cc_label(cx + 10, cy + 16, "Find what:");
    cc_edit(cx + 78, cy + 14, cw - 88 - 80, 20, g_find, 1);
    cc_defbutton(cx + cw - 76, cy + 12, 66, 22, "Find Next", 0);
    cc_button(cx + cw - 76, cy + 40, 66, 22, "Cancel", 0);
    (void)ch;
}
static void fw_key(char c) {
    if (c == '\n' || c == '\r') { find_next(); return; }
    if ((c == 8 || c == 0x7F) && g_findlen) g_find[--g_findlen] = 0;
    else if (c >= 32 && g_findlen < 38) { g_find[g_findlen++] = c; g_find[g_findlen] = 0; }
}
static void fw_click(int rx, int ry) {
    int x, y, cw, ch; wm_client_rect(g_fw, &x, &y, &cw, &ch); (void)x; (void)y; (void)ch;
    if (rx >= cw - 76 && rx < cw - 10) {
        if (ry >= 12 && ry < 34) find_next();
        else if (ry >= 40 && ry < 62) wm_hide(g_fw);
    }
}
static void ed_find_open(void) {
    if (!g_fw) g_fw = wm_add_custom(280, 200, 340, 92, "Find", fw_paint, fw_key, fw_click);
    wm_set_close(g_fw, fw_close);
    wm_set_text_cursor(g_fw);
    wm_show(g_fw);
}

/* ---- Replace dialog (Search > Replace...) ---- */
static Window *g_rw; static char g_repl[40]; static int g_repllen, g_rw_field;
static int match_at(int r, int c) {
    const char *line = g_buf[r]; int len = (int)strlen(line);
    if (c > len - g_findlen || g_findlen == 0) return 0;
    for (int k = 0; k < g_findlen; k++) if (line[c + k] != g_find[k]) return 0; return 1;
}
static void repl_here(void) {                         /* replace the match at (g_cr,g_cc) */
    char *line = g_buf[g_cr]; int len = (int)strlen(line);
    if (len - g_findlen + g_repllen >= ECOLS - 1) return;
    if (g_repllen > g_findlen) for (int i = len; i >= g_cc + g_findlen; i--) line[i + (g_repllen - g_findlen)] = line[i];
    else if (g_repllen < g_findlen) for (int i = g_cc + g_findlen; i <= len; i++) line[i - (g_findlen - g_repllen)] = line[i];
    for (int k = 0; k < g_repllen; k++) line[g_cc + k] = g_repl[k];
    g_cc += g_repllen; g_saved = 0;
}
static void do_replace(void) { if (!g_findlen) return; if (match_at(g_cr, g_cc)) repl_here(); find_next(); }
static void do_replace_all(void) {
    if (!g_findlen) return;
    for (int r = 0; r < g_nl; r++) { int c = 0;
        for (;;) { const char *line = g_buf[r]; int len = (int)strlen(line);
            if (c > len - g_findlen) break;
            int k = 0; while (k < g_findlen && line[c + k] == g_find[k]) k++;
            if (k == g_findlen) { g_cr = r; g_cc = c; repl_here(); c += g_repllen ? g_repllen : 1; } else c++;
        } }
}
static void rw_close(void) { g_rw = 0; }
static void rw_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, RGB(0xC0, 0xC0, 0xC0));
    cc_label(cx + 10, cy + 14, "Find what:");
    cc_edit(cx + 96, cy + 12, cw - 106 - 90, 20, g_find, g_rw_field == 0);
    cc_label(cx + 10, cy + 40, "Replace with:");
    cc_edit(cx + 96, cy + 38, cw - 106 - 90, 20, g_repl, g_rw_field == 1);
    int bx = cx + cw - 84;
    cc_defbutton(bx, cy + 10, 76, 20, "Find Next", 0);
    cc_button(bx, cy + 34, 76, 20, "Replace", 0);
    cc_button(bx, cy + 58, 76, 20, "Replace All", 0);
    cc_button(bx, cy + 82, 76, 20, "Cancel", 0);
    (void)ch;
}
static void rw_key(char c) {
    if (c == '\t') { g_rw_field ^= 1; return; }
    if (c == '\n' || c == '\r') { find_next(); return; }
    if (g_rw_field == 0) { if ((c == 8 || c == 0x7F) && g_findlen) g_find[--g_findlen] = 0;
        else if (c >= 32 && g_findlen < 38) { g_find[g_findlen++] = c; g_find[g_findlen] = 0; } }
    else { if ((c == 8 || c == 0x7F) && g_repllen) g_repl[--g_repllen] = 0;
        else if (c >= 32 && g_repllen < 38) { g_repl[g_repllen++] = c; g_repl[g_repllen] = 0; } }
}
static void rw_click(int rx, int ry) {
    int x, y, cw, ch; wm_client_rect(g_rw, &x, &y, &cw, &ch); (void)x; (void)y; (void)ch;
    if (ry >= 12 && ry < 30) g_rw_field = 0;               /* click a field to focus it */
    else if (ry >= 38 && ry < 56) g_rw_field = 1;
    if (rx >= cw - 84 && rx < cw - 8) {
        if (ry >= 10 && ry < 30) find_next();
        else if (ry >= 34 && ry < 54) do_replace();
        else if (ry >= 58 && ry < 78) do_replace_all();
        else if (ry >= 82 && ry < 102) wm_hide(g_rw);
    }
}
static void ed_replace_open(void) {
    if (!g_rw) g_rw = wm_add_custom(240, 180, 380, 130, "Replace", rw_paint, rw_key, rw_click);
    wm_set_close(g_rw, rw_close);
    wm_set_text_cursor(g_rw);
    wm_show(g_rw);
}

/* update the caption to "<name> - Notepad" (the window holds a pointer to g_title) */
static void ed_set_title(const char *name) {
    int t = 0, s = 0; while (name[s] && t < 20) g_title[t++] = name[s++];
    const char *suf = " - Notepad"; for (int i = 0; suf[i] && t < 39; i++) g_title[t++] = suf[i];
    g_title[t] = 0;
}
/* COMDLG32 completion callbacks: open/save the file the user picked */
static void ed_do_open(const char *name) {
    int i = 0; while (name[i] && i < 15) { g_name[i] = name[i]; i++; } g_name[i] = 0;
    ed_load(name); wm_add_recent(name); ed_set_title(name); g_saved = 0;
}
static void ed_do_saveas(const char *name) {
    int i = 0; while (name[i] && i < 15) { g_name[i] = name[i]; i++; } g_name[i] = 0;
    ed_save(); ed_set_title(name);
}

/* ---- Notepad menu bar ---- */
static const WmMenuItem MI_file[] = {
    { "New", 1 }, { "Open...", 2 }, { "Save", 3 }, { "Save As...", 5 }, { "", -1 }, { "Print", 6 }, { "", -1 }, { "Exit", 4 },
};
static const WmMenuItem MI_edit[] = {
    { "Time/Date", 10 }, { "", -1 }, { "Word Wrap", 11 },
};
static const WmMenuItem MI_search[] = { { "Find...", 30 }, { "Find Next", 31 }, { "Replace...", 32 } };
static const WmMenuItem MI_help[] = { { "About Notepad", 20 } };
static const WmMenu ED_MENUS[] = {
    { "File", MI_file, 8 }, { "Edit", MI_edit, 3 }, { "Search", MI_search, 3 }, { "Help", MI_help, 1 },
};

static void ed_menu(int id) {
    switch (id) {
        case 1:  ed_clear(); g_saved = 0; break;               /* New */
        case 2:  { static OPENFILENAME o; o.title = "Open"; o.filter = 0; o.file[0] = 0;   /* Open... via COMDLG32 */
                   o.onok = ed_do_open; GetOpenFileName(&o); } break;
        case 3:  ed_save(); break;                              /* Save (to the current name) */
        case 5:  { static OPENFILENAME o; o.title = "Save As"; o.filter = "*.TXT";          /* Save As... via COMDLG32 */
                   int i = 0; for (; g_name[i] && i < 40; i++) o.file[i] = g_name[i]; o.file[i] = 0;
                   o.onok = ed_do_saveas; GetSaveFileName(&o); } break;
        case 4:  wm_hide(g_win); break;                         /* Exit */
        case 6:  {                                              /* Print -> spool to C:\Printed */
            static char body[ELINES * ECOLS]; int p = 0;
            for (int i = 0; i < ELINES && g_buf[i][0]; i++) {
                for (int k = 0; g_buf[i][k] && p < (int)sizeof body - 2; k++) body[p++] = g_buf[i][k];
                body[p++] = '\n';
            }
            body[p] = 0;
            print_submit_text(g_name[0] ? g_name : "Untitled.txt", body, (u32)p);
            break;
        }
        case 10: {                                              /* insert Time/Date */
            int hh = 12, mm = 0; rtc_time(&hh, &mm);
            char t[16]; int p = 0;
            if (hh >= 10) t[p++] = (char)('0' + hh / 10); t[p++] = (char)('0' + hh % 10);
            t[p++] = ':'; t[p++] = (char)('0' + mm / 10); t[p++] = (char)('0' + mm % 10);
            t[p++] = ' '; t[p++] = 'P'; t[p++] = 'M'; t[p] = 0;
            for (int i = 0; t[i]; i++) ed_key(t[i]);
            break;
        }
        case 11: g_wrap = !g_wrap; break;                      /* Word Wrap toggle */
        case 30: ed_find_open(); break;                           /* Find... */
        case 31: find_next(); break;                           /* Find Next */
        case 32: ed_replace_open(); break;                     /* Replace... */
        case 20: wm_messagebox("About Notepad",
                     "Debian95 Notepad\nA simple text editor.\nFile / Edit / Help menus.");
                 break;
        default: break;
    }
}

/* dynamic menu state: Word Wrap shows a check mark when on; Find Next is grayed
 * until there is a search term (exactly as Win95 Notepad behaves). */
static int ed_menuflags(int id) {
    if (id == 11) return g_wrap ? 1 : 0;
    if (id == 31) return g_findlen ? 0 : 2;
    return 0;
}

void editor_open(const char *name) {
    { char *getenv(const char *); const char *wv = getenv("DEBIAN95_WRAP"); if (wv && wv[0] == '1') g_wrap = 1; }  /* test */
    int i = 0; while (name[i] && i < 15) { g_name[i] = name[i]; i++; } g_name[i] = 0;
    g_saved = 0;
    wm_add_recent(name);                          /* Start > Documents MRU */
    ed_load(name);
    /* .LOG: if the first line is exactly ".LOG", append a fresh timestamp at the
     * end and park the caret there - the classic Notepad running-log behaviour */
    if (g_buf[0][0] == '.' && g_buf[0][1] == 'L' && g_buf[0][2] == 'O' && g_buf[0][3] == 'G' && g_buf[0][4] == 0) {
        g_cr = g_nl - 1; g_cc = (int)strlen(g_buf[g_cr]);
        ed_key('\n'); ed_insert_timestamp(); ed_key('\n'); g_saved = 0;
    }
    /* title: "<name> - Notepad" */
    int t = 0, s = 0; while (name[s] && t < 20) g_title[t++] = name[s++];
    const char *suf = " - Notepad"; for (int i = 0; suf[i] && t < 39; i++) g_title[t++] = suf[i];
    g_title[t] = 0;

    /* the window keeps a pointer to g_title, so updating its contents is enough */
    if (!g_win) {
        g_win = wm_add_custom(140, 120, 560, 340, g_title, ed_paint, ed_key, ed_click);
        wm_set_icon(g_win, IC_DOC);
        wm_set_close(g_win, ed_close);
        wm_set_text_cursor(g_win);      /* I-beam over the editor text area */
        wm_set_menu(g_win, ED_MENUS, 4, ed_menu);
        wm_set_menuflags(g_win, ed_menuflags);     /* Word Wrap check mark tracks g_wrap */
    }
    wm_show(g_win);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
static void editor_open_default(void) { editor_open("README.TXT"); }   /* Start menu / blank launch */
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 4, "Notepad", IC_DOC, 0, editor_open_default, -1, editor_open };
#endif
