/* Debian95 — a loader for REAL Win32 dialog resources. Walks a PE file's resource
 * directory to the RT_DIALOG resources, parses the binary DLGTEMPLATE /
 * DLGTEMPLATEEX + DLGITEMTEMPLATE format Microsoft's rc.exe emits, and converts it
 * straight into our engine's DLGITEM/DLGTEMPLATE_L. Because our control atoms
 * (WC_BUTTON=0x80 ...), style bits (BS_* low nibble, ES_PASSWORD) and Dialog Units
 * already match Win32, a real Windows dialog renders in our own engine. "For fun,
 * to see if it works" — and it does, for standard controls. This reads structure
 * only (no Microsoft binaries live in the repo). */
#include <debian95/win32.h>
#include <debian95/mm.h>
#include <debian95/types.h>
#include <dirent.h>
#include <string.h>

int  FsReadAlloc(const char *name, u8 **out, u32 *len);   /* absolute paths read the real FS (shim) */
void wm_messagebox(const char *title, const char *msg);

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((u32)p[3]<<24); }
static int nstreq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
/* map a Win32 window-class NAME to our nearest control atom (common controls incl.) */
static u16 cls_to_wc(const char *c) {
    if (nstreq(c,"Button")) return WC_BUTTON;
    if (nstreq(c,"Edit") || nstreq(c,"RICHEDIT") || nstreq(c,"RichEdit20A") || nstreq(c,"RichEdit20W")) return WC_EDIT;
    if (nstreq(c,"ComboBox") || nstreq(c,"ComboBoxEx32")) return WC_COMBO;
    if (nstreq(c,"ListBox")) return WC_LISTBOX;
    if (nstreq(c,"SysListView32") || nstreq(c,"SysListView")) return WC_LISTVIEW;
    if (nstreq(c,"SysTreeView32")) return WC_TREEVIEW;
    if (nstreq(c,"msctls_trackbar32")) return WC_TRACKBAR;
    if (nstreq(c,"SysTabControl32") || nstreq(c,"SysTabControl")) return WC_TABFRAME;
    return WC_STATIC;                                            /* Static + unknown/custom */
}

/* ---- sz_Or_Ord: a menu/class/title field. Advances p; fills ordinal/ascii. ---- */
static const u8 *skip_szord(const u8 *p, int *is_ord, u16 *ord, char *out, int outn) {
    u16 w = rd16(p);
    if (w == 0x0000) { if (out && outn) out[0] = 0; if (is_ord) *is_ord = 0; return p + 2; }
    if (w == 0xFFFF) { if (is_ord) *is_ord = 1; if (ord) *ord = rd16(p + 2); if (out && outn) out[0] = 0; return p + 4; }
    int k = 0; const u8 *q = p;                       /* UTF-16LE string -> ASCII, stripping mnemonic '&' */
    while (rd16(q)) { u16 c = rd16(q); q += 2;
        if (c == '&') { if (rd16(q) == '&') { if (out && k < outn-1) out[k++] = '&'; q += 2; } continue; }  /* '&&' -> '&', lone '&' dropped */
        if (out && k < outn - 1 && c >= 32 && c < 127) out[k++] = (char)c;
    }
    if (out && outn) out[k] = 0; if (is_ord) *is_ord = 0; return q + 2;
}
static const u8 *skip_wstr(const u8 *p) { while (rd16(p)) p += 2; return p + 2; }
static const u8 *align4(const u8 *base, const u8 *p) { u32 o = (u32)(p - base); return base + ((o + 3) & ~3u); }

/* ---- convert one binary DLGTEMPLATE(EX) into a DLGTEMPLATE_L ---- */
#define RC_MAXITEM 64
static DLGITEM g_items[RC_MAXITEM];
static char    g_strs[RC_MAXITEM][64];
static char    g_caption[64];
static DLGTEMPLATE_L g_tmpl;

const DLGTEMPLATE_L *rc_parse_dialog(const u8 *buf, u32 len) {
    const u8 *p = buf; (void)len;
    int ex = (rd16(buf) == 0x0001 && rd16(buf + 2) == 0xFFFF);   /* DLGTEMPLATEEX: dlgVer=1, signature=0xFFFF */
    u32 style; int cdit; short cx, cy;
    if (ex) {
        p += 4;                       /* dlgVer + signature */
        p += 4;                       /* helpID */
        p += 4;                       /* exStyle */
        style = rd32(p); p += 4;
        cdit = rd16(p); p += 2;
        p += 2 + 2;                   /* x, y */
        cx = (short)rd16(p); p += 2; cy = (short)rd16(p); p += 2;
    } else {
        style = rd32(p); p += 4;
        p += 4;                       /* exStyle */
        cdit = rd16(p); p += 2;
        p += 2 + 2;                   /* x, y */
        cx = (short)rd16(p); p += 2; cy = (short)rd16(p); p += 2;
    }
    p = skip_szord(p, 0, 0, 0, 0);                 /* menu  */
    p = skip_szord(p, 0, 0, 0, 0);                 /* class */
    p = skip_szord(p, 0, 0, g_caption, sizeof g_caption);   /* title */
    if (style & 0x40) {                            /* DS_SETFONT: skip font info */
        if (ex) p += 2 + 2 + 1 + 1;                /* pointsize, weight, italic, charset */
        else    p += 2;                            /* pointsize */
        p = skip_wstr(p);                          /* typeface */
    }
    int n = 0;
    for (int i = 0; i < cdit && n < RC_MAXITEM; i++) {
        p = align4(buf, p);
        u32 istyle, id; short ix, iy, icx, icy;
        if (ex) { p += 4 + 4; istyle = rd32(p); p += 4;
            ix=(short)rd16(p);p+=2; iy=(short)rd16(p);p+=2; icx=(short)rd16(p);p+=2; icy=(short)rd16(p);p+=2;
            id = rd32(p); p += 4; }
        else { istyle = rd32(p); p += 4; p += 4;
            ix=(short)rd16(p);p+=2; iy=(short)rd16(p);p+=2; icx=(short)rd16(p);p+=2; icy=(short)rd16(p);p+=2;
            id = rd16(p); p += 2; }
        int isord = 0; u16 ord = 0; char cls[32];
        p = skip_szord(p, &isord, &ord, cls, sizeof cls);   /* window class */
        p = skip_szord(p, 0, 0, g_strs[n], sizeof g_strs[n]);/* caption */
        u16 extra = rd16(p); p += 2 + extra;                /* creation data */
        u16 wc;
        if (isord) { switch (ord) { case 0x80: wc = WC_BUTTON; break; case 0x81: wc = WC_EDIT; break;
                     case 0x83: wc = WC_LISTBOX; break; case 0x85: wc = WC_COMBO; break; default: wc = WC_STATIC; } }
        else { wc = cls_to_wc(cls); }
        DLGITEM *it = &g_items[n];
        it->wclass = wc; it->id = (int)id; it->x = ix; it->y = iy; it->cx = icx; it->cy = icy; it->text = g_strs[n];
        it->style = (wc == WC_BUTTON) ? (istyle & 0x0F) : (wc == WC_EDIT) ? (istyle & 0x20) : 0;
        n++;
    }
    g_tmpl.caption = g_caption[0] ? g_caption : "Dialog";
    g_tmpl.cx = cx; g_tmpl.cy = cy; g_tmpl.items = g_items; g_tmpl.nitems = n; g_tmpl.flags = DS_DLU;
    return &g_tmpl;
}

/* ---- PE resource walker: collect the RT_DIALOG resource blobs ---- */
typedef struct { u32 off, size; int is16; } RcDlg;   /* is16 = 16-bit NE dialog template */

static u32 rva2off(const u8 *buf, int nsec, u32 secoff, u32 rva) {
    for (int i = 0; i < nsec; i++) { const u8 *s = buf + secoff + i * 40;
        u32 va = rd32(s + 12), rawsz = rd32(s + 16), praw = rd32(s + 20);
        if (rva >= va && rva < va + rawsz) return praw + (rva - va);
    }
    return 0;
}
/* walk one resource subdirectory level, calling for each entry */
int pe_list_dialogs(const u8 *buf, u32 len, RcDlg *out, int max) {
    if (len < 0x40 || rd16(buf) != 0x5A4D) return 0;         /* 'MZ' */
    u32 pe = rd32(buf + 0x3C); if (pe + 24 > len || rd32(buf + pe) != 0x00004550) return 0;  /* 'PE\0\0' */
    u16 opt = rd16(buf + pe + 20);                           /* SizeOfOptionalHeader */
    int nsec = rd16(buf + pe + 6);
    u32 secoff = pe + 24 + opt;
    int magic = rd16(buf + pe + 24);
    u32 ddir = pe + 24 + (magic == 0x20B ? 112 : 96);        /* data directory (PE32 vs PE32+) */
    u32 resrva = rd32(buf + ddir + 2 * 8);                   /* directory[2] = RESOURCE */
    if (!resrva) return 0;
    u32 resoff = rva2off(buf, nsec, secoff, resrva); if (!resoff) return 0;
    /* level 1: types -> find RT_DIALOG (5) */
    const u8 *L1 = buf + resoff; int named = rd16(L1 + 12), idc = rd16(L1 + 14);
    u32 dlgsub = 0;
    for (int i = 0; i < named + idc; i++) { const u8 *e = L1 + 16 + i * 8;
        u32 name = rd32(e), o = rd32(e + 4);
        if (!(name & 0x80000000) && name == 5 && (o & 0x80000000)) { dlgsub = o & 0x7FFFFFFF; break; }
    }
    if (!dlgsub) return 0;
    /* level 2: each dialog name/id */
    const u8 *L2 = buf + resoff + dlgsub; int n2 = rd16(L2 + 12) + rd16(L2 + 14), got = 0;
    for (int i = 0; i < n2 && got < max; i++) { const u8 *e = L2 + 16 + i * 8;
        u32 o = rd32(e + 4); if (!(o & 0x80000000)) continue;
        const u8 *L3 = buf + resoff + (o & 0x7FFFFFFF);      /* level 3: languages */
        u32 de = rd32(L3 + 16 + 4);                          /* first lang -> data entry off */
        if (de & 0x80000000) continue;
        const u8 *DE = buf + resoff + de;                    /* IMAGE_RESOURCE_DATA_ENTRY */
        u32 datarva = rd32(DE), datasz = rd32(DE + 4);
        u32 dataoff = rva2off(buf, nsec, secoff, datarva);
        if (dataoff && dataoff + datasz <= len) { out[got].off = dataoff; out[got].size = datasz; got++; }
    }
    return got;
}

/* ================= 16-bit NE resources + 16-bit DIALOG templates ================= */
/* read a null-terminated ANSI string (mnemonic '&' stripped) */
static const u8 *nstr(const u8 *p, char *out, int outn) {
    int k = 0;
    while (*p) { u8 c = *p++;
        if (c == '&') { if (*p == '&') { if (out && k < outn-1) out[k++] = '&'; p++; } continue; }
        if (out && k < outn - 1 && c >= 32 && c < 127) out[k++] = (char)c;
    }
    if (out && outn) out[k] = 0; return p + 1;
}
/* dialog-header menu/class/caption field: 0x00 none, 0xFF+WORD ordinal, else string */
static const u8 *ne_hdr_field(const u8 *p, char *out, int outn) {
    if (*p == 0x00) { if (out && outn) out[0] = 0; return p + 1; }
    if (*p == 0xFF) { if (out && outn) out[0] = 0; return p + 3; }
    return nstr(p, out, outn);
}

int ne_list_dialogs(const u8 *buf, u32 len, RcDlg *out, int max) {
    if (len < 0x40 || rd16(buf) != 0x5A4D) return 0;
    u32 ne = rd32(buf + 0x3C); if (ne + 0x28 > len || rd16(buf + ne) != 0x454E) return 0;   /* 'NE' */
    u32 rtbl = ne + rd16(buf + ne + 0x24);                 /* resource table (rel to NE header) */
    if (rtbl + 2 > len) return 0;
    u32 p = rtbl; u16 align = rd16(buf + p); p += 2;
    int n = 0;
    while (p + 8 <= len) {
        u16 type = rd16(buf + p); if (type == 0) break;
        u16 count = rd16(buf + p + 2); p += 8;             /* skip rtReserved(4) */
        for (u16 i = 0; i < count && p + 12 <= len; i++, p += 12) {
            if ((type & 0x7FFF) == 5 && (type & 0x8000)) { /* RT_DIALOG (integer type 5) */
                u32 off = (u32)rd16(buf + p) << align, sz = (u32)rd16(buf + p + 2) << align;
                if (off && off + sz <= len && n < max) { out[n].off = off; out[n].size = sz; out[n].is16 = 1; n++; }
            }
        }
    }
    return n;
}

/* parse a 16-bit DLGTEMPLATE into our DLGTEMPLATE_L (coords are Dialog Units) */
const DLGTEMPLATE_L *rc_parse_dialog16(const u8 *buf, u32 len) {
    const u8 *p = buf, *end = buf + len;
    u32 style = rd32(p); p += 4;
    int cItems = *p++;
    short cx, cy;
    p += 2 + 2;                                            /* x, y */
    cx = (short)rd16(p); p += 2; cy = (short)rd16(p); p += 2;
    p = ne_hdr_field(p, 0, 0);                             /* menu */
    p = ne_hdr_field(p, 0, 0);                             /* class */
    p = ne_hdr_field(p, g_caption, sizeof g_caption);      /* caption */
    if (style & 0x40) { p += 2; p = nstr(p, 0, 0); }       /* DS_SETFONT: pointsize + face */
    int n = 0;
    for (int i = 0; i < cItems && n < RC_MAXITEM && p < end; i++) {
        short ix = (short)rd16(p); p += 2; short iy = (short)rd16(p); p += 2;
        short icx = (short)rd16(p); p += 2; short icy = (short)rd16(p); p += 2;
        u16 id = rd16(p); p += 2; u32 istyle = rd32(p); p += 4;
        u16 wc; u8 c0 = *p;                                 /* class: byte ordinal (>=0x80) or string */
        if (c0 >= 0x80) { p += 1; switch (c0) { case 0x80: wc = WC_BUTTON; break; case 0x81: wc = WC_EDIT; break;
                          case 0x83: wc = WC_LISTBOX; break; case 0x85: wc = WC_COMBO; break; default: wc = WC_STATIC; } }
        else { char cls[24]; p = nstr(p, cls, sizeof cls); wc = cls_to_wc(cls); }
        if (*p == 0xFF) p += 3; else p = nstr(p, g_strs[n], sizeof g_strs[n]);   /* text */
        u8 extra = *p++; p += extra;                       /* creation data */
        DLGITEM *it = &g_items[n];
        it->wclass = wc; it->id = id; it->x = ix; it->y = iy; it->cx = icx; it->cy = icy; it->text = g_strs[n];
        it->style = (wc == WC_BUTTON) ? (istyle & 0x0F) : (wc == WC_EDIT) ? (istyle & 0x20) : 0;
        n++;
    }
    g_tmpl.caption = g_caption[0] ? g_caption : "Dialog";
    g_tmpl.cx = cx; g_tmpl.cy = cy; g_tmpl.items = g_items; g_tmpl.nitems = n; g_tmpl.flags = DS_DLU;
    return &g_tmpl;
}

/* unified: list dialogs from a PE or NE module (sets each entry's is16) */
static int list_dialogs(const u8 *buf, u32 len, RcDlg *out, int max) {
    if (len < 0x40 || rd16(buf) != 0x5A4D) return 0;
    u32 e = rd32(buf + 0x3C);
    if (e + 2 <= len && rd16(buf + e) == 0x4550) { int n = pe_list_dialogs(buf, len, out, max);
        for (int i = 0; i < n; i++) out[i].is16 = 0; return n; }
    if (e + 2 <= len && rd16(buf + e) == 0x454E) return ne_list_dialogs(buf, len, out, max);
    return 0;
}
static const DLGTEMPLATE_L *parse_any(const u8 *blob, u32 sz, int is16) {
    return is16 ? rc_parse_dialog16(blob, sz) : rc_parse_dialog(blob, sz);
}

/* ---- public: load a real PE file, render its Nth dialog in our engine ---- */
static LRESULT CALLBACK Rc_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) { EndDialogL(h); return 0; }
    if (msg == WM_DESTROY) return 0;
    return DefWindowProc(h, msg, wParam, lParam);
}
void rc_open_file(const char *path, int index) {
    u8 *buf; u32 len;
    if (FsReadAlloc(path, &buf, &len) != 0) { wm_messagebox("Resource Loader", "Could not read the file."); return; }
    RcDlg dlg[128]; int n = list_dialogs(buf, len, dlg, 128);
    if (n <= 0) { wm_messagebox("Resource Loader", "No RT_DIALOG resources found in this file."); kfree(buf); return; }
    if (index < 0 || index >= n) index = 0;
    const DLGTEMPLATE_L *t = parse_any(buf + dlg[index].off, dlg[index].size, dlg[index].is16);
    CreateDialogL(t, Rc_DlgProc, 0);      /* strings were copied into static storage during parse */
    kfree(buf);
}

/* ==================== Resource Browser: enumerate + render EVERY dialog across a
 * whole directory of real Windows binaries. "Load all the resources." ==================== */
#define RC_MAXENTRY 600
typedef struct { char path[200]; u32 off, size; int is16; char label[48]; } RcEntry;
static RcEntry g_ent[RC_MAXENTRY]; static int g_nent;

static int has_ext(const char *n, const char *e3) {
    int L = 0; while (n[L]) L++; if (L < 4 || n[L-4] != '.') return 0;
    for (int i = 0; i < 3; i++) { char c = n[L-3+i]; if (c >= 'A' && c <= 'Z') c += 32; if (c != e3[i]) return 0; }
    return 1;
}
static void rc_scan_dir(const char *dir) {
    g_nent = 0;
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_nent < RC_MAXENTRY) {
        if (!(has_ext(e->d_name, "dll") || has_ext(e->d_name, "cpl") || has_ext(e->d_name, "exe"))) continue;
        char full[200]; int k = 0; for (const char *s = dir; *s && k < 190; s++) full[k++] = *s;
        if (k && full[k-1] != '/') full[k++] = '/';
        int base0 = k; for (const char *s = e->d_name; *s && k < 199; s++) full[k++] = *s; full[k] = 0;
        u8 *buf; u32 len;
        if (FsReadAlloc(full, &buf, &len) != 0) continue;
        if (len > 6*1024*1024) { kfree(buf); continue; }     /* skip huge files to stay snappy */
        RcDlg dl[128]; int n = list_dialogs(buf, len, dl, 128);
        for (int i = 0; i < n && g_nent < RC_MAXENTRY; i++) {
            const DLGTEMPLATE_L *t = parse_any(buf + dl[i].off, dl[i].size, dl[i].is16);
            if (!t || t->nitems == 0) continue;
            RcEntry *en = &g_ent[g_nent];
            int p = 0; for (const char *s = full; *s && p < 199; s++) en->path[p++] = *s; en->path[p] = 0;
            en->off = dl[i].off; en->size = dl[i].size; en->is16 = dl[i].is16;
            /* label = "basename: Title" */
            int q = 0; for (const char *s = full + base0; *s && q < 20; s++) en->label[q++] = *s;
            en->label[q++] = ':'; en->label[q++] = ' ';
            for (const char *s = t->caption; *s && q < 47; s++) en->label[q++] = *s; en->label[q] = 0;
            g_nent++;
        }
        kfree(buf);
    }
    closedir(d);
}

enum { IDC_RB_LIST = 1, IDC_RB_SHOW, IDC_RB_CLOSE };
static const DLGITEM rb_items[] = {
    { WC_STATIC,  0,           7,  6, 280, 9, -1, "Windows dialog resources found (double-click Show to render):" },
    { WC_LISTBOX, LBS_NOTIFY,  7, 18, 286, 150, IDC_RB_LIST, 0 },
    { WC_STATIC,  0,           7, 172, 180, 9, IDC_RB_CLOSE, "" },
    { WC_BUTTON,  BS_DEFPUSHBUTTON, 233, 170, 60, 14, IDC_RB_SHOW, "Show" },
};
static DLGTEMPLATE_L rb_tmpl = { "Resource Browser", 300, 190, rb_items, 4, DS_DLU };
static HWND g_rb;

static LRESULT CALLBACK Rb_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            for (int i = 0; i < g_nent; i++) DlgListAdd(h, IDC_RB_LIST, g_ent[i].label);
            char st[48] = "", n[12]; int k = 0; int v = g_nent; char t[12]; int j = 0; if(!v)t[j++]='0'; while(v){t[j++]=(char)('0'+v%10);v/=10;} while(j)st[k++]=t[--j];
            (void)n; const char *sfx = " dialogs"; for (const char *s = sfx; *s; s++) st[k++] = *s; st[k] = 0;
            SetDlgItemTextW32(h, IDC_RB_CLOSE, st);
            if (g_nent) DlgListSetSel(h, IDC_RB_LIST, 0);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_RB_SHOW) { int s = DlgListGetSel(h, IDC_RB_LIST);
                if (s >= 0 && s < g_nent) { u8 *buf; u32 len;
                    if (FsReadAlloc(g_ent[s].path, &buf, &len) == 0) {
                        const DLGTEMPLATE_L *t = parse_any(buf + g_ent[s].off, g_ent[s].size, g_ent[s].is16);
                        CreateDialogL(t, Rc_DlgProc, 0); kfree(buf);
                    } } }
            return 0;
        case WM_DESTROY: g_rb = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
void resbrowse_open(const char *dir) {
    if (g_rb) return;
    rc_scan_dir(dir);
    if (g_nent == 0) { wm_messagebox("Resource Browser", "No dialog resources found in that folder."); return; }
    g_rb = CreateDialogL(&rb_tmpl, Rb_DlgProc, 0);
}

/* desktop app entry: browse the configured Windows-binary folder (setting "resdir"
 * or DEBIAN95_RCDIR). Renders real Windows dialogs (PE + 16-bit NE) in our engine. */
int sys_get_setting(const char *key, char *out, int n);
void resviewer_open(void) {
    static char dir[256]; dir[0] = 0;
    if (sys_get_setting("resdir", dir, sizeof dir) == 0 && dir[0]) resbrowse_open(dir);
    else wm_messagebox("Resource Viewer",
        "Set the 'resdir' setting to a folder of\nWindows binaries (.dll/.exe/.cpl), then this\nbrowses and renders their dialogs in Debian95.");
}
