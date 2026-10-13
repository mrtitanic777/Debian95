/* Debian95 — Recycle Bin: an Explorer-style window over a real "Recycled"
 * folder. Deleting a file in the file manager moves it here (recycle_add);
 * the Bin lists each item with its Original Location, Date Deleted, Type and
 * Size, and can Restore items or be emptied. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/icons.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/nt.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include "shellutil.h"

#define FACE  RGB(0xC0,0xC0,0xC0)
#define WHITE RGB(0xFF,0xFF,0xFF)
#define DK    RGB(0x00,0x00,0x00)
#define SEL   RGB(0x00,0x00,0x80)

int  sys_get_setting(const char *key, char *out, int n);   /* persisted settings (shims) */
void sys_set_setting(const char *key, const char *val);
/* Recycle Bin Properties: when set, deletes bypass the Bin (permanent). */
static int g_no_recycle = -1;                /* -1 = not yet loaded */
static int recycle_bypass(void) {
    if (g_no_recycle < 0) { char v[8]; g_no_recycle = (sys_get_setting("no_recycle", v, sizeof v) == 0 && v[0] == '1'); }
    return g_no_recycle;
}

/* ---- tiny string helpers ---- */
static void s_set(char *d, const char *s) { int i=0; for(; s[i]; i++) d[i]=s[i]; d[i]=0; }
static void s_cat(char *d, const char *s) { int l=0; while(d[l])l++; for(int i=0; s[i]; i++) d[l++]=s[i]; d[l]=0; }
static int  s_eq(const char *a, const char *b){ int i=0; for(;;i++){ if(a[i]!=b[i])return 0; if(!a[i])return 1; } }

/* ---- the index: maps a recycled leaf name to its original location ---- */
static void idx_lookup(const char *leaf, char *outdir) {
    outdir[0] = 0; s_set(outdir, "C:\\");
    u8 *data; u32 sz;
    if (FsReadAlloc("Recycled/.index", &data, &sz) != 0) return;
    /* lines: "leaf\torigdir\n" */
    u32 i = 0;
    while (i < sz) {
        char nm[40]; int a = 0; while (i < sz && data[i] != '\t' && data[i] != '\n' && a < 39) nm[a++] = (char)data[i++]; nm[a] = 0;
        char dr[64]; int b = 0; if (i < sz && data[i] == '\t') { i++; while (i < sz && data[i] != '\n' && b < 63) dr[b++] = (char)data[i++]; } dr[b] = 0;
        while (i < sz && data[i] != '\n') i++; if (i < sz) i++;
        if (s_eq(nm, leaf)) { s_set(outdir, dr[0] ? dr : "C:\\"); break; }
    }
    kfree(data);
}
static void idx_remove(const char *leaf) {
    u8 *data; u32 sz;
    if (FsReadAlloc("Recycled/.index", &data, &sz) != 0) return;
    char *out = (char *)kmalloc(sz + 1); if (!out) { kfree(data); return; }
    u32 i = 0, o = 0;
    while (i < sz) {
        u32 ls = i; char nm[40]; int a = 0; while (i < sz && data[i] != '\t' && data[i] != '\n' && a < 39) nm[a++] = (char)data[i++]; nm[a] = 0;
        while (i < sz && data[i] != '\n') i++; if (i < sz) i++;
        if (!s_eq(nm, leaf)) { for (u32 k = ls; k < i; k++) out[o++] = (char)data[k]; }
    }
    FsWriteFile("Recycled/.index", (const u8 *)out, o);
    kfree(out); kfree(data);
}

/* Move a file into the Recycle Bin, recording its original location. */
void recycle_add(const char *fullpath) {
    if (!fullpath || !fullpath[0]) return;
    if (recycle_bypass()) { FsDelete(fullpath); return; }   /* "Remove immediately" -> permanent */
    /* split leaf / dir */
    int L = 0; while (fullpath[L]) L++;
    int cut = L; while (cut > 0 && fullpath[cut-1] != '/') cut--;
    const char *leaf = fullpath + cut;
    char origdir[64]; origdir[0] = 0; s_cat(origdir, "C:\\");
    for (int k = 0; k < cut - 1; k++) { char c = fullpath[k] == '/' ? '\\' : fullpath[k]; char s[2] = { c, 0 }; s_cat(origdir, s); }
    /* copy bytes into Recycled/, then remove the original */
    u8 *data; u32 sz;
    if (FsReadAlloc(fullpath, &data, &sz) != 0) return;
    FsMkdir("Recycled");
    char dst[80]; dst[0] = 0; s_cat(dst, "Recycled/"); s_cat(dst, leaf);
    FsWriteFile(dst, data, sz); kfree(data);
    FsDelete(fullpath);
    /* append "leaf\torigdir\n" to the index */
    u8 *idx = 0; u32 isz = 0; FsReadAlloc("Recycled/.index", &idx, &isz);
    u32 need = isz + 128; char *n = (char *)kmalloc(need); if (!n) { if (idx) kfree(idx); return; }
    u32 o = 0; for (u32 k = 0; k < isz; k++) n[o++] = (char)idx[k]; if (idx) kfree(idx);
    for (int k = 0; leaf[k]; k++) n[o++] = leaf[k]; n[o++] = '\t';
    for (int k = 0; origdir[k]; k++) n[o++] = origdir[k]; n[o++] = '\n';
    FsWriteFile("Recycled/.index", (const u8 *)n, o); kfree(n);
}

/* ---- window ---- */
static HWND g_win;            /* the single Recycle Bin window (Win32 veneer) */
static FsEntry g_ents[64]; static int g_count;
static int g_sel = -1, g_top;

#define TBH   28
#define ADRH  24
#define STATH 20
#define RROW  17
static const int COLW[5] = { 150, 160, 120, 110, 70 };  /* Name/Orig/Date Deleted/Type/Size */

static void re_refresh(void) { g_count = FsListDir("Recycled", g_ents, 64); if (g_sel >= g_count) g_sel = -1; }

static void tool_btn(int x, int y, char g) {
    fb_fill_rect(x, y, 23, 22, FACE); wm_bevel_raised(x, y, 23, 22);
    u32 c = DK; int cx = x + 11, cy = y + 11;
    switch (g) {
    case 'u': for (int i=0;i<5;i++) fb_fill_rect(cx-i, cy-4+i, 2*i+1, 1, c); fb_fill_rect(cx-1, cy-2, 3, 8, c); break;   /* up */
    case 'x': fb_fill_rect(x+7, y+7, 9, 3, RGB(0x40,0x40,0x40)); break;                                                   /* cut */
    case 'c': fb_rect(x+7, y+6, 8, 9, c); fb_rect(x+9, y+8, 8, 9, c); break;                                              /* copy */
    case 'v': fb_fill_rect(x+7, y+6, 9, 11, RGB(0xF0,0xF0,0xF0)); wm_bevel_raised(x+7,y+6,9,11); break;                    /* paste */
    case 'd': for (int i=0;i<7;i++){ fb_pixel(x+7+i,y+7+i,RGB(0xC0,0,0)); fb_pixel(x+13-i,y+7+i,RGB(0xC0,0,0)); } break;   /* delete (X) */
    case 'p': fb_rect(x+7,y+6,9,11,c); fb_fill_rect(x+9,y+8,5,1,c); fb_fill_rect(x+9,y+10,5,1,c); break;                   /* properties */
    default: break;
    }
}

static void re_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    /* toolbar */
    fb_fill_rect(cx, cy, cw, TBH, FACE);
    int tx = cx + 4, ty = cy + 3;
    const char gs[] = { 'u','x','c','v','d','p' };
    for (int i = 0; i < 6; i++) { tool_btn(tx, ty, gs[i]); tx += 25; if (i==0||i==3) { fb_fill_rect(tx, ty, 1, 22, RGB(0x80,0x80,0x80)); tx += 5; } }
    /* address bar */
    int ay = cy + TBH;
    cc_label(cx + 6, ay + 5, "Address");
    fb_fill_rect(cx + 60, ay + 2, 200, 19, WHITE); wm_bevel_sunken(cx + 60, ay + 2, 200, 19);
    fb_text_t(cx + 66, ay + 5, "Recycle Bin", DK);
    /* column header */
    int lx = cx + 2, ly = ay + ADRH, lw = cw - 4;
    int hx = lx;
    static const char *H[5] = { "Name", "Original Location", "Date Deleted", "Type", "Size" };
    for (int c = 0; c < 5; c++) { fb_fill_rect(hx, ly, COLW[c], 17, FACE); wm_bevel_raised(hx, ly, COLW[c], 17); fb_text_t(hx + 5, ly + 3, H[c], DK); hx += COLW[c]; }
    /* rows */
    int by = ly + 17, bh = (cy + ch - STATH) - by - 2, fh = (int)font_height();
    fb_fill_rect(lx, by, lw, bh, WHITE); wm_bevel_sunken(lx, by, lw, bh);
    int rows = (bh - 4) / RROW;
    for (int r = 0; r < rows && g_top + r < g_count; r++) {
        int i = g_top + r, ry = by + 2 + r * RROW, colx = lx + 2;
        char od[64]; idx_lookup(g_ents[i].name, od);
        if (i == g_sel) fb_fill_rect(lx + 2, ry, COLW[0] - 2, RROW, SEL);
        fb_text_t(colx + 20, ry + (RROW - fh)/2, g_ents[i].name, i == g_sel ? WHITE : DK); colx += COLW[0];
        fb_text_t(colx + 4, ry + (RROW - fh)/2, od, DK); colx += COLW[1];
        { char d[24]; shell_date(g_ents[i].mtime, d); fb_text_t(colx + 4, ry + (RROW - fh)/2, d, DK); } colx += COLW[2];
        fb_text_t(colx + 4, ry + (RROW - fh)/2, shell_type(g_ents[i].name, g_ents[i].is_dir), DK); colx += COLW[3];
        if (!g_ents[i].is_dir) { char s[16]; shell_size(g_ents[i].size, s); fb_text_t(colx + COLW[4] - (int)font_measure(s) - 8, ry + (RROW - fh)/2, s, DK); }
    }
    /* status bar */
    int syb = cy + ch - STATH;
    fb_fill_rect(cx, syb, cw, STATH, FACE);
    char st[40]; char nb[12]; int v = g_count, k = 0, j; char t[12];
    if (!v) t[k++]='0'; while (v){ t[k++]=(char)('0'+v%10); v/=10; } j=0; while (k) nb[j++]=t[--k]; nb[j]=0;
    st[0]=0; s_cat(st, nb); s_cat(st, g_count == 1 ? " object" : " object(s)");
    fb_fill_rect(cx + 2, syb, 220, 18, FACE); wm_bevel_sunken(cx + 2, syb, 220, 18);
    fb_text_t(cx + 8, syb + 3, st, DK);
}

static int row_hit(int rx, int ry, int cy) {
    int by = cy + TBH + ADRH + 17 + 2;
    int rel = (ry + cy) - by; if (rel < 0) return -1;
    int r = rel / RROW, i = g_top + r;
    return (i >= 0 && i < g_count) ? i : -1;
}
static void re_click(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
    int i = row_hit(rx, ry, cy);
    g_sel = i;
}
static void re_restore(int i) {
    if (i < 0 || i >= g_count) return;
    char leaf[40]; s_set(leaf, g_ents[i].name);
    char od[64]; idx_lookup(leaf, od);
    /* rebuild the original relative path from "C:\dir\..." */
    char rel[80]; rel[0] = 0; int started = 0;
    for (int k = 3; od[k]; k++) { char c = od[k] == '\\' ? '/' : od[k]; if (c=='/'&&!started) continue; started = 1; char s[2]={c,0}; s_cat(rel, s); }
    if (rel[0] && rel[(int)strlen(rel)-1] != '/') s_cat(rel, "/"); s_cat(rel, leaf);
    char src[80]; src[0]=0; s_cat(src, "Recycled/"); s_cat(src, leaf);
    u8 *data; u32 sz; if (FsReadAlloc(src, &data, &sz) == 0) { FsWriteFile(rel, data, sz); kfree(data); }
    FsDelete(src); idx_remove(leaf);
    re_refresh();
}
static void re_delete(int i) {
    if (i < 0 || i >= g_count) return;
    char src[80]; src[0]=0; s_cat(src, "Recycled/"); s_cat(src, g_ents[i].name);
    FsDelete(src); idx_remove(g_ents[i].name);
    re_refresh();
}
static void re_do_empty(void) { while (g_count > 0) re_delete(0); }
static void re_empty(void) {
    if (g_count <= 0) return;
    char msg[80]; msg[0] = 0; s_cat(msg, "Are you sure you want to delete\nthese ");
    { char n[6]; int v = g_count, k = 0; if (!v) n[k++]='0'; char t[6]; int m=0; while(v){t[m++]=(char)('0'+v%10);v/=10;} while(m)n[k++]=t[--m]; n[k]=0; s_cat(msg, n); }
    s_cat(msg, " items?");
    wm_confirm("Empty Recycle Bin", msg, re_do_empty);
}

/* ---- Recycle Bin Properties (right-click > Properties) ---- */
static HWND g_rp; static int g_rp_confirm = 1;
static void rp_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    static const char *tabs[] = { "Global" };
    cc_tabs(cx + 6, cy + 6, 90, tabs, 1, 0);
    int th = (int)font_height() + 8;
    int px = cx + 10, py = cy + 6 + th + 8, pw = cw - 20, ph = ch - (6 + th) - 52;
    fb_fill_rect(px, py, pw, ph, FACE); wm_bevel_raised(px, py, pw, ph);
    cc_checkbox(px + 16, py + 16, "Do not move files to the Recycle Bin.", recycle_bypass());
    cc_label(px + 32, py + 34, "Remove files immediately when deleted.");
    fb_fill_rect(px + 12, py + 58, pw - 24, 1, RGB(0x80,0x80,0x80)); fb_fill_rect(px + 12, py + 59, pw - 24, 1, WHITE);
    cc_label(px + 16, py + 70, "Maximum size of Recycle Bin (% of drive):");
    { int tx = px + 20, tw = pw - 60; fb_fill_rect(tx, py + 92, tw, 4, FACE); wm_bevel_sunken(tx, py + 92, tw, 4);
      int t = 10 * (tw - 10) / 100; fb_fill_rect(tx + t, py + 86, 10, 16, FACE); wm_bevel_raised(tx + t, py + 86, 10, 16);
      cc_label(px + 16, py + 106, "10%   (reserved for deleted files)"); }
    cc_checkbox(px + 16, py + 128, "Display delete confirmation dialog", g_rp_confirm);
    int by = cy + ch - 32, bw = 74;
    cc_defbutton(cx + cw - bw * 2 - 15, by, bw, 24, "OK", 0);
    cc_button(cx + cw - bw - 8, by, bw, 24, "Cancel", 0);
}
static void rp_click(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_rp, &cx, &cy, &cw, &ch); (void)cx; (void)cy;
    int th = (int)font_height() + 8; int px = 10, py = 6 + th + 8;
    if (rx >= px + 16 && rx < px + 300) {
        if (ry >= py + 12 && ry < py + 30) { g_no_recycle = !recycle_bypass(); sys_set_setting("no_recycle", g_no_recycle ? "1" : "0"); return; }
        if (ry >= py + 124 && ry < py + 142) { g_rp_confirm = !g_rp_confirm; sys_set_setting("confirm_del", g_rp_confirm ? "1" : "0"); return; }
    }
    int by = ch - 32, bw = 74;
    if (ry >= by && ry < by + 24 && rx >= cw - bw * 2 - 15) DestroyWindow(g_rp);
}
/* ---- Win32 veneer: Properties dialog WndProc ---- */
static LRESULT CALLBACK RecycleProps_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: g_rp = hwnd; return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            rp_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: rp_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_DESTROY: g_rp = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
static void recycle_props(void) {
    { char v[8]; if (sys_get_setting("confirm_del", v, sizeof v) == 0 && v[0]) g_rp_confirm = v[0] == '1'; }
    if (g_rp) return;                                     /* single instance */
    CreateAppWindow(200, 130, 340, 260, "Recycle Bin Properties", RecycleProps_WndProc, 0);
}
static void re_ctx(int act) {
    if (act == 1) re_restore(g_sel);
    else if (act == 3) re_delete(g_sel);
    else if (act == 4) recycle_props();
    else if (act == 5) re_empty();
}
static void re_rclick(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
    int i = row_hit(rx, ry, cy);
    if (i < 0) {                                  /* empty area: bin-wide actions */
        static const char *E[] = { "Empty Recycle Bin", "-", "Properties" };
        static const int   EA[] = { 5, -1, 4 };
        wm_context_menu(E, EA, 3, re_ctx); return;
    }
    g_sel = i;
    static const char *M[] = { "Restore", "-", "Cut", "-", "Delete", "-", "Properties" };
    static const int   A[] = { 1, -1, 0, -1, 3, -1, 4 };
    wm_context_menu(M, A, 7, re_ctx);
}

static const MenuItemW32 RE_file[] = { { "Restore", 1 }, { "Empty Recycle Bin", 5 }, { "", -1 }, { "Close", 9 } };
static const MenuItemW32 RE_edit[] = { { "Select All", 0 } };
static const MenuItemW32 RE_view[] = { { "Large Icons", 0 }, { "Details", 0 } };
static const MenuItemW32 RE_help[] = { { "About", 0 } };
static const MenuW32 RE_MENUS[] = { { "File", RE_file, 4 }, { "Edit", RE_edit, 1 }, { "View", RE_view, 2 }, { "Help", RE_help, 1 } };

/* ---- Win32 veneer: main window WndProc ---- */
static LRESULT CALLBACK Recycle_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, RE_MENUS, 4);
            SetWindowIcon(hwnd, IC_RECYCLE);
            SetWindowSizeGrip(hwnd);
            g_win = hwnd;
            re_refresh();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            re_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: re_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_RBUTTONDOWN: re_rclick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case 1: re_restore(g_sel); break;
                case 5: re_empty();        break;
                case 9: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void recycle_open(void) {
    if (g_win) { re_refresh(); return; }                  /* single instance */
    CreateAppWindow(90, 60, 500, 340, "Recycle Bin", Recycle_WndProc, 0);
}
