/* Debian95 — Windows-Explorer-style file manager with a real, explorable tree:
 * toolbar, address bar, a Folders pane, four view modes (Large/Small/List/
 * Details) and a status bar. Double-click a folder to enter it, a file to open
 * it; the Up button goes to the parent. Copy/Cut/Paste and Rename act on disk. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/nt.h>
#include <debian95/ps.h>
#include <debian95/img.h>
#include <debian95/icons.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include "shellutil.h"

#define TBH     28
#define ADRH    24
#define TREEW  150
#define STATH   18
#define CELLW   80
#define CELLH   56
#define FACE    RGB(0xC0,0xC0,0xC0)
#define WHITE   RGB(0xFF,0xFF,0xFF)
#define DK      RGB(0x00,0x00,0x00)
#define SEL     RGB(0x00,0x00,0x80)
#define SHc     RGB(0x80,0x80,0x80)
#define ICON_KEY RGB(0xFF,0x00,0xFF)

static HWND g_win;            /* the main Explorer window (Win32 veneer) */
static FsEntry g_ents[64];
static int  g_count;
static char g_path[128] = "";          /* current dir relative to the root */
static int  g_sel = -1;                 /* the focused item (Cut/Copy/Rename/Properties act on it) */
static unsigned char g_selset[64];      /* multi-selection (Ctrl/Shift-click), for Delete */
static int  g_anchor = -1;              /* Shift-click range anchor */
static int  sel_has(int i) { return i >= 0 && i < 64 && g_selset[i]; }
static void sel_clear(void) { for (int i = 0; i < 64; i++) g_selset[i] = 0; }
static int  sel_count(void) { int n = 0; for (int i = 0; i < 64; i++) if (g_selset[i]) n++; return n; }
static int  g_view = 0;                 /* 0 large, 1 details, 2 small, 3 list */
static int  g_marq_on;                   /* rubber-band selection active (Large Icons) */
static int  g_marq_x0, g_marq_y0, g_marq_x1, g_marq_y1;   /* client-relative marquee rect */
static int  g_renaming = -1;
static char g_rbuf[16]; static int g_rlen;
static char g_status[80] = "";
static char g_cliplist[32][160];       /* full paths of the cut/copied files (multi-select) */
static int  g_clipn;                   /* number of items on the clipboard */
static int  g_cut;                     /* 1 = the clipboard items were Cut (paste moves them) */
static int  g_mycomp;                  /* 1 = showing the My Computer root (drive + folders) */
static int  g_drag_idx = -1, g_drag_on, g_drag_x, g_drag_y, g_drag_sx, g_drag_sy;   /* file drag-and-drop */
static int  g_printers;                /* 1 = showing the Printers folder */
int sys_disk_free(const char *path, unsigned long long *total, unsigned long long *avail);  /* shims */
int  sys_get_setting(const char *key, char *out, int n);   /* persisted settings (shims) */
void sys_set_setting(const char *key, const char *val);
static void rename_box(int x, int y, int w);
/* the virtual My Computer entries (index 0 must be the C: drive) */
static const char *MYCOMP[] = { "(C:)", "Filesystem", "Control Panel", "Printers", "Dial-Up Networking" };
#define NMYCOMP 5

/* real removable / mounted drives that appear after the fixed My Computer entries */
struct lg_disk { char mount[128]; char label[32]; char size[16]; int removable; };
int sys_disk_list(struct lg_disk *out, int max);
int sys_disk_eject(const char *mount);
void sys_disk_automount(void);   /* mount freshly-inserted USB media */
int sys_open_file(const char *path);   /* file association: open in a downloaded app (VLC/Chromium/Viewer) */
static struct lg_disk g_disk[8]; static int g_ndisk;

/* installed printers: a real persisted list (printer0..7 in SETTINGS.INI) */
static char g_prn[8][32]; static int g_nprn, g_prn_loaded;
static void prn_load(void) {
    g_nprn = 0; char key[10] = "printer0", v[32];
    for (int i = 0; i < 8; i++) { key[7] = (char)('0' + i);
        if (sys_get_setting(key, v, sizeof v) == 0 && v[0]) { int k = 0; for (; v[k] && k < 31; k++) g_prn[g_nprn][k] = v[k]; g_prn[g_nprn][k] = 0; g_nprn++; } }
    g_prn_loaded = 1;
}
static void prn_save(void) {
    char key[10] = "printer0";
    for (int i = 0; i < 8; i++) { key[7] = (char)('0' + i); sys_set_setting(key, i < g_nprn ? g_prn[i] : ""); }
}
static void prn_add(void) {
    static const char *models[] = { "Generic / Text Only", "HP LaserJet 4", "Epson Stylus", "PostScript Printer" };
    if (g_nprn >= 8) return;
    const char *m = models[g_nprn & 3]; int k = 0; for (; m[k] && k < 31; k++) g_prn[g_nprn][k] = m[k]; g_prn[g_nprn][k] = 0;
    g_nprn++; prn_save();
}
static void prn_delete(int i) {   /* i is the printer index (0-based within g_prn) */
    if (i < 0 || i >= g_nprn) return;
    for (int j = i; j < g_nprn - 1; j++) for (int k = 0; k < 32; k++) g_prn[j][k] = g_prn[j + 1][k];
    g_nprn--; prn_save();
}

/* 16px (tree/details/small) + 32px (large) icons per kind */
static u32 i16_folder[256], i16_drive[256], i16_mycomp[256], i16_desktop[256];
static u32 i16_doc[256], i16_app[256], i16_img[256];
static u32 i32_doc[1024], i32_app[1024], i32_img[1024], i32_folder[1024], i32_drive[1024];
static int g_icons_ok;

static void append_(char *dst, const char *src) {
    int d = 0; while (dst[d]) d++;
    for (int s = 0; src[s]; s++) dst[d++] = src[s];
    dst[d] = 0;
}
static void utoa_(u32 v, char *b) {
    char t[12]; int i = 0;
    if (!v) { b[0] = '0'; b[1] = 0; return; }
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0; while (i) b[j++] = t[--i]; b[j] = 0;
}
static int name_eq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
/* Win95-style size: KB (rounded up, min 1) for the common case, scaling to
 * MB / GB / TB with one decimal for larger files. */
static void fmt_size(u32 bytes, char *out) {
    u32 kb = (bytes + 1023) / 1024; if (!kb) kb = 1;
    char n[16]; out[0] = 0;
    if (kb < 1024) { utoa_(kb, n); append_(out, n); append_(out, " KB"); return; }
    u32 num, den; const char *suf;
    if      (kb < 1024u * 1024)        { num = kb;          den = 1024; suf = " MB"; }
    else if (kb < 1024u * 1024 * 1024) { num = kb / 1024;   den = 1024; suf = " GB"; }
    else                               { num = kb / 1024 / 1024; den = 1024; suf = " TB"; }
    u32 whole = num / den, tenth = (num % den) * 10 / den;
    utoa_(whole, n); append_(out, n); append_(out, ".");
    n[0] = (char)('0' + tenth); n[1] = 0; append_(out, n); append_(out, suf);
}

/* ---- path helpers ---- */
static void path_push(const char *name) {
    int l = 0; while (g_path[l]) l++;
    if (l && g_path[l - 1] != '/') g_path[l++] = '/';
    for (int i = 0; name[i] && l < 126; i++) g_path[l++] = name[i];
    g_path[l] = 0;
}
static void path_up(void) {
    int l = 0; while (g_path[l]) l++;
    while (l > 0 && g_path[l - 1] != '/') l--;
    if (l > 0) l--;
    g_path[l] = 0;
}
static const char *path_leaf(void) {
    int l = 0; while (g_path[l]) l++;
    if (!l) return "Debian95 (C:)";
    int s = l; while (s > 0 && g_path[s - 1] != '/') s--;
    return g_path + s;
}
/* full path of entry idx into out */
static void full_of(int idx, char *out) {
    out[0] = 0;
    if (g_path[0]) { append_(out, g_path); append_(out, "/"); }
    append_(out, g_ents[idx].name);
}
/* current-dir path for a bare name */
static void cur_of(const char *name, char *out) {
    out[0] = 0;
    if (g_path[0]) { append_(out, g_path); append_(out, "/"); }
    append_(out, name);
}

static void fm_load_icons(void) {
    if (g_icons_ok) return;
    int any = 0;
    any |= icon_get(IC_FOLDER, 16, ICON_KEY, i16_folder) == 0;
    icon_get(IC_DRIVE, 16, ICON_KEY, i16_drive);
    icon_get(IC_MYCOMPUTER, 16, ICON_KEY, i16_mycomp);
    if (icon_get(IC_DESKTOP, 16, ICON_KEY, i16_desktop) != 0) for (int j = 0; j < 256; j++) i16_desktop[j] = i16_mycomp[j];
    icon_get(IC_DOC, 16, ICON_KEY, i16_doc);
    icon_get(IC_APP, 16, ICON_KEY, i16_app);
    icon_get_from("MSPAINT.EXE", 4, 16, ICON_KEY, i16_img);   /* real bitmap-image icon */
    icon_get(IC_DOC, 32, ICON_KEY, i32_doc);
    icon_get(IC_APP, 32, ICON_KEY, i32_app);
    icon_get_from("MSPAINT.EXE", 3, 32, ICON_KEY, i32_img);   /* real bitmap-image icon */
    icon_get(IC_FOLDER, 32, ICON_KEY, i32_folder);
    icon_get(IC_DRIVE, 32, ICON_KEY, i32_drive);
    if (any) g_icons_ok = 1;
}
static void blit_key(int x, int y, int n, const u32 *px) {
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) { u32 c = px[j * n + i]; if (c != ICON_KEY) fb_pixel(x + i, y + j, c); }
}
static void blit_key_sel(int x, int y, int n, const u32 *px) {
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) { u32 c = px[j * n + i];
            if (c != ICON_KEY) fb_pixel(x + i, y + j, (((c >> 1) & 0x7F7F7F) + ((SEL >> 1) & 0x7F7F7F))); }
}

static int ext_is(const char *n, const char *e3) {
    int L = (int)strlen(n);
    if (L < 4 || n[L-4] != '.') return 0;
    char a = n[L-3], b = n[L-2], c = n[L-1];
    if (a >= 'a' && a <= 'z') a -= 32; if (b >= 'a' && b <= 'z') b -= 32; if (c >= 'a' && c <= 'z') c -= 32;
    return a == e3[0] && b == e3[1] && c == e3[2];
}
static int file_kind(const char *n) {
    if (ext_is(n, "ELF")) return 1;
    if (ext_is(n, "EXE") || ext_is(n, "COM") || ext_is(n, "CPL") || ext_is(n, "DLL")) return 2;
    if (ext_is(n, "LGB") || ext_is(n, "BMP") || ext_is(n, "PNG") || ext_is(n, "JPG") ||
        ext_is(n, "ICO") || ext_is(n, "QOI") || ext_is(n, "GIF")) return 3;
    if (ext_is(n, "MPG") || ext_is(n, "MPE") || ext_is(n, "AVI")) return 5;
    if (ext_is(n, "WAV")) return 6;                                          /* Sound Recorder */
    if (ext_is(n, "RTF") || ext_is(n, "DOC") || ext_is(n, "WRI")) return 7;  /* WordPad */
    return 0;
}
static const u32 *ent_icon32(int i) {
    if (g_printers) return i32_app;                         /* Add Printer + printers */
    if (g_mycomp) return (i == 0 || i >= NMYCOMP) ? i32_drive : i32_folder;  /* (C:) + real drives = drive */
    if (g_ents[i].is_dir) return i32_folder;
    int k = file_kind(g_ents[i].name);
    if (k == 1 || k == 2) return i32_app; if (k == 3) return i32_img; return i32_doc;
}
static const u32 *ent_icon16(int i) {
    if (g_printers) return i16_app;
    if (g_mycomp) return (i == 0 || i >= NMYCOMP) ? i16_drive : i16_folder;
    if (g_ents[i].is_dir) return i16_folder;
    int k = file_kind(g_ents[i].name);
    if (k == 1 || k == 2) return i16_app; if (k == 3) return i16_img; return i16_doc;
}
static const char *ent_type(int i) {
    if (g_printers) return i == 0 ? "Add Printer Wizard" : "Printer";
    if (g_mycomp) { if (i == 0) return "Local Disk";
        if (i >= NMYCOMP) return g_disk[i - NMYCOMP].removable ? "Removable Disk" : "Local Disk";
        return "System Folder"; }
    if (g_ents[i].is_dir) return "File Folder";
    int k = file_kind(g_ents[i].name);
    return k == 1 ? "Debian95 Application" : k == 2 ? "Application"
         : k == 3 ? "Image" : k == 5 ? "Video Clip" : "Document";
}

static const char *ent_type(int i);   /* fwd */
static int g_sortcol, g_sortdesc;     /* Details header sort: 0 Name 1 Size 2 Type 3 Date */
static int nm_cmp(const char *a, const char *b) {   /* case-insensitive */
    for (int i = 0;; i++) { char x=a[i],y=b[i]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y) return x<y?-1:1; if(!x) return 0; }
}
/* order the entries: folders first, then by the active Details column (asc/desc) */
static void fm_sort(void) {
    for (int i = 1; i < g_count; i++)                 /* insertion sort (n<=64) */
        for (int j = i; j > 0; j--) {
            FsEntry *a = &g_ents[j], *b = &g_ents[j-1]; int sw = 0;
            if (a->is_dir != b->is_dir) sw = a->is_dir && !b->is_dir;   /* dirs first always */
            else { int c = 0;
                if (g_sortcol == 1) c = a->size < b->size ? -1 : a->size > b->size ? 1 : nm_cmp(a->name, b->name);
                else if (g_sortcol == 3) c = a->mtime < b->mtime ? -1 : a->mtime > b->mtime ? 1 : nm_cmp(a->name, b->name);
                else if (g_sortcol == 2) { c = nm_cmp(ent_type(j), ent_type(j-1)); if (!c) c = nm_cmp(a->name, b->name); }
                else c = nm_cmp(a->name, b->name);
                if (g_sortdesc) c = -c;
                sw = c < 0;
            }
            if (sw) { FsEntry t = *a; *a = *b; *b = t; } else break;
        }
}
static void sort_dirs_first(void) { fm_sort(); }
static char g_title[80];
static int g_vtop, g_hoff;              /* details-view scroll (rows, pixels) */
/* ---------- Back/Forward navigation history ---------- */
static void fm_refresh(void);
typedef struct { char path[128]; unsigned char mycomp, printers; } NavLoc;
static NavLoc g_hist[32]; static int g_histn, g_histpos = -1, g_replaying;
static int navloc_eq(const NavLoc *a) {
    if (a->mycomp != (unsigned char)g_mycomp || a->printers != (unsigned char)g_printers) return 0;
    for (int i = 0; i < 128; i++) { if (a->path[i] != g_path[i]) return 0; if (!a->path[i]) break; }
    return 1;
}
static void nav_record(void) {                        /* called from fm_refresh; pushes real navigations */
    if (g_replaying) return;
    if (g_histpos >= 0 && navloc_eq(&g_hist[g_histpos])) return;   /* unchanged -> a plain refresh */
    NavLoc c; int i = 0; for (; g_path[i] && i < 127; i++) c.path[i] = g_path[i]; c.path[i] = 0;
    c.mycomp = (unsigned char)g_mycomp; c.printers = (unsigned char)g_printers;
    if (g_histpos + 1 >= 32) { for (int k = 1; k < 32; k++) g_hist[k-1] = g_hist[k]; g_histpos = 30; }
    g_histpos++; g_hist[g_histpos] = c; g_histn = g_histpos + 1;
}
static void nav_apply(const NavLoc *l) {
    g_replaying = 1;
    int i = 0; for (; l->path[i] && i < 127; i++) g_path[i] = l->path[i]; g_path[i] = 0;
    g_mycomp = l->mycomp; g_printers = l->printers;
    fm_refresh();
    g_replaying = 0;
}
static void fm_back(void)    { if (g_histpos > 0) { g_histpos--; nav_apply(&g_hist[g_histpos]); } }
static void fm_forward(void) { if (g_histpos < g_histn - 1) { g_histpos++; nav_apply(&g_hist[g_histpos]); } }

static void fm_refresh(void) {
    nav_record();                                    /* record this location for Back/Forward */
    sel_clear(); g_anchor = -1;                      /* selection resets when the listing changes */
    if (g_printers) {                                /* virtual Printers folder */
        if (!g_prn_loaded) prn_load();
        g_count = 1 + g_nprn;
        { const char *ap = "Add Printer"; int k = 0; for (; ap[k] && k < 15; k++) g_ents[0].name[k] = ap[k]; g_ents[0].name[k] = 0; g_ents[0].is_dir = 0; g_ents[0].size = 0; }
        for (int i = 0; i < g_nprn; i++) { int k = 0; for (; g_prn[i][k] && k < 15; k++) g_ents[i + 1].name[k] = g_prn[i][k]; g_ents[i + 1].name[k] = 0; g_ents[i + 1].is_dir = 0; g_ents[i + 1].size = 0; }
        g_sel = -1; g_vtop = 0; g_hoff = 0;
        g_title[0] = 0; append_(g_title, "Printers");
        if (g_win) SetWindowText(g_win, g_title);
        return;
    }
    if (g_mycomp) {                                  /* virtual My Computer root */
        for (int i = 0; i < NMYCOMP; i++) { int k = 0; for (; MYCOMP[i][k] && k < 15; k++) g_ents[i].name[k] = MYCOMP[i][k];
            g_ents[i].name[k] = 0; g_ents[i].is_dir = 1; g_ents[i].size = 0; g_ents[i].mtime = 0; }
        sys_disk_automount();                        /* mount any freshly-inserted USB first */
        g_ndisk = sys_disk_list(g_disk, 8);          /* real mounted/removable drives */
        for (int i = 0; i < g_ndisk && NMYCOMP + i < 64; i++) {   /* named (E:), (F:), ... */
            char nm[16]; nm[0] = '('; nm[1] = (char)('E' + i); nm[2] = ':'; nm[3] = ')'; nm[4] = 0;
            int e = NMYCOMP + i, k = 0; for (; nm[k] && k < 15; k++) g_ents[e].name[k] = nm[k];
            g_ents[e].name[k] = 0; g_ents[e].is_dir = 1; g_ents[e].size = 0; g_ents[e].mtime = 0;
        }
        g_count = NMYCOMP + g_ndisk;
        g_sel = -1; g_vtop = 0; g_hoff = 0;
        g_title[0] = 0; append_(g_title, "My Computer");
        if (g_win) SetWindowText(g_win, g_title);
        return;
    }
    g_count = FsListDir(g_path, g_ents, 64); sort_dirs_first(); g_sel = -1;
    g_vtop = 0; g_hoff = 0;
    g_title[0] = 0; append_(g_title, "Exploring - "); append_(g_title, path_leaf());
    if (g_win) SetWindowText(g_win, g_title);
}

/* ---------- toolbar ---------- */
static void tb_button(int x, int y, int w) { fb_fill_rect(x, y, w, 22, FACE); wm_bevel_raised(x, y, w, 22); }
static void gl_back(int x, int y)    { for (int i = 0; i < 5; i++) { fb_fill_rect(x+8-i, y+3+i, 2, 2, DK); fb_fill_rect(x+8-i, y+11-i, 2, 2, DK); } fb_fill_rect(x+4, y+7, 6, 2, DK); }
static void gl_forward(int x, int y) { for (int i = 0; i < 5; i++) { fb_fill_rect(x+4+i, y+3+i, 2, 2, DK); fb_fill_rect(x+4+i, y+11-i, 2, 2, DK); } fb_fill_rect(x+4, y+7, 6, 2, DK); }
static void gl_up(int x, int y) { if (g_icons_ok) blit_key(x, y, 16, i16_folder);
    int ax = x + 9, ay = y + 3; for (int i = 0; i < 4; i++) fb_fill_rect(ax - i, ay + i, 2 * i + 1, 1, DK); fb_fill_rect(ax, ay + 4, 1, 3, DK); }
static void gl_cut(int x, int y) { fb_rect(x+1,y+9,4,4,DK); fb_rect(x+8,y+9,4,4,DK);
    for (int i=0;i<7;i++){ fb_pixel(x+3+i,y+2+i,RGB(0x50,0x50,0x50)); fb_pixel(x+11-i,y+2+i,RGB(0x50,0x50,0x50)); } }
static void gl_copy(int x, int y) { fb_fill_rect(x+5,y+1,8,10,WHITE); fb_rect(x+5,y+1,8,10,DK);
    fb_fill_rect(x+2,y+4,8,10,WHITE); fb_rect(x+2,y+4,8,10,DK); for (int r=0;r<3;r++) fb_fill_rect(x+4,y+6+r*2,4,1,SHc); }
static void gl_paste(int x, int y) { fb_fill_rect(x+1,y+2,13,12,RGB(0xB0,0x84,0x40)); wm_bevel_raised(x+1,y+2,13,12);
    fb_fill_rect(x+5,y,5,3,DK); fb_fill_rect(x+3,y+4,9,8,WHITE); for (int r=0;r<3;r++) fb_fill_rect(x+4,y+5+r*2,7,1,SHc); }
static void gl_del(int x, int y) { u32 R = RGB(0xD0,0x00,0x00);
    for (int i=0;i<8;i++){ fb_pixel(x+4+i,y+3+i,R); fb_pixel(x+5+i,y+3+i,R); fb_pixel(x+11-i,y+3+i,R); fb_pixel(x+12-i,y+3+i,R); } }
static void gl_props(int x, int y) { fb_fill_rect(x+1,y+1,9,13,WHITE); fb_rect(x+1,y+1,9,13,DK);
    for (int r=0;r<4;r++) fb_fill_rect(x+3,y+3+r*2,5,1,SHc); fb_fill_rect(x+8,y+8,6,5,RGB(0xF0,0xD8,0xA8)); fb_rect(x+8,y+8,6,5,DK); }
#define NTB 8
static int g_tbx[NTB];
static int g_vbx[4];                          /* right-aligned view buttons */
static const int VBTN_GV[4] = { 0, 2, 3, 1 }; /* Large, Small, List, Details -> g_view */
/* real COMCTL32 toolbar images per button: Back/Forward=history(130,0/1),
 * Up=view Parent(124,8), then the standard strip Cut/Copy/Paste/Delete/Properties. */
static const struct { u16 res; int idx; } TB_IMG[NTB] = {
    { 130, 0 }, { 130, 1 }, { 124, TBV_PARENTFOLDER },
    { 120, TBI_CUT }, { 120, TBI_COPY }, { 120, TBI_PASTE }, { 120, TBI_DELETE }, { 120, TBI_PROPERTIES }
};
static void draw_toolbar(int cx, int cy, int cw) {
    fb_fill_rect(cx, cy, cw, TBH, FACE);
    int x = cx + 4, y = cy + 3;
    void (*gl[NTB])(int,int) = { gl_back, gl_forward, gl_up, gl_cut, gl_copy, gl_paste, gl_del, gl_props };
    for (int b = 0; b < NTB; b++) {
        if (b == 3 || b == 6) { fb_fill_rect(x, y, 1, 22, SHc); x += 5; }   /* group separators */
        tb_button(x, y, 24);
        u32 img[16*16];
        if (toolbar_img(TB_IMG[b].res, TB_IMG[b].idx, 16, FACE, img) == 0) fb_blit(x + 4, y + 3, 16, 16, img);
        else gl[b](x + 4, y + 3);                 /* fall back to the hand-drawn glyph */
        g_tbx[b] = x - cx; x += 26;               /* store client-relative for hit-testing */
    }
    /* right-aligned view buttons: Large / Small / List / Details (view strip) */
    int vx = cx + cw - 4 * 26 - 4;
    for (int b = 0; b < 4; b++) {
        int bx = vx + b * 26, pressed = (g_view == VBTN_GV[b]);
        fb_fill_rect(bx, y, 24, 22, FACE);
        if (pressed) wm_bevel_sunken(bx, y, 24, 22); else wm_bevel_raised(bx, y, 24, 22);
        u32 img[16*16]; int d = pressed ? 1 : 0;
        if (toolbar_img(124, b, 16, FACE, img) == 0) fb_blit(bx + 4 + d, y + 3 + d, 16, 16, img);
        g_vbx[b] = bx - cx;
    }
    fb_fill_rect(cx, cy + TBH - 1, cw, 1, SHc);
}
/* ---------- address bar ---------- */
static void draw_address(int cx, int cy, int cw) {
    int y = cy + TBH;
    fb_fill_rect(cx, y, cw, ADRH, FACE);
    fb_text_t(cx + 6, y + (ADRH - (int)font_height()) / 2, "Address", DK);
    int bx = cx + 56, bw = cw - 56 - 6, by = y + 2, bh = ADRH - 4;
    fb_fill_rect(bx, by, bw, bh, WHITE); wm_bevel_sunken(bx, by, bw, bh);
    if (g_icons_ok) blit_key(bx + 3, by + (bh - 16) / 2, 16, (g_mycomp || g_printers) ? i16_mycomp : i16_drive);
    char ad[160];
    if (g_printers) { ad[0] = 0; append_(ad, "Printers"); }
    else if (g_mycomp) { ad[0] = 0; append_(ad, "My Computer"); }
    else { int z = 0; const char *d = "Debian95 (C:)"; for (; d[z]; z++) ad[z] = d[z]; ad[z] = 0;
        for (int i = 0; g_path[i]; i++) { char c = g_path[i] == '/' ? '\\' : g_path[i]; char s[2] = { c, 0 };
            if (g_path[i] == '/') append_(ad, "\\"); else append_(ad, s); } }
    fb_text_t(bx + 22, by + (bh - (int)font_height()) / 2, ad, DK);
}
/* ---------- Folders tree (breadcrumb + current subfolders) ---------- */
static void expand_box(int x, int y, int minus) { fb_fill_rect(x, y, 9, 9, WHITE); fb_rect(x, y, 9, 9, SHc);
    fb_fill_rect(x + 2, y + 4, 5, 1, DK); if (!minus) fb_fill_rect(x + 4, y + 2, 1, 5, DK); }
static void dotted_v(int x, int y0, int y1) { for (int k = y0; k < y1; k += 2) fb_pixel(x, k, SHc); }
static void dotted_h(int y, int x0, int x1) { for (int k = x0; k < x1; k += 2) fb_pixel(k, y, SHc); }
static void tree_line(int x, int y, const u32 *ic, const char *label, int sel, int cw) {
    if (sel) fb_fill_rect(x, y, cw, 16, SEL);
    if (ic && g_icons_ok) blit_key(x, y, 16, ic);
    fb_text_t(x + 20, y + (16 - (int)font_height()) / 2, label, sel ? WHITE : DK);
}
/* a real, expandable Folders tree. Expansion state = a set of expanded folder
 * paths (relative to the volume root; "" = C:). Rows are recorded for
 * hit-testing, each with the [+]/[-] box rect and the node's path. */
#define MAXEXP 32
static char g_exp[MAXEXP][128]; static int g_nexp;
static int  path_eq(const char *a, const char *b) { int i = 0; for (;; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; } }
static int  is_expanded(const char *p) { for (int i = 0; i < g_nexp; i++) if (path_eq(g_exp[i], p)) return 1; return 0; }
static void toggle_expand(const char *p) {
    for (int i = 0; i < g_nexp; i++) if (path_eq(g_exp[i], p)) {
        for (int j = i; j < g_nexp - 1; j++) for (int k = 0; k < 128; k++) g_exp[j][k] = g_exp[j+1][k];
        g_nexp--; return; }
    if (g_nexp < MAXEXP) { int k = 0; for (; p[k] && k < 127; k++) g_exp[g_nexp][k] = p[k]; g_exp[g_nexp][k] = 0; g_nexp++; }
}
static int dir_has_subdirs(const char *rel) { FsEntry e[64]; int n = FsListDir(rel, e, 64); for (int i = 0; i < n; i++) if (e[i].is_dir) return 1; return 0; }

static struct { int y, boxx, boxy, hasbox; char path[128]; } g_tree[64]; static int g_ntree;
static int tree_row(int base, int y, int depth, const u32 *ic, const char *label,
                    const char *path, int hasbox, int expanded, int sel) {
    int ix = base + depth * 19;                          /* indent per level */
    if (hasbox) { dotted_h(y + 8, ix - 12, ix - 3); expand_box(ix - 11, y + 3, expanded); }
    else        { dotted_h(y + 8, ix - 12, ix - 2); }
    tree_line(ix, y, ic, label, sel, TREEW - (ix - base) - 6);
    if (g_ntree < 64) { g_tree[g_ntree].y = y; g_tree[g_ntree].boxx = ix - 11; g_tree[g_ntree].boxy = y + 3;
        g_tree[g_ntree].hasbox = hasbox; int k = 0; for (; path[k] && k < 127; k++) g_tree[g_ntree].path[k] = path[k]; g_tree[g_ntree].path[k] = 0; g_ntree++; }
    return y + 18;
}
/* draw the folders under `rel` at `depth`, recursing into expanded ones.
 * Returns the y past the last row; fills *lastcy with the last child's centre. */
static int draw_subtree(int base, int y, int depth, const char *rel, int ymax, int *lastcy) {
    FsEntry e[64]; int n = FsListDir(rel, e, 64);
    /* directories first, in listing order */
    for (int i = 0; i < n && y < ymax; i++) {
        if (!e[i].is_dir) continue;
        char childrel[128]; int a = 0;
        for (const char *p = rel; *p && a < 120; p++) childrel[a++] = *p;
        if (a && rel[0]) childrel[a++] = '/';
        for (const char *p = e[i].name; *p && a < 127; p++) childrel[a++] = *p; childrel[a] = 0;
        int haskids = dir_has_subdirs(childrel), exp = is_expanded(childrel), sel = path_eq(g_path, childrel);
        if (lastcy) *lastcy = y + 8;
        y = tree_row(base, y, depth, exp ? i16_folder : i16_folder, e[i].name, childrel, haskids, exp, sel);
        if (exp && haskids) { int lc = y; y = draw_subtree(base, y, depth + 1, childrel, ymax, &lc);
            dotted_v(base + (depth + 1) * 19 - 11 + 4, /*stem*/ (y - 18) - 18 + 9, lc + 1); }
    }
    return y;
}
static void draw_tree(int cx, int cy, int th) {
    int x = cx, y = cy + TBH + ADRH;
    fb_fill_rect(x, y, TREEW, th, WHITE); wm_bevel_sunken(x, y, TREEW, th);
    g_ntree = 0;
    int base = x + 18, ymax = y + th - 14;
    int yD = y + 5, yM = yD + 18, yC = yM + 18;
    /* Desktop (root) */
    tree_line(base, yD, i16_desktop, "Desktop", 0, TREEW - (base - x) - 6);
    g_tree[g_ntree].y = yD; g_tree[g_ntree].hasbox = 0; g_tree[g_ntree].boxx = -1; g_tree[g_ntree].path[0] = 0; g_ntree++;
    /* My Computer (expanded) */
    dotted_v(base + 8, yD + 16, yM + 9); dotted_h(yM + 8, base + 8, base + 8);
    expand_box(base + 7, yM + 3, 1);
    tree_line(base + 19, yM, i16_mycomp, "My Computer", g_mycomp, TREEW - (base + 19 - x) - 6);
    g_tree[g_ntree].y = yM; g_tree[g_ntree].hasbox = 0; g_tree[g_ntree].boxx = -1; g_tree[g_ntree].path[0] = 1; g_tree[g_ntree].path[1] = 0; g_ntree++;  /* sentinel */
    /* Debian95 (C:) (expanded) */
    dotted_v(base + 27, yM + 16, yC + 9); dotted_h(yC + 8, base + 27, base + 27);
    expand_box(base + 26, yC + 3, 1);
    tree_line(base + 38, yC, i16_drive, "Debian95 (C:)", !g_mycomp && g_path[0] == 0, TREEW - (base + 38 - x) - 6);
    g_tree[g_ntree].y = yC; g_tree[g_ntree].hasbox = 0; g_tree[g_ntree].boxx = -1; g_tree[g_ntree].path[0] = 0; g_ntree++;
    /* the recursive folder tree lives two indents under C: */
    int lc = yC + 18;
    int yend = draw_subtree(base + 2, yC + 18, 1, "", ymax, &lc);
    if (yend > yC + 18) dotted_v(base + 2 + 1 * 19 - 11 + 4, yC + 16, lc + 1);
}
/* ---------- large-icon view ---------- */
static void fit_name(const char *src, char *dst, int maxw) {
    dst[0] = 0;
    if ((int)font_measure(src) <= maxw) { append_(dst, src); return; }
    for (int i = 1; i < 15 && src[i]; i++) {
        char t[20]; int k = 0; for (; k < i; k++) t[k] = src[k]; t[k] = 0; append_(t, "...");
        if ((int)font_measure(t) > maxw) { k = i - 1; for (int j = 0; j < k; j++) dst[j] = src[j]; dst[k] = 0; append_(dst, "..."); return; }
    }
    append_(dst, src);
}
static void icon_label(int cellx, int y, const char *s, int sel) {
    char nm[24]; fit_name(s, nm, CELLW - 6);
    int tw = (int)font_measure(nm), lx = cellx + (CELLW - tw) / 2;
    if (sel) { fb_fill_rect(lx - 2, y, tw + 4, (int)font_height() + 2, SEL); fb_text_t(lx, y + 1, nm, WHITE); }
    else fb_text_t(lx, y + 1, nm, DK);
}
static void draw_iconview(int cx, int cy, int cw, int ch) {
    int vx = cx + TREEW + 2, vy = cy + TBH + ADRH, vw = cw - TREEW - 2, vh = ch - TBH - ADRH - STATH;
    fb_fill_rect(vx, vy, vw, vh, WHITE); wm_bevel_sunken(vx, vy, vw, vh);
    int cols = (vw - 8) / CELLW; if (cols < 1) cols = 1;
    for (int i = 0; i < g_count; i++) {
        int col = i % cols, row = i / cols;
        int cellx = vx + 6 + col * CELLW, celly = vy + 6 + row * CELLH;
        if (celly + CELLH > vy + vh) break;
        const u32 *ic = ent_icon32(i); int ixp = cellx + (CELLW - 32) / 2;
        if (sel_has(i)) blit_key_sel(ixp, celly, 32, ic); else blit_key(ixp, celly, 32, ic);
        if (i == g_renaming) rename_box(cellx + 6, celly + 34, CELLW - 12);
        else icon_label(cellx, celly + 34, g_ents[i].name, sel_has(i));
    }
}
/* ---------- details view ---------- */
#define DROWH 17
#define SBW   16                       /* scrollbar thickness */
static const int DETCOL[4] = { 200, 76, 150, 130 };   /* Name / Size / Type / Modified */
static int det_content_w(void) { return DETCOL[0] + DETCOL[1] + DETCOL[2] + DETCOL[3]; }

static void sb_arrow(int x, int y, int w, int h, char dir) {
    fb_fill_rect(x, y, w, h, FACE); wm_bevel_raised(x, y, w, h);
    int cx = x + w / 2, cy = y + h / 2;
    for (int i = 0; i < 4; i++) {
        if (dir == '^') fb_fill_rect(cx - i, cy + i - 2, 2 * i + 1, 1, DK);
        else if (dir == 'v') fb_fill_rect(cx - i, cy + 2 - i, 2 * i + 1, 1, DK);
        else if (dir == '<') fb_fill_rect(cx + i - 2, cy - i, 1, 2 * i + 1, DK);
        else fb_fill_rect(cx + 2 - i, cy - i, 1, 2 * i + 1, DK);
    }
}
static void draw_vscroll(int x, int y, int h, int top, int vis, int total) {
    fb_fill_rect(x, y, SBW, h, RGB(0xE8, 0xE8, 0xE8));
    sb_arrow(x, y, SBW, SBW, '^'); sb_arrow(x, y + h - SBW, SBW, SBW, 'v');
    int track = h - 2 * SBW; if (track < 8) return;
    int thumb = total > 0 ? track * vis / total : track; if (thumb < 12) thumb = 12; if (thumb > track) thumb = track;
    int maxtop = total - vis; if (maxtop < 1) maxtop = 1;
    int ty = y + SBW + (track - thumb) * (top < maxtop ? top : maxtop) / maxtop;
    fb_fill_rect(x, ty, SBW, thumb, FACE); wm_bevel_raised(x, ty, SBW, thumb);
}
static void draw_hscroll(int x, int y, int w, int off, int content, int view) {
    fb_fill_rect(x, y, w, SBW, RGB(0xE8, 0xE8, 0xE8));
    sb_arrow(x, y, SBW, SBW, '<'); sb_arrow(x + w - SBW, y, SBW, SBW, '>');
    int track = w - 2 * SBW; if (track < 8) return;
    int thumb = content > 0 ? track * view / content : track; if (thumb < 12) thumb = 12; if (thumb > track) thumb = track;
    int maxoff = content - view; if (maxoff < 1) maxoff = 1;
    int tx = x + SBW + (track - thumb) * (off < maxoff ? off : maxoff) / maxoff;
    fb_fill_rect(tx, y, thumb, SBW, FACE); wm_bevel_raised(tx, y, thumb, SBW);
}
static void draw_details(int cx, int cy, int cw, int ch) {
    int vx = cx + TREEW + 2, vy = cy + TBH + ADRH, vw = cw - TREEW - 2, vh = ch - TBH - ADRH - STATH;
    fb_fill_rect(vx, vy, vw, vh, WHITE); wm_bevel_sunken(vx, vy, vw, vh);
    int fh = (int)font_height(), Wc = det_content_w();
    int innerw = vw - 4 - SBW;                         /* content area (minus vscroll) */
    int need_h = Wc > innerw;
    int innerh = vh - 4 - (need_h ? SBW : 0);
    int visible = (innerh - 19) / DROWH; if (visible < 1) visible = 1;
    int maxtop = g_count - visible; if (maxtop < 0) maxtop = 0;
    if (g_vtop > maxtop) g_vtop = maxtop; if (g_vtop < 0) g_vtop = 0;
    int maxoff = need_h ? Wc - innerw : 0; if (g_hoff > maxoff) g_hoff = maxoff; if (g_hoff < 0) g_hoff = 0;

    fb_set_clip(vx + 2, vy + 2, innerw, innerh);        /* clip header + rows to the pane */
    int hx = vx + 2 - g_hoff, hy = vy + 2, xx = hx;
    const char *H[4] = { "Name", "Size", "Type", "Modified" };
    for (int i = 0; i < 4; i++) { fb_fill_rect(xx, hy, DETCOL[i], 17, FACE); wm_bevel_raised(xx, hy, DETCOL[i], 17); fb_text_t(xx + 5, hy + 3, H[i], DK);
        if (i == g_sortcol) fb_text_t(xx + DETCOL[i] - 14, hy + 3, g_sortdesc ? "v" : "^", DK);   /* sort arrow */
        xx += DETCOL[i]; }
    int y = vy + 21;
    for (int r = 0; r < visible && g_vtop + r < g_count; r++) {
        int i = g_vtop + r;
        if (sel_has(i) && i != g_renaming) fb_fill_rect(hx, y, DETCOL[0], DROWH, SEL);
        if (g_icons_ok) blit_key(hx + 1, y, 16, ent_icon16(i));
        if (i == g_renaming) rename_box(hx + 18, y, DETCOL[0] - 22);
        else fb_text_t(hx + 20, y + (DROWH - fh) / 2, g_ents[i].name, sel_has(i) ? WHITE : DK);
        int c1 = DETCOL[0], c2 = DETCOL[1];
        if (!g_ents[i].is_dir) { char num[16]; fmt_size(g_ents[i].size, num);
            fb_text_t(hx + c1 + c2 - (int)font_measure(num) - 8, y + (DROWH - fh) / 2, num, DK); }
        fb_text_t(hx + c1 + c2 + 4, y + (DROWH - fh) / 2, ent_type(i), DK);
        { char d[24]; shell_date(g_ents[i].mtime, d); fb_text_t(hx + c1 + c2 + DETCOL[2] + 4, y + (DROWH - fh) / 2, d, DK); }
        y += DROWH;
    }
    fb_clear_clip();
    draw_vscroll(vx + vw - SBW - 1, vy + 1, vh - 2 - (need_h ? SBW : 0), g_vtop, visible, g_count);
    if (need_h) { draw_hscroll(vx + 1, vy + vh - SBW - 1, vw - 2 - SBW, g_hoff, Wc, innerw);
        fb_fill_rect(vx + vw - SBW - 1, vy + vh - SBW - 1, SBW, SBW, FACE); wm_bevel_raised(vx + vw - SBW - 1, vy + vh - SBW - 1, SBW, SBW); }
}
/* scrollbar interaction for the details view (client-relative rx/ry);
 * returns 1 if the click was consumed */
static int det_scroll_click(int cw, int ch, int rx, int ry) {
    int vx = TREEW + 2, vy = TBH + ADRH, vw = cw - TREEW - 2, vh = ch - TBH - ADRH - STATH;
    int ax = vx + vw - SBW - 1, need_h = det_content_w() > vw - 4 - SBW;
    if (rx >= ax && rx < ax + SBW) {                   /* vertical bar */
        if (ry < vy + SBW + 1) { if (g_vtop) g_vtop--; return 1; }
        if (ry > vy + vh - SBW - 1 - (need_h ? SBW : 0)) { g_vtop++; return 1; }
        g_vtop += (ry < vy + vh / 2) ? -3 : 3; return 1;
    }
    int ay = vy + vh - SBW - 1;
    if (need_h && ry >= ay && ry < ay + SBW) {          /* horizontal bar */
        if (rx < vx + SBW + 1) { g_hoff -= 24; return 1; }
        if (rx > vx + vw - SBW - 1 - SBW) { g_hoff += 24; return 1; }
        g_hoff += (rx < vx + vw / 2) ? -60 : 60; return 1;
    }
    return 0;
}
/* ---------- small icons / list ---------- */
#define SCELLW 118
#define SCELLH 18
static void draw_smalllist(int cx, int cy, int cw, int ch, int colmajor) {
    int vx = cx + TREEW + 2, vy = cy + TBH + ADRH, vw = cw - TREEW - 2, vh = ch - TBH - ADRH - STATH;
    fb_fill_rect(vx, vy, vw, vh, WHITE); wm_bevel_sunken(vx, vy, vw, vh);
    int fh = (int)font_height();
    int cols = (vw - 10) / SCELLW; if (cols < 1) cols = 1;
    int rows = (vh - 10) / SCELLH; if (rows < 1) rows = 1;
    for (int i = 0; i < g_count; i++) {
        int col = colmajor ? i / rows : i % cols, row = colmajor ? i % rows : i / cols;
        if (col >= cols) continue;
        int x = vx + 6 + col * SCELLW, y = vy + 6 + row * SCELLH;
        int nw = (int)font_measure(g_ents[i].name);
        if (sel_has(i) && i != g_renaming) fb_fill_rect(x + 18, y, nw + 4, SCELLH - 2, SEL);
        if (g_icons_ok) blit_key(x, y, 16, ent_icon16(i));
        if (i == g_renaming) rename_box(x + 18, y, SCELLW - 24);
        else fb_text_t(x + 20, y + (SCELLH - fh) / 2, g_ents[i].name, sel_has(i) ? WHITE : DK);
    }
}
/* ---------- status bar ---------- */
static void draw_status(int cx, int cy, int cw, int ch) {
    int y = cy + ch - STATH;
    fb_fill_rect(cx, y, cw, STATH, FACE);
    char sb[80];
    if (g_status[0]) { sb[0] = 0; append_(sb, g_status); }
    else { utoa_((u32)g_count, sb); append_(sb, g_count == 1 ? " object" : " object(s)"); }
    fb_fill_rect(cx + 2, y, 200, STATH - 2, FACE); wm_bevel_sunken(cx + 2, y, 200, STATH - 2);
    int ty = y + (STATH - (int)font_height()) / 2 - 1;
    fb_text_t(cx + 8, ty, sb, DK);
    /* second pane: the drive's REAL free space (statvfs), like Win95 Explorer */
    unsigned long long tot = 0, av = 0;
    if (sys_disk_free(0, &tot, &av) == 0) {
        char fb2[48]; int k = 0;
        unsigned m = (unsigned)(av / (1024ull * 1024ull));            /* MB free */
        unsigned t = (unsigned)(tot / (1024ull * 1024ull));           /* MB total */
        char n[16]; utoa_(m, n); for (char *s = n; *s; s++) fb2[k++] = *s;
        const char *suf = "MB free (of "; for (const char *s = suf; *s; s++) fb2[k++] = *s;
        utoa_(t, n); for (char *s = n; *s; s++) fb2[k++] = *s;
        const char *suf2 = "MB)"; for (const char *s = suf2; *s; s++) fb2[k++] = *s; fb2[k] = 0;
        int pw2 = cw - 210 - 4; if (pw2 > 40) {
            fb_fill_rect(cx + 206, y, pw2, STATH - 2, FACE); wm_bevel_sunken(cx + 206, y, pw2, STATH - 2);
            fb_text_t(cx + 212, ty, fb2, DK);
        }
    }
}
static void fm_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    draw_toolbar(cx, cy, cw);
    draw_address(cx, cy, cw);
    int th = ch - TBH - ADRH - STATH;
    draw_tree(cx, cy, th);
    if (g_view == 1) draw_details(cx, cy, cw, ch);
    else if (g_view == 2) draw_smalllist(cx, cy, cw, ch, 0);
    else if (g_view == 3) draw_smalllist(cx, cy, cw, ch, 1);
    else draw_iconview(cx, cy, cw, ch);
    draw_status(cx, cy, cw, ch);
    if (g_marq_on) {                                                  /* rubber-band outline (dotted) */
        int x0 = cx + (g_marq_x0 < g_marq_x1 ? g_marq_x0 : g_marq_x1);
        int x1 = cx + (g_marq_x0 < g_marq_x1 ? g_marq_x1 : g_marq_x0);
        int y0 = cy + (g_marq_y0 < g_marq_y1 ? g_marq_y0 : g_marq_y1);
        int y1 = cy + (g_marq_y0 < g_marq_y1 ? g_marq_y1 : g_marq_y0);
        for (int x = x0; x <= x1; x += 2) { fb_pixel((u32)x, (u32)y0, DK); fb_pixel((u32)x, (u32)y1, DK); }
        for (int y = y0; y <= y1; y += 2) { fb_pixel((u32)x0, (u32)y, DK); fb_pixel((u32)x1, (u32)y, DK); }
    }
    if (g_drag_on && g_drag_idx >= 0 && g_drag_idx < g_count) {       /* drag ghost following the cursor */
        const char *nm = g_ents[g_drag_idx].name; int tw = (int)font_measure(nm);
        int gx = cx + g_drag_x + 6, gy = cy + g_drag_y - 4;
        fb_fill_rect(gx - 1, gy - 1, tw + 4, (int)font_height() + 2, RGB(0xFF,0xFF,0xE0));
        fb_rect(gx - 2, gy - 2, tw + 6, (int)font_height() + 4, DK);
        fb_text_t(gx + 1, gy, nm, DK);
    }
}

/* ---- Open With: a chosen-program launcher + a per-extension association ---- */
extern void openwith_open(void);
static char g_ow_full[160];     /* the file the Open With dialog is acting on */
static char g_ow_name[32];      /* its leaf name (shown in the dialog) */

static void ow_ext3(const char *name, char *out) {   /* UPPER 3-char extension, "" if none */
    int L = (int)strlen(name), dot = -1; out[0] = 0;
    for (int i = L - 1; i >= 0; i--) { if (name[i] == '.') { dot = i; break; } if (name[i] == '/') break; }
    if (dot < 0) return;
    int k = 0; for (int i = dot + 1; name[i] && k < 3; i++) { char c = name[i]; if (c >= 'a' && c <= 'z') c -= 32; out[k++] = c; }
    out[k] = 0;
}
static void ow_run(int prog, const char *full) {
    switch (prog) {
        case 0: app_launch_doc(4,  full); break;   /* Notepad        */
        case 1: app_launch_doc(27, full); break;   /* WordPad        */
        case 2: app_launch_doc(5,  full); break;   /* Paint          */
        case 3: viewer_open(full);        break;   /* Image Viewer   */
        case 4: mediaplayer_open(full);   break;   /* Media Player   */
        case 5: app_launch_doc(26, full); break;   /* Sound Recorder */
        default: app_launch_doc(4, full); break;
    }
}
static void ow_key(const char *name, char *key) {    /* "ow_<EXT>" or "" */
    char ext[4]; ow_ext3(name, ext); key[0] = 0; if (!ext[0]) return;
    int k = 0; const char *p = "ow_"; for (; p[k]; k++) key[k] = p[k];
    for (int i = 0; ext[i]; i++) key[k++] = ext[i]; key[k] = 0;
}
const char *fm_openwith_file(void) { return g_ow_name; }
void fm_openwith_for(const char *full, const char *name) {   /* desktop: Open With on an arbitrary path */
    int k = 0; for (; full[k] && k < 159; k++) g_ow_full[k] = full[k]; g_ow_full[k] = 0;
    k = 0; for (; name[k] && k < 31; k++) g_ow_name[k] = name[k]; g_ow_name[k] = 0;
    openwith_open();
}
void fm_openwith_launch(int prog, int always) {
    if (always) { char key[12]; ow_key(g_ow_name, key);
        if (key[0]) { char v[4]; v[0] = (char)('0' + prog); v[1] = 0; sys_set_setting(key, v); } }
    ow_run(prog, g_ow_full);
}
static int ow_assoc(const char *name) {              /* chosen default program index, or -1 */
    char key[12]; ow_key(name, key); if (!key[0]) return -1;
    char v[4]; if (sys_get_setting(key, v, sizeof v) == 0 && v[0] >= '0' && v[0] <= '9') return v[0] - '0';
    return -1;
}

/* ---------- interaction ---------- */
static void fm_open(int idx) {
    if (idx < 0 || idx >= g_count) return;
    if (g_printers) {                                /* Printers folder */
        if (idx == 0) { prn_add(); fm_refresh();     /* Add Printer wizard (adds a printer) */
            wm_messagebox("Add Printer Wizard", "The printer has been installed and is\nready to use."); }
        else printman_open();                        /* open the printer's queue (Print Manager) */
        return;
    }
    if (g_mycomp) {                                  /* My Computer entries have custom actions */
        if (idx == 0) { g_mycomp = 0; g_path[0] = 0; fm_refresh(); }        /* enter (C:) - data dir */
        else if (idx == 1) { g_mycomp = 0; g_path[0] = '/'; g_path[1] = 0; fm_refresh(); }  /* the REAL filesystem */
        else if (idx == 2) control_open();                                   /* Control Panel */
        else if (idx == 3) { g_mycomp = 0; g_printers = 1; fm_refresh(); }   /* Printers folder */
        else if (idx == 4) { extern void vpn_open(void); vpn_open(); }       /* Dial-Up / VPN */
        else if (idx >= NMYCOMP && idx - NMYCOMP < g_ndisk) {                 /* a real mounted drive */
            const char *mp = g_disk[idx - NMYCOMP].mount; g_mycomp = 0;
            int k = 0; for (; mp[k] && k < 126; k++) g_path[k] = mp[k]; g_path[k] = 0; fm_refresh();
        }
        return;
    }
    if (g_ents[idx].is_dir) { path_push(g_ents[idx].name); fm_refresh(); return; }
    char full[152]; full_of(idx, full);
    { int a = ow_assoc(g_ents[idx].name); if (a >= 0) { ow_run(a, full); return; } }  /* user's Open With default */
    if (sys_open_file(full)) return;                 /* a downloaded app (VLC/Chromium/Viewer) claimed this type */
    switch (file_kind(g_ents[idx].name)) {
        case 1: ps_spawn(full); break;
        case 2: pe_run(full); break;
        case 3: viewer_open(full); break;
        case 5: mediaplayer_open(full); break;
        case 6: app_launch_doc(26, full); break;      /* Sound Recorder (module) */
        case 7: app_launch_doc(27, full); break;      /* WordPad (module) */
        default: app_launch_doc(4, full); break;      /* Notepad (module) */
    }
}
static void gen_copy_name(const char *src, char *dst) {
    { int coll = 0; for (int i = 0; i < g_count; i++) if (name_eq(src, g_ents[i].name)) { coll = 1; break; }
      if (!coll) { int k = 0; for (; src[k] && k < 15; k++) dst[k] = src[k]; dst[k] = 0; return; } }   /* keep the name if it doesn't collide */
    char base[16], ext[8]; int b = 0, e = 0, indot = 0;
    for (int i = 0; src[i]; i++) { if (src[i] == '.') { indot = 1; continue; }
        if (!indot && b < 8) base[b++] = src[i]; else if (indot && e < 3) ext[e++] = src[i]; }
    base[b] = 0; ext[e] = 0;
    for (int n = 1; n < 100; n++) {
        char num[4]; int nn = 0, v = n, t = 0; char tmp[4];
        while (v) { tmp[t++] = (char)('0' + v % 10); v /= 10; } while (t) num[nn++] = tmp[--t]; num[nn] = 0;
        int keep = 8 - nn; if (keep > b) keep = b; int p = 0;
        for (int i = 0; i < keep; i++) dst[p++] = base[i];
        for (int i = 0; num[i]; i++) dst[p++] = num[i];
        if (e) { dst[p++] = '.'; for (int i = 0; i < e; i++) dst[p++] = ext[i]; }
        dst[p] = 0;
        int coll = 0; for (int i = 0; i < g_count; i++) if (name_eq(dst, g_ents[i].name)) { coll = 1; break; }
        if (!coll) return;
    }
}
static const char *path_leaf_of(const char *p) { int l = 0; while (p[l]) l++; int s = l; while (s > 0 && p[s-1] != '/') s--; return p + s; }
/* collect the current selection (or the focused file) onto the clipboard */
static void fm_clip_collect(int cut) {
    g_clipn = 0; g_cut = cut;
    if (sel_count() > 0) { for (int i = 0; i < g_count && g_clipn < 32; i++) if (sel_has(i) && !g_ents[i].is_dir) full_of(i, g_cliplist[g_clipn++]); }
    else if (g_sel >= 0 && g_sel < g_count && !g_ents[g_sel].is_dir) full_of(g_sel, g_cliplist[g_clipn++]);
    g_status[0] = 0;
    if (g_clipn) { append_(g_status, cut ? "Cut " : "Copied ");
        char n[6]; int v = g_clipn, k = 0; char t[6]; int m = 0; if (!v) n[k++]='0'; while (v) { t[m++]=(char)('0'+v%10); v/=10; } while (m) n[k++]=t[--m]; n[k]=0;
        append_(g_status, n); append_(g_status, g_clipn == 1 ? " item" : " items"); }
}
static void fm_paste(void) {
    if (g_clipn == 0) { g_status[0] = 0; append_(g_status, "Clipboard is empty"); return; }
    int pasted = 0;
    for (int c = 0; c < g_clipn; c++) {
        const char *src = g_cliplist[c]; u8 *data; u32 sz;
        if (FsReadAlloc(src, &data, &sz) != 0) continue;
        char nn[16];
        if (g_cut) { const char *l = path_leaf_of(src); int k = 0; for (; l[k] && k < 15; k++) nn[k] = l[k]; nn[k] = 0; }
        else gen_copy_name(path_leaf_of(src), nn);
        char dest[160]; cur_of(nn, dest);
        if (g_cut) { int eq = 1; for (int i = 0;; i++) { if (dest[i] != src[i]) { eq = 0; break; } if (!dest[i]) break; }
            if (eq) { kfree(data); continue; } }               /* moving into the same folder: skip */
        int ok = FsWriteFile(dest, data, sz) == 0; kfree(data);
        if (ok) { pasted++; if (g_cut) FsDelete(src); }
    }
    if (g_cut) { g_clipn = 0; g_cut = 0; }
    fm_refresh();
    g_status[0] = 0; if (pasted) { append_(g_status, "Pasted "); char n[6]; int v = pasted, k = 0; char t[6]; int m = 0; while (v) { t[m++]=(char)('0'+v%10); v/=10; } while (m) n[k++]=t[--m]; n[k]=0; append_(g_status, n); }
    else append_(g_status, "Nothing pasted");
}
static void rename_begin(int idx) { g_renaming = idx; g_rlen = 0;
    for (int i = 0; g_ents[idx].name[i] && i < 14; i++) g_rbuf[g_rlen++] = g_ents[idx].name[i]; g_rbuf[g_rlen] = 0; }
static void rename_commit(void) {
    if (g_renaming < 0) return; int idx = g_renaming; g_renaming = -1;
    if (!g_rlen || name_eq(g_rbuf, g_ents[idx].name)) return;
    char oldp[152]; full_of(idx, oldp);
    char newp[152]; cur_of(g_rbuf, newp);
    u8 *data; u32 sz;
    if (FsReadAlloc(oldp, &data, &sz) != 0) return;
    if (FsWriteFile(newp, data, sz) == 0) FsDelete(oldp);
    kfree(data); g_sel = -1; fm_refresh();
}
static void fm_key(char c) {
    if (g_renaming < 0) {                             /* browsing: Enter opens, Backspace goes Up */
        if ((c == '\n' || c == '\r') && g_sel >= 0) fm_open(g_sel);
        else if (c == 8) { if (g_printers) { g_printers = 0; g_mycomp = 1; fm_refresh(); }
                           else if (g_mycomp) {} else if (g_path[0]) { path_up(); fm_refresh(); } else { g_mycomp = 1; fm_refresh(); } }
        return;
    }
    if (c == '\n' || c == '\r') { rename_commit(); return; }
    if (c == 27) { g_renaming = -1; return; }
    if ((c == 8 || c == 0x7F) && g_rlen) g_rbuf[--g_rlen] = 0;
    else if (c >= 32 && g_rlen < 14) { char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; g_rbuf[g_rlen++] = u; g_rbuf[g_rlen] = 0; }
}
static void rename_box(int x, int y, int w) {
    fb_fill_rect(x, y, w, (int)font_height() + 4, WHITE); fb_rect(x, y, w, (int)font_height() + 4, DK);
    fb_text_t(x + 2, y + 2, g_rbuf, DK);
    fb_fill_rect(x + 2 + (int)font_measure(g_rbuf), y + 2, 1, (int)font_height(), DK);
}

static u32 g_last_ms; static int g_last_idx = -1;
extern u32 wm_now_ms(void);
static void fm_ctx(int act);
/* collected delete targets (by full path, so indices can shift as we delete) */
static char g_del_paths[64][160]; static int g_del_isdir[64], g_del_n;
static void fm_do_delete_set(void) {
    for (int i = 0; i < g_del_n; i++) {
        if (g_del_isdir[i]) FsDelete(g_del_paths[i]); else recycle_add(g_del_paths[i]);
    }
    g_sel = -1; fm_refresh();
}
static void fm_delete(int idx) {                 /* deletes the whole selection (or idx) */
    g_del_n = 0;
    if (sel_count() > 0) { for (int i = 0; i < g_count && g_del_n < 64; i++) if (sel_has(i)) {
        full_of(i, g_del_paths[g_del_n]); g_del_isdir[g_del_n] = g_ents[i].is_dir; g_del_n++; } }
    else if (idx >= 0 && idx < g_count) { full_of(idx, g_del_paths[0]); g_del_isdir[0] = g_ents[idx].is_dir; g_del_n = 1; }
    if (g_del_n == 0) return;
    char v[8];                                    /* Recycle Bin Properties: confirm unless disabled */
    if (sys_get_setting("confirm_del", v, sizeof v) == 0 && v[0] == '0') { fm_do_delete_set(); return; }
    char msg[96];
    if (g_del_n == 1) { msg[0] = 0; append_(msg, "Are you sure you want to send '");
        int cut = 0; { int l = 0; while (g_del_paths[0][l]) l++; cut = l; while (cut > 0 && g_del_paths[0][cut-1] != '/') cut--; }
        append_(msg, g_del_paths[0] + cut); append_(msg, "'\nto the Recycle Bin?"); }
    else { msg[0] = 0; append_(msg, "Are you sure you want to send these\n");
        char n[6]; int v2 = g_del_n, k = 0; char t[6]; int m = 0; while (v2) { t[m++] = (char)('0'+v2%10); v2/=10; } while (m) n[k++] = t[--m]; n[k] = 0;
        append_(msg, n); append_(msg, " items to the Recycle Bin?"); }
    wm_confirm("Confirm Multiple File Delete", msg, fm_do_delete_set);
}
static void toolbar_act(int b) {
    switch (b) {
        case 0: fm_back(); break;                                                           /* Back */
        case 1: fm_forward(); break;                                                        /* Forward */
        case 2: if (g_mycomp) break;                                                        /* Up */
                else if (g_printers) { g_printers = 0; g_mycomp = 1; fm_refresh(); }         /* Printers -> My Computer */
                else if (g_path[0]) { path_up(); fm_refresh(); }
                else { g_mycomp = 1; fm_refresh(); } break;                                  /* C: root -> My Computer */
        case 3: case 4: fm_clip_collect(b == 3); break;   /* Cut / Copy the selection */
        case 5: fm_paste(); break;
        case 6: if (g_sel >= 0) fm_delete(g_sel); break;
        case 7: fm_ctx(4); break;
    }
}
static int view_hit(int rx, int ry, int cw, int ch) {
    int vx = TREEW + 2, vy = TBH + ADRH, vw = cw - TREEW - 2, vh = ch - TBH - ADRH - STATH;
    if (rx < vx + 2 || ry < vy + 2 || rx >= vx + vw || ry >= vy + vh) return -1;
    if (g_view == 1) { if (rx >= vx + vw - SBW - 1) return -1;      /* on the vertical bar */
        if (ry < vy + 21) return -1; int i = (ry - vy - 21) / DROWH + g_vtop; return (i >= 0 && i < g_count) ? i : -1; }
    if (g_view == 2 || g_view == 3) {
        int cols = (vw - 10) / SCELLW; if (cols < 1) cols = 1;
        int rows = (vh - 10) / SCELLH; if (rows < 1) rows = 1;
        int lx = rx - vx - 6, ly = ry - vy - 6; if (lx < 0 || ly < 0) return -1;
        int col = lx / SCELLW, row = ly / SCELLH; if (col >= cols) return -1;
        int i = (g_view == 3) ? col * rows + row : row * cols + col; return (i >= 0 && i < g_count) ? i : -1;
    }
    int cols = (vw - 8) / CELLW; if (cols < 1) cols = 1;
    if (rx < vx + 6 || ry < vy + 6) return -1;
    int col = (rx - vx - 6) / CELLW, row = (ry - vy - 6) / CELLH;
    if (col >= cols) return -1; int i = row * cols + col; return (i >= 0 && i < g_count) ? i : -1;
}
static void nav_to(const char *p) {
    if (p[0] == 1) { g_printers = 0; g_mycomp = 1; fm_refresh(); return; }   /* sentinel: My Computer root */
    g_mycomp = 0; g_printers = 0;
    int i = 0; for (; p[i] && i < 126; i++) g_path[i] = p[i]; g_path[i] = 0; fm_refresh();
}
/* rubber-band (Large Icons): select every item whose cell intersects the marquee */
static void marq_select(int cw, int ch) {
    if (g_view != 0) return;
    int vx = TREEW + 2, vy = TBH + ADRH, vw = cw - TREEW - 2;
    int cols = (vw - 8) / CELLW; if (cols < 1) cols = 1;
    int mx0 = g_marq_x0 < g_marq_x1 ? g_marq_x0 : g_marq_x1;
    int mx1 = g_marq_x0 < g_marq_x1 ? g_marq_x1 : g_marq_x0;
    int my0 = g_marq_y0 < g_marq_y1 ? g_marq_y0 : g_marq_y1;
    int my1 = g_marq_y0 < g_marq_y1 ? g_marq_y1 : g_marq_y0;
    sel_clear(); g_sel = -1;
    for (int i = 0; i < g_count && i < 64; i++) {
        int col = i % cols, row = i / cols;
        int cx0 = vx + 6 + col * CELLW, cy0 = vy + 6 + row * CELLH;
        if (mx0 < cx0 + CELLW && mx1 > cx0 && my0 < cy0 + CELLH && my1 > cy0) { g_selset[i] = 1; g_sel = i; }
    }
}
static void fm_click(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
    if (ry < TBH) {
        for (int b = NTB - 1; b >= 0; b--) if (rx >= g_tbx[b] && rx < g_tbx[b] + 24) { toolbar_act(b); return; }
        for (int b = 0; b < 4; b++) if (rx >= g_vbx[b] && rx < g_vbx[b] + 24) { g_view = VBTN_GV[b]; g_vtop = 0; g_hoff = 0; return; }
        return;
    }
    if (rx < TREEW && ry >= TBH + ADRH) {                                   /* Folders tree click */
        int ax = rx + cx, ay = ry + cy;                                     /* to client-absolute */
        for (int i = g_ntree - 1; i >= 0; i--)                              /* [+]/[-] box toggles expansion */
            if (g_tree[i].hasbox && ax >= g_tree[i].boxx - 1 && ax < g_tree[i].boxx + 11 &&
                ay >= g_tree[i].boxy - 1 && ay < g_tree[i].boxy + 11) { toggle_expand(g_tree[i].path); return; }
        for (int i = g_ntree - 1; i >= 0; i--)                              /* a label selects that folder */
            if (ay >= g_tree[i].y && ay < g_tree[i].y + 16) { nav_to(g_tree[i].path); return; }
        return;
    }
    if (g_view == 1 && ry >= TBH + ADRH + 2 && ry < TBH + ADRH + 19 && rx >= TREEW + 4) {   /* Details column header -> sort */
        int xx = TREEW + 4 - g_hoff, col = -1;
        for (int c = 0; c < 4; c++) { if (rx >= xx && rx < xx + DETCOL[c]) { col = c; break; } xx += DETCOL[c]; }
        if (col >= 0) { if (g_sortcol == col) g_sortdesc = !g_sortdesc; else { g_sortcol = col; g_sortdesc = 0; } fm_sort(); g_sel = -1; return; }
    }
    if (g_view == 1 && det_scroll_click(cw, ch, rx, ry)) return;    /* details scrollbars */
    int i = view_hit(rx, ry, cw, ch);
    if (i < 0) {                                          /* empty space: clear + arm a rubber-band */
        g_sel = -1; sel_clear(); g_status[0] = 0;
        if (g_view == 0 && rx >= TREEW && ry >= TBH + ADRH) {
            g_marq_on = 1; g_marq_x0 = g_marq_x1 = rx; g_marq_y0 = g_marq_y1 = ry;
        }
        return;
    }
    u32 now = wm_now_ms();
    if (i == g_sel && i == g_last_idx && now - g_last_ms < 400) { fm_open(i); g_last_idx = -1; return; }
    if (wm_mod_ctrl()) { if (i < 64) g_selset[i] ^= 1; g_sel = i; g_anchor = i; }          /* toggle */
    else if (wm_mod_shift() && g_anchor >= 0) { sel_clear();                                /* range */
        int a = g_anchor, b = i; if (a > b) { int t = a; a = b; b = t; }
        for (int k = a; k <= b && k < 64; k++) g_selset[k] = 1; g_sel = i; }
    else { sel_clear(); if (i < 64) g_selset[i] = 1; g_sel = i; g_anchor = i; }             /* single */
    g_last_idx = i; g_last_ms = now;
    g_status[0] = 0; append_(g_status, g_ents[i].name);
    g_drag_idx = i; g_drag_sx = rx; g_drag_sy = ry; g_drag_on = 0;    /* arm a possible drag */
}
/* move file g_ents[idx] into the folder at relative path destdir (drag-and-drop) */
static void fm_move_into(int idx, const char *destdir) {
    if (idx < 0 || idx >= g_count || g_ents[idx].is_dir) return;      /* files only */
    char nm[16]; { int k = 0; for (; g_ents[idx].name[k] && k < 15; k++) nm[k] = g_ents[idx].name[k]; nm[k] = 0; }
    char src[176]; full_of(idx, src);
    char dst[200]; int k = 0; for (int i = 0; destdir[i] && k < 160; i++) dst[k++] = destdir[i];
    if (k) dst[k++] = '/'; for (int i = 0; nm[i]; i++) dst[k++] = nm[i]; dst[k] = 0;
    { int eq = 1; for (int i = 0;; i++) { if (src[i] != dst[i]) { eq = 0; break; } if (!src[i]) break; } if (eq) return; }  /* same place */
    u8 *d; u32 n; if (FsReadAlloc(src, &d, &n) != 0) return;
    int ok = FsWriteFile(dst, d, n) == 0; kfree(d);
    if (ok) { FsDelete(src); fm_refresh(); g_status[0] = 0; append_(g_status, "Moved "); append_(g_status, nm); }
    else { g_status[0] = 0; append_(g_status, "Move failed"); }
}
static void fm_motion(int rx, int ry, u8 buttons) {
    if (g_marq_on) {                                                /* rubber-band selection */
        if (buttons & 1) {
            int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch); (void)cx; (void)cy;
            g_marq_x1 = rx; g_marq_y1 = ry; marq_select(cw, ch);
        } else g_marq_on = 0;                                        /* released: keep the selection */
        return;
    }
    if (buttons & 1) {                                               /* dragging */
        if (g_drag_idx >= 0) {
            if (!g_drag_on) { int dx = rx - g_drag_sx, dy = ry - g_drag_sy; if (dx*dx + dy*dy > 25) g_drag_on = 1; }
            g_drag_x = rx; g_drag_y = ry;
        }
        return;
    }
    if (g_drag_on && g_drag_idx >= 0 && !g_mycomp && !g_printers) {  /* dropped: find a folder target */
        int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
        char destdir[176]; int have = 0;
        if (rx < TREEW && ry >= TBH + ADRH) {                        /* onto a Folders-tree node */
            int ay = ry + cy;
            for (int i = g_ntree - 1; i >= 0; i--)
                if (ay >= g_tree[i].y && ay < g_tree[i].y + 16 && g_tree[i].path[0] != 1) {
                    int k = 0; for (; g_tree[i].path[k] && k < 170; k++) destdir[k] = g_tree[i].path[k]; destdir[k] = 0; have = 1; break; }
        } else {                                                     /* onto a folder icon in the view */
            int t = view_hit(rx, ry, cw, ch);
            if (t >= 0 && g_ents[t].is_dir && t != g_drag_idx) {
                int k = 0; for (; g_path[k] && k < 120; k++) destdir[k] = g_path[k]; if (k) destdir[k++] = '/';
                for (int i = 0; g_ents[t].name[i]; i++) destdir[k++] = g_ents[t].name[i]; destdir[k] = 0; have = 1;
            }
        }
        if (have) fm_move_into(g_drag_idx, destdir);
    }
    g_drag_idx = -1; g_drag_on = 0;
}
/* ---------- Properties tabbed sheet ---------- */
static HWND g_props;          /* file Properties sheet (Win32 veneer) */
static char g_pr_name[16], g_pr_type[24], g_pr_loc[140], g_pr_title[32];
static u32  g_pr_size; static int g_pr_isdir, g_pr_kind;
static void props_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    static const char *tabs[] = { "General" };
    cc_tabs(cx + 6, cy + 6, 74, tabs, 1, 0);
    int th = (int)font_height() + 8;
    int px = cx + 8, py = cy + 6 + th, pw = cw - 16, ph = ch - (6 + th) - 44;
    fb_fill_rect(px, py, pw, ph, FACE); wm_bevel_raised(px, py, pw, ph);
    const u32 *ic = g_pr_isdir ? i32_folder : (g_pr_kind == 2 ? i32_app : g_pr_kind == 3 ? i32_img : i32_doc);
    if (g_icons_ok) blit_key(px + 14, py + 12, 32, ic);
    fb_text_t(px + 58, py + 22, g_pr_name, DK);
    fb_fill_rect(px + 12, py + 52, pw - 24, 1, SHc); fb_fill_rect(px + 12, py + 53, pw - 24, 1, WHITE);
    fb_text_t(px + 14, py + 62, "Type:", DK);     fb_text_t(px + 90, py + 62, g_pr_type, DK);
    fb_text_t(px + 14, py + 82, "Location:", DK); fb_text_t(px + 90, py + 82, g_pr_loc, DK);
    if (!g_pr_isdir) { char sz[40], nb[16]; fmt_size(g_pr_size, sz); append_(sz, " (");
        utoa_(g_pr_size, nb); append_(sz, nb); append_(sz, " bytes)");
        fb_text_t(px + 14, py + 102, "Size:", DK); fb_text_t(px + 90, py + 102, sz, DK); }
    int by = cy + ch - 32, bw = 70;
    cc_defbutton(cx + cw - bw * 3 - 22, by, bw, 24, "OK", 0);
    cc_button(cx + cw - bw * 2 - 15, by, bw, 24, "Cancel", 0);
    cc_button(cx + cw - bw - 8, by, bw, 24, "Apply", 0);
}
static void props_click(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_props, &cx, &cy, &cw, &ch); (void)cx; (void)cy;
    if (ry >= ch - 32 && ry < ch - 8 && rx >= cw - 70 * 3 - 22) DestroyWindow(g_props);
}
/* ---- Win32 veneer: file Properties WndProc ---- */
static LRESULT CALLBACK Props_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: g_props = hwnd; return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            props_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: props_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_DESTROY: g_props = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
static void fm_props(int idx) {
    g_pr_name[0] = 0; append_(g_pr_name, g_ents[idx].name);
    g_pr_type[0] = 0; append_(g_pr_type, ent_type(idx));
    g_pr_size = g_ents[idx].size; g_pr_isdir = g_ents[idx].is_dir; g_pr_kind = file_kind(g_ents[idx].name);
    g_pr_loc[0] = 0; append_(g_pr_loc, "Debian95 (C:)");
    for (int i = 0; g_path[i]; i++) { if (g_path[i] == '/') append_(g_pr_loc, "\\"); else { char s[2] = { g_path[i], 0 }; append_(g_pr_loc, s); } }
    g_pr_title[0] = 0; append_(g_pr_title, g_pr_name); append_(g_pr_title, " Properties");
    if (g_props) SetWindowText(g_props, g_pr_title);   /* reuse the sheet for the new file */
    else CreateAppWindow(300, 170, 300, 250, g_pr_title, Props_WndProc, 0);
}
/* ---------- drive Properties (General tab with the Win95 capacity pie) ---------- */
static HWND g_dprops;         /* drive Properties sheet (Win32 veneer) */
/* integer Bresenham line (fb has no line primitive); fb_pixel clips out-of-range */
static void px_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 - x0, dy = y1 - y0, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (ax > ay) { int e = ax / 2; for (int i = 0; i <= ax; i++) { fb_pixel((u32)x0, (u32)y0, c); x0 += sx; e -= ay; if (e < 0) { y0 += sy; e += ax; } } }
    else         { int e = ay / 2; for (int i = 0; i <= ay; i++) { fb_pixel((u32)x0, (u32)y0, c); y0 += sy; e -= ax; if (e < 0) { x0 += sx; e += ay; } } }
}
/* a filled pie: `usedDeg` degrees (from 12 o'clock, clockwise) in cu, rest in cf.
 * Rotates a rim vector by 1 deg/step with fixed-point math - no libm, portable. */
static void draw_pie(int cx0, int cy0, int r, int usedDeg, u32 cu, u32 cf) {
    long x = 0, y = -(long)r * 1000;                 /* 12 o'clock, scaled x1000 */
    const long C = 999848, S = 17452;                /* cos/sin 1 deg, scaled x1e6 */
    for (int i = 0; i < 360; i++) {
        px_line(cx0, cy0, cx0 + (int)(x / 1000), cy0 + (int)(y / 1000), i < usedDeg ? cu : cf);
        long nx = (x * C - y * S) / 1000000, ny = (x * S + y * C) / 1000000; x = nx; y = ny;
    }
}
static unsigned long long g_dv_tot, g_dv_used, g_dv_free;
static void commafy(unsigned long long v, char *o) {   /* 12345678 -> "12,345,678" */
    char t[24]; int n = 0; if (v == 0) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int k = 0; for (int i = 0; i < n; i++) { o[k++] = t[n - 1 - i]; int rem = n - 1 - i; if (rem && rem % 3 == 0) o[k++] = ','; }
    o[k] = 0;
}
static void dprops_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    static const char *tabs[] = { "General", "Tools" };
    cc_tabs(cx + 6, cy + 6, 74, tabs, 2, 0);
    int th = (int)font_height() + 8;
    int px = cx + 8, py = cy + 6 + th, pw = cw - 16, ph = ch - (6 + th) - 44;
    fb_fill_rect(px, py, pw, ph, FACE); wm_bevel_raised(px, py, pw, ph);
    if (g_icons_ok) blit_key(px + 14, py + 12, 32, i32_drive);
    fb_text_t(px + 58, py + 14, "Debian95", DK);
    cc_edit(px + 58, py + 30, pw - 80, 18, "Debian95", 0);
    fb_fill_rect(px + 12, py + 56, pw - 24, 1, SHc); fb_fill_rect(px + 12, py + 57, pw - 24, 1, WHITE);
    fb_text_t(px + 14, py + 66, "Type:", DK);        fb_text_t(px + 110, py + 66, "Local Disk", DK);
    fb_text_t(px + 14, py + 84, "File system:", DK); fb_text_t(px + 110, py + 84, "FAT32", DK);
    fb_fill_rect(px + 12, py + 104, pw - 24, 1, SHc); fb_fill_rect(px + 12, py + 105, pw - 24, 1, WHITE);
    u32 CU = RGB(0xB0, 0x00, 0xB0), CF = RGB(0x00, 0x00, 0xC8);   /* Win95: used=magenta, free=blue */
    char nb[24], mb[16];
    /* used (magenta) legend */
    fb_fill_rect(px + 16, py + 116, 12, 12, CU); wm_bevel_sunken(px + 16, py + 116, 12, 12);
    commafy(g_dv_used, nb); fb_text_t(px + 34, py + 116, "Used space:", DK);
    fb_text_t(px + 130, py + 116, nb, DK); fb_text_t(px + 130, py + 130, "bytes", RGB(0x40,0x40,0x40));
    utoa_((u32)(g_dv_used / (1024*1024)), mb); { char l[24]; int k=0; for(char*s=mb;*s;s++)l[k++]=*s; const char*u=" MB"; for(const char*s=u;*s;s++)l[k++]=*s; l[k]=0; fb_text_t(px+180, py+130, l, RGB(0x40,0x40,0x40)); }
    /* free (blue) legend */
    fb_fill_rect(px + 16, py + 150, 12, 12, CF); wm_bevel_sunken(px + 16, py + 150, 12, 12);
    commafy(g_dv_free, nb); fb_text_t(px + 34, py + 150, "Free space:", DK);
    fb_text_t(px + 130, py + 150, nb, DK);
    utoa_((u32)(g_dv_free / (1024*1024)), mb); { char l[24]; int k=0; for(char*s=mb;*s;s++)l[k++]=*s; const char*u=" MB"; for(const char*s=u;*s;s++)l[k++]=*s; l[k]=0; fb_text_t(px+180, py+164, l, RGB(0x40,0x40,0x40)); }
    fb_fill_rect(px + 12, py + 184, pw - 24, 1, SHc); fb_fill_rect(px + 12, py + 185, pw - 24, 1, WHITE);
    commafy(g_dv_tot, nb); fb_text_t(px + 34, py + 192, "Capacity:", DK); fb_text_t(px + 130, py + 192, nb, DK);
    /* the capacity pie */
    int used_deg = g_dv_tot ? (int)((g_dv_used * 360ULL) / g_dv_tot) : 0;
    draw_pie(px + pw - 70, py + 150, 44, used_deg, CU, CF);
    int by = cy + ch - 32, bw = 70;
    cc_defbutton(cx + cw - bw * 2 - 15, by, bw, 24, "OK", 0);
    cc_button(cx + cw - bw - 8, by, bw, 24, "Cancel", 0);
}
static void dprops_click(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_dprops, &cx, &cy, &cw, &ch); (void)cx; (void)cy;
    if (ry >= ch - 32 && ry < ch - 8 && rx >= cw - 70 * 2 - 15) DestroyWindow(g_dprops);
}
/* ---- Win32 veneer: drive Properties WndProc ---- */
static LRESULT CALLBACK DriveProps_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: g_dprops = hwnd; return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            dprops_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: dprops_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_DESTROY: g_dprops = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
static void fm_drive_props(void) {
    g_dv_tot = g_dv_free = g_dv_used = 0;
    sys_disk_free(0, &g_dv_tot, &g_dv_free);
    g_dv_used = g_dv_tot > g_dv_free ? g_dv_tot - g_dv_free : 0;
    if (g_dprops) return;                                  /* single instance */
    CreateAppWindow(270, 110, 360, 316, "Debian95 (C:) Properties", DriveProps_WndProc, 0);
}
/* Send To > <dir>: copy the selected file into a target folder at the root */
static void fm_send_to(const char *dir) {
    if (g_sel < 0 || g_sel >= g_count || g_ents[g_sel].is_dir) return;
    u8 *data; u32 sz; char src[152]; full_of(g_sel, src);
    if (FsReadAlloc(src, &data, &sz) != 0) { g_status[0] = 0; append_(g_status, "Cannot read source"); return; }
    char dest[160]; dest[0] = 0; if (dir[0]) { append_(dest, dir); append_(dest, "/"); } append_(dest, g_ents[g_sel].name);
    int ok = FsWriteFile(dest, data, sz) == 0;
    kfree(data);
    g_status[0] = 0; if (ok) { append_(g_status, "Sent to "); append_(g_status, dir[0] ? dir : "Desktop"); }
    else append_(g_status, "Send To failed");
}
/* Create Shortcut: write a .lnk on the Desktop that points at the selected file */
static void fm_make_shortcut(int idx) {
    if (idx < 0 || idx >= g_count || g_ents[idx].is_dir || g_mycomp || g_printers) return;
    char tgt[160]; full_of(idx, tgt);                  /* target path relative to C: */
    char body[200]; int b = 0;
    body[b++] = 'L'; body[b++] = 'N'; body[b++] = 'K'; body[b++] = '\n';
    for (int i = 0; tgt[i] && b < 198; i++) body[b++] = tgt[i]; body[b++] = '\n'; body[b] = 0;
    FsMkdir("Desktop");
    char lnk[80]; int k = 0; const char *pre = "Desktop/Shortcut to ";
    for (; pre[k]; k++) lnk[k] = pre[k];
    for (int i = 0; g_ents[idx].name[i] && k < 70; i++) lnk[k++] = g_ents[idx].name[i];
    const char *ext = ".lnk"; for (int i = 0; ext[i]; i++) lnk[k++] = ext[i]; lnk[k] = 0;
    int ok = FsWriteFile(lnk, (const u8 *)body, (u32)b) == 0;
    g_status[0] = 0; append_(g_status, ok ? "Shortcut created on the Desktop" : "Could not create shortcut");
}
/* New > Folder/Text/Bitmap: create a uniquely-named item in the current folder */
static int fm_name_exists(const char *nm) {
    FsEntry e[64]; int n = FsListDir(g_path, e, 64);
    for (int i = 0; i < n; i++) { int eq = 1; for (int k = 0;; k++) { char a=e[i].name[k],b=nm[k]; if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; if(a!=b){eq=0;break;} if(!a)break; } if (eq) return 1; }
    return 0;
}
static void fm_new(int type) {
    if (g_mycomp || g_printers) return;
    const char *base = type == 0 ? "New Folder" : type == 1 ? "New Text Document" : "New Bitmap Image";
    const char *ext  = type == 0 ? "" : type == 1 ? ".txt" : ".bmp";
    char name[48];
    for (int nth = 1; nth < 100; nth++) {
        int k = 0; for (const char *s = base; *s && k < 40; s++) name[k++] = *s;
        if (nth > 1) { name[k++]=' '; name[k++]='('; if (nth>=10) name[k++]=(char)('0'+nth/10); name[k++]=(char)('0'+nth%10); name[k++]=')'; }
        for (const char *s = ext; *s; s++) name[k++] = *s; name[k] = 0;
        if (!fm_name_exists(name)) break;
    }
    char path[176]; int k = 0;
    for (int i = 0; g_path[i] && k < 120; i++) path[k++] = g_path[i];
    if (k) path[k++] = '/';
    for (int i = 0; name[i]; i++) path[k++] = name[i]; path[k] = 0;
    if (type == 0) FsMkdir(path);
    else if (type == 1) FsWriteFile(path, (const u8 *)"", 0);
    else { static const u8 bmp[58] = { 'B','M',58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,
        1,0,24,0,0,0,0,0,4,0,0,0,0x13,0x0B,0,0,0x13,0x0B,0,0,0,0,0,0,0,0,0,0,0xFF,0xFF,0xFF,0 };
        FsWriteFile(path, bmp, sizeof bmp); }
    fm_refresh();
    g_status[0] = 0; append_(g_status, "Created "); append_(g_status, name);
}
static void fm_emptyctx(int act) {
    if (act == 50) fm_drive_props();
    else if (act == 80) fm_new(0);
    else if (act == 81) fm_new(1);
    else if (act == 82) fm_new(2);
}
static void fm_ctx(int act) {
    if (g_sel < 0 || g_sel >= g_count) return;
    if (act == 1) fm_open(g_sel);
    else if (act == 90) {                              /* Open With... */
        full_of(g_sel, g_ow_full);
        int k = 0; for (; g_ents[g_sel].name[k] && k < 31; k++) g_ow_name[k] = g_ents[g_sel].name[k]; g_ow_name[k] = 0;
        openwith_open();
    }
    else if (act == 70) fm_make_shortcut(g_sel);
    else if (act == 2) fm_delete(g_sel);
    else if (act == 5 || act == 7) fm_clip_collect(act == 7);   /* Copy(5) / Cut(7) the selection */
    else if (act == 8) rename_begin(g_sel);
    else if (act == 4) fm_props(g_sel);
    else if (act == 30) fm_send_to("MyDocs");
    else if (act == 31) fm_send_to("");            /* Desktop (volume root) */
    else if (act == 32) fm_send_to("Windows");     /* stand-in for 3½ Floppy */
    else if (act == 50) fm_drive_props();          /* drive (container) Properties */
    else if (act == 60) fm_open(g_sel);            /* printer: open */
    else if (act == 61) { prn_delete(g_sel - 1); fm_refresh(); }   /* printer: delete */
    else if (act == 62) {                          /* eject a removable drive */
        int d = g_sel - NMYCOMP;
        if (g_mycomp && d >= 0 && d < g_ndisk) {
            int rc = sys_disk_eject(g_disk[d].mount);
            fm_refresh();
            wm_messagebox("Eject", rc == 0 ? "The drive can now be safely removed."
                                           : "The drive is in use and could not be ejected.");
        }
    }
}
static void fm_drivectx(int act) { if (act == 50) fm_drive_props(); }
static void fm_rclick(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
    /* right-click the drive node in the Folders tree -> drive Properties */
    if (rx < TREEW && ry >= TBH + ADRH) {
        int ay = ry + cy;
        for (int t = 0; t < g_ntree; t++)
            if (ay >= g_tree[t].y && ay < g_tree[t].y + 16 && g_tree[t].path[0] == 0 && g_tree[t].boxx != -1) {
                static const char *DM[] = { "Open", "-", "Properties" };
                static const int   DA[] = { 0, -1, 50 };
                wm_context_menu(DM, DA, 3, fm_drivectx); return;
            }
    }
    int i = view_hit(rx, ry, cw, ch);
    if (i < 0) {                                   /* empty area: New / Paste / Properties */
        g_sel = -1;
        if (g_mycomp || g_printers) {              /* virtual views: no New here */
            static const char *EM[] = { "Properties" }; static const int EA[] = { 50 };
            wm_context_menu(EM, EA, 1, fm_emptyctx); return;
        }
        static const char *NW_i[] = { "Folder", "-", "Text Document", "Bitmap Image" };
        static const int   NW_a[] = { 80, -1, 81, 82 };
        static const WmContextSub NW = { NW_i, NW_a, 4 };
        static const char *EM[] = { "New", "-", "Paste", "-", "Properties" };
        static const int   EA[] = { 0, -1, 0, -1, 50 };
        static const WmContextSub *ES[] = { &NW, 0, 0, 0, 0 };
        wm_context_menu_sub(EM, EA, ES, 5, fm_emptyctx); return;
    }
    g_sel = i;
    if (g_printers) {                              /* a printer row: allow Delete */
        if (i >= 1) { static const char *PM[] = { "Open", "-", "Delete" }; static const int PA[] = { 60, -1, 61 };
            wm_context_menu(PM, PA, 3, fm_ctx); }
        return;
    }
    if (g_mycomp) {                                /* a real drive row: Open / Eject / Properties */
        if (i >= NMYCOMP && i - NMYCOMP < g_ndisk) {
            if (g_disk[i - NMYCOMP].removable) {
                static const char *RM[] = { "Open", "-", "Eject", "-", "Properties" };
                static const int   RA[] = { 1, -1, 62, -1, 50 };
                wm_context_menu(RM, RA, 5, fm_ctx);
            } else { static const char *DM[] = { "Open", "-", "Properties" }; static const int DA[] = { 1, -1, 50 };
                wm_context_menu(DM, DA, 3, fm_ctx); }
        }
        return;                                    /* other virtual entries: no file context menu */
    }
    static const char *ST_i[] = { "3\xBD Floppy (A)", "Desktop (create shortcut)", "My Documents" };
    static const int   ST_a[] = { 32, 31, 30 };
    static const WmContextSub ST = { ST_i, ST_a, 3 };
    static const char *M[] = { "Open", "Open With...", "Send To", "-", "Cut", "Copy", "-", "Create Shortcut", "Delete", "Rename", "-", "Properties" };
    static const int   A[] = { 1, 90, 0, -1, 7, 5, -1, 70, 2, 8, -1, 4 };
    static const WmContextSub *S[] = { 0, 0, &ST, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    wm_context_menu_sub(M, A, S, 12, fm_ctx);
}

/* menu bar */
static const MenuItemW32 MF_file[] = { { "Open", 1 }, { "", -1 }, { "Close", 9 } };
static const MenuItemW32 MF_edit[] = { { "Cut", 0 }, { "Copy", 0 }, { "Paste", 0 } };
static const MenuItemW32 MF_view[] = {
    { "Large Icons", 20 }, { "Small Icons", 22 }, { "List", 23 }, { "Details", 21 }, { "", -1 }, { "Refresh", 3 },
};
static const MenuItemW32 MF_go[] = { { "Up One Level", 40 }, { "Home", 41 } };
static const MenuItemW32 MF_help[] = { { "About", 8 } };
static const MenuW32 FM_MENUS[] = {
    { "File", MF_file, 3 }, { "Edit", MF_edit, 3 }, { "View", MF_view, 6 },
    { "Go", MF_go, 2 }, { "Help", MF_help, 1 },
};
static void fm_menu(int id) {
    if (id == 1 && g_sel >= 0) fm_open(g_sel);
    else if (id == 20) g_view = 0; else if (id == 22) g_view = 2; else if (id == 23) g_view = 3; else if (id == 21) g_view = 1;
    else if (id == 3) fm_refresh();
    else if (id == 40) { if (g_mycomp) {} else if (g_printers) { g_printers = 0; g_mycomp = 1; fm_refresh(); } else if (g_path[0]) { path_up(); fm_refresh(); } else { g_mycomp = 1; fm_refresh(); } }
    else if (id == 41) nav_to("");
    else if (id == 9) DestroyWindow(g_win);
    else if (id == 8) wm_messagebox("Exploring - Debian95 (C:)",
        "Debian95 Explorer\nBrowse a real folder tree with a toolbar,\nfour view modes, copy/paste and rename.");
}

/* ---- Win32 veneer: main Explorer WndProc ---- */
static LRESULT CALLBACK Filemgr_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, FM_MENUS, 5);
            SetWindowIcon(hwnd, IC_FOLDER);
            SetWindowSizeGrip(hwnd);
            g_win = hwnd;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            fm_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_CHAR:        fm_key((char)wParam); return 0;
        case WM_LBUTTONDOWN: fm_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_RBUTTONDOWN: fm_rclick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_MOUSEMOVE:   fm_motion(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (u8)wParam); return 0;
        case WM_COMMAND:     fm_menu(LOWORD(wParam)); return 0;
        case WM_DESTROY:     g_sel = -1; g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void files_open(void) {
    if (!g_win)
        CreateAppWindow(90, 60, 500, 340, "Exploring - Debian95 (C:)", Filemgr_WndProc, 0);
    fm_load_icons();
    fm_refresh();
}
/* the desktop "My Computer" icon: open Explorer at the My Computer root */
void files_open_mycomputer(void) { g_mycomp = 1; g_printers = 0; files_open(); }
/* open Explorer directly at an absolute real path (e.g. "/home") */
void files_open_at(const char *abspath) {
    g_mycomp = 0; g_printers = 0;
    int i = 0; for (; abspath[i] && i < 126; i++) g_path[i] = abspath[i]; g_path[i] = 0;
    files_open();
}
