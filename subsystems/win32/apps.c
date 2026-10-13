/* Debian95 — small shell apps: Help (a text window) and Find (a live file
 * search dialog). Reached from the Start menu. */
#include <debian95/wm.h>
#include <debian95/icons.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/nt.h>
#include <debian95/ps.h>
#include <debian95/rtl.h>

#define FACE  RGB(0xC0, 0xC0, 0xC0)
#define DK    RGB(0x00, 0x00, 0x00)

/* ---------------- Help ---------------- */
static const char *help_lines[] = {
    "Debian95 - quick help",
    "",
    "Desktop:  click Files, Editor or Paint, or use the Start menu.",
    "Windows:  drag the title bar to move; the corner grip resizes;",
    "          the X button closes and the taskbar button reopens.",
    "",
    "Shell commands:",
    "  help ls cat edit <f> run <f> win <f> view <f>",
    "  net ping dhcp ns <host> wget <url> dl <url> firewall",
    "  controls rings ver mem clear",
};
static Window *g_help;
static void help_close(void) { g_help = 0; }
void help_open(void) {
    if (!g_help) {
        g_help = wm_add_text(220, 150, 470, 210, "Debian95 Help",
                             help_lines, (int)(sizeof(help_lines) / sizeof(help_lines[0])));
        wm_set_icon(g_help, IC_HELP);
        wm_set_close(g_help, help_close);
    }
    wm_show(g_help);
}

/* ---------------- Find: All Files (the Start-menu Find applet) ---------------- */
#include "shellutil.h"
#define WHITE RGB(0xFF,0xFF,0xFF)
#define SEL   RGB(0x00,0x00,0x80)

static Window *g_find;
static void find_close(void) { g_find = 0; }

static char g_q[24];                    /* "Named:" query (e.g. *.txt) */
static int  g_qlen;
static int  g_tab;                       /* 0 Name&Location, 1 Date, 2 Advanced */
static int  g_subdirs = 1;               /* Include subfolders */
static int  g_ran;                        /* Find Now has been pressed */
static int  g_fsel = -1, g_ftop;          /* selected result + scroll top */

#define MAXR 128
static char r_name[MAXR][40]; static char r_dir[MAXR][48];
static u32  r_size[MAXR]; static u32 r_mtime[MAXR]; static int r_isdir[MAXR];
static int  g_nr;

static void lower_of(const char *s, char *o) { int i=0; for(; s[i]&&i<39; i++){ char c=s[i]; o[i]=(c>='A'&&c<='Z')?(char)(c+32):c; } o[i]=0; }
/* match the Named box: empty=all; "*.ext" or ".ext"=suffix; else substring */
static int q_match(const char *name) {
    if (!g_qlen) return 1;
    char ql[24], nl[40]; lower_of(g_q, ql); lower_of(name, nl);
    const char *pat = ql; if (pat[0]=='*') pat++;
    if (pat[0]=='.') { int a=0,b=0; while(nl[a])a++; while(pat[b])b++; if(b>a) return 0;
        for(int i=0;i<b;i++) if(nl[a-b+i]!=pat[i]) return 0; return 1; }
    for (int i=0; nl[i]; i++){ int j=0; while(ql[j]&&nl[i+j]==ql[j]) j++; if(!ql[j]) return 1; }
    return 0;
}
static void search_dir(const char *rel, const char *disp) {
    FsEntry e[64]; int n = FsListDir(rel, e, 64);
    for (int i = 0; i < n && g_nr < MAXR; i++) {
        if (q_match(e[i].name)) {
            int k=0; for(; e[i].name[k]&&k<39; k++) r_name[g_nr][k]=e[i].name[k]; r_name[g_nr][k]=0;
            int d=0; for(; disp[d]&&d<47; d++) r_dir[g_nr][d]=disp[d]; r_dir[g_nr][d]=0;
            r_size[g_nr]=e[i].size; r_mtime[g_nr]=e[i].mtime; r_isdir[g_nr]=e[i].is_dir; g_nr++;
        }
        if (e[i].is_dir && g_subdirs) {
            char sub[64]={0}, sd[48]={0}; int a=0;
            for (const char *p=rel; *p&&a<62; p++) sub[a++]=*p; if (a&&rel[0]) sub[a++]='/';
            for (const char *p=e[i].name; *p&&a<62; p++) sub[a++]=*p; sub[a]=0;
            int b=0; for(const char *p=disp; *p&&b<46; p++) sd[b++]=*p;
            if (b&&sd[b-1]!='\\') sd[b++]='\\'; for(const char *p=e[i].name; *p&&b<46; p++) sd[b++]=*p; sd[b]=0;
            search_dir(sub, sd);
        }
    }
}
static void run_find(void) { g_nr = 0; g_fsel = -1; g_ftop = 0; search_dir("", "C:\\"); g_ran = 1; }

/* ---- geometry ---- */
#define FIND_PANELH 100                 /* tab-body panel height (encloses all controls) */
static int listX(int cx)      { return cx + 8; }
static int listY(int cy)      { return cy + 26 + FIND_PANELH + 6; }
static int listW(int cw)      { return cw - 16; }
static int listH(int cy,int ch){ return (cy + ch - 22) - listY(cy) - 2; }
#define RROW 17
static const int COLW[5] = { 150, 120, 52, 100, 110 };   /* Name / In Folder / Size / Type / Modified */

static const char *find_menus_lbl[5] = { "File", "Edit", "View", "Options", "Help" };
static const WmMenuItem MF_file[] = { { "Open", 1 }, { "", -1 }, { "Close", 9 } };
static const WmMenuItem MF_opt[]  = { { "Save Results", 0 }, { "Case Sensitive", 0 } };
static const WmMenuItem MF_help[] = { { "About", 8 } };
static const WmMenu FIND_MENUS[] = {
    { "File", MF_file, 3 }, { "Edit", MF_file, 0 }, { "View", MF_file, 0 },
    { "Options", MF_opt, 2 }, { "Help", MF_help, 1 },
};
static void find_menu(int id) { if (id == 9) wm_hide(g_find); (void)find_menus_lbl; }

static void find_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    /* property-sheet tabs */
    static const char *tabs[3] = { "Name & Location", "Date Modified", "Advanced" };
    cc_tabs(cx + 6, cy + 4, 110, tabs, 3, g_tab);
    int px = cx + 8, py = cy + 26, pw = cw - 16, phb = FIND_PANELH;
    fb_fill_rect(px, py, pw, phb, FACE); wm_bevel_raised(px, py, pw, phb);
    /* right-hand action buttons (a fixed column); left controls stop at WALL */
    int bw = 92, brx = px + pw - bw - 12, wall = brx - 12;
    cc_defbutton(brx, py + 10, bw, 23, "Find Now", 0);
    cc_button(brx, py + 39, bw, 23, "Stop", 0);
    cc_button(brx, py + 68, bw, 23, "New Search", 0);
    int lblw = 62;                                   /* label column width */
    int ex = px + 14 + lblw;                          /* left of the edit fields */
    if (g_tab == 0) {
        cc_label(px + 14, py + 16, "Named:");
        cc_edit(ex, py + 12, wall - ex, 20, g_q, 1);
        cc_label(px + 14, py + 44, "Look in:");
        cc_button(wall - 62, py + 40, 62, 20, "Browse...", 0);
        cc_edit(ex, py + 40, (wall - 62 - 6) - ex, 20, "(C:)", 0);
        cc_checkbox(px + 16, py + 70, "Include subfolders", g_subdirs);
    } else if (g_tab == 1) {
        cc_radio(px + 16, py + 16, "All files", 1);
        cc_radio(px + 16, py + 40, "Find all files created or modified:", 0);
    } else {
        cc_label(px + 14, py + 16, "Of type:");
        cc_edit(ex, py + 12, wall - ex, 20, "All Files and Folders", 0);
        cc_label(px + 14, py + 44, "Containing text:");
        cc_edit(px + 14 + 104, py + 40, wall - (px + 14 + 104), 20, "", 0);
    }
    /* results list with a 5-column header */
    int lx = listX(cx), ly = listY(cy), lw = listW(cw), lh = listH(cy, ch);
    int hx = lx, hy = ly;
    for (int c = 0; c < 5; c++) {
        static const char *H[5] = { "Name", "In Folder", "Size", "Type", "Modified" };
        fb_fill_rect(hx, hy, COLW[c], 17, FACE); wm_bevel_raised(hx, hy, COLW[c], 17);
        fb_text_t(hx + 5, hy + 3, H[c], DK); hx += COLW[c];
    }
    int bx = lx, by = ly + 17, bw2 = lw, bh = lh - 17;
    fb_fill_rect(bx, by, bw2, bh, WHITE); wm_bevel_sunken(bx, by, bw2, bh);
    int rows = (bh - 4) / RROW, fh = (int)font_height();
    for (int r = 0; r < rows && g_ftop + r < g_nr; r++) {
        int i = g_ftop + r, ry = by + 2 + r * RROW, colx = bx + 2;
        if (i == g_fsel) fb_fill_rect(bx + 2, ry, COLW[0] - 2, RROW, SEL);
        u32 tc = (i == g_fsel) ? WHITE : DK;
        fb_text_t(colx + 20, ry + (RROW - fh) / 2, r_name[i], tc); colx += COLW[0];
        fb_text_t(colx + 4, ry + (RROW - fh) / 2, r_dir[i], DK); colx += COLW[1];
        if (!r_isdir[i]) { char s[16]; shell_size(r_size[i], s);
            fb_text_t(colx + COLW[2] - (int)font_measure(s) - 8, ry + (RROW - fh) / 2, s, DK); }
        colx += COLW[2];
        fb_text_t(colx + 4, ry + (RROW - fh) / 2, shell_type(r_name[i], r_isdir[i]), DK); colx += COLW[3];
        { char d[24]; shell_date(r_mtime[i], d); fb_text_t(colx + 4, ry + (RROW - fh) / 2, d, DK); }
    }
    /* status bar */
    int syb = cy + ch - 20;
    fb_fill_rect(cx, syb, cw, 20, FACE);
    char st[40]; st[0] = 0;
    if (g_ran) { char nb[12]; int v = g_nr, k = 0; char t[12]; if (!v){t[k++]='0';} while(v){t[k++]=(char)('0'+v%10); v/=10;} int j=0; while(k) nb[j++]=t[--k]; nb[j]=0;
        int p=0; for(int q=0;nb[q];q++) st[p++]=nb[q]; const char *w=" file(s) found"; for(int q=0;w[q];q++) st[p++]=w[q]; st[p]=0; }
    else { const char *w = "Ready"; int p=0; for(int q=0;w[q];q++) st[p++]=w[q]; st[p]=0; }
    fb_fill_rect(cx + 2, syb, 240, 18, FACE); wm_bevel_sunken(cx + 2, syb, 240, 18);
    fb_text_t(cx + 8, syb + 3, st, DK);
}

static int res_hit_abs(int absy, int cy) {
    int by = listY(cy) + 17 + 2;                   /* first result row, client-abs */
    int rel = absy - by; if (rel < 0) return -1;
    int r = rel / RROW, i = g_ftop + r;
    return (i >= 0 && i < g_nr) ? i : -1;
}
static void find_click(int rx, int ry) {
    int cx = 0, cy = 0, cw, ch; wm_client_rect(g_find, &cx, &cy, &cw, &ch);
    rx += cx; ry += cy;                       /* to client-abs used by geometry helpers */
    int t = cc_tab_hit(cx + 6, cy + 4, 110, 3, rx, ry);
    if (t >= 0) { g_tab = t; return; }
    int px = cx + 8, py = cy + 26, pw = cw - 16, bw = 92, brx = px + pw - bw - 12;
    if (rx >= brx && rx < brx + bw) {
        if (ry >= py + 10 && ry < py + 33) { run_find(); return; }             /* Find Now */
        if (ry >= py + 68 && ry < py + 91) { g_q[0]=0; g_qlen=0; g_nr=0; g_ran=0; g_fsel=-1; return; }  /* New Search */
    }
    if (g_tab == 0 && rx >= px + 16 && ry >= py + 68 && ry < py + 84) { g_subdirs = !g_subdirs; return; }
    int i = res_hit_abs(ry, cy);
    if (i >= 0) g_fsel = i;
}
static void find_key(char c) {
    if (g_tab != 0) return;
    if (c == '\b') { if (g_qlen) g_q[--g_qlen] = 0; }
    else if (c == '\n' || c == '\r') { run_find(); }
    else if (c >= 32 && c < 127 && g_qlen < 23) { g_q[g_qlen++] = c; g_q[g_qlen] = 0; }
}
static void find_ctx(int act) {
    if (g_fsel < 0 || g_fsel >= g_nr) return;
    if (act == 2) app_launch_doc(4, r_name[g_fsel]);      /* Open (Notepad module) */
}
static void find_rclick(int rx, int ry) {
    int cx, cy, cw, ch; wm_client_rect(g_find, &cx, &cy, &cw, &ch);
    int i = res_hit_abs(ry + cy, cy);
    if (i < 0) return;
    g_fsel = i;
    static const char *ST_i[] = { "3\xBD Floppy (A)", "Desktop (create shortcut)", "My Documents" };
    static const int   ST_a[] = { 30, 31, 32 };
    static const WmContextSub ST = { ST_i, ST_a, 3 };
    static const char *M[] = { "Open", "Print", "-", "Send To", "-", "Cut", "Copy", "-", "Create Shortcut", "Delete", "Rename", "-", "Properties" };
    static const int   A[] = { 2, 0, -1, 0, -1, 0, 0, -1, 0, 0, 0, -1, 4 };
    static const WmContextSub *S[] = { 0,0,0, &ST, 0,0,0,0,0,0,0,0,0 };
    wm_context_menu_sub(M, A, S, 13, find_ctx);
}

void find_open(void) {
    if (!g_find) {
        g_find = wm_add_custom(80, 55, 500, 380, "Find: All Files", find_paint, find_key, find_click);
        wm_set_icon(g_find, IC_FIND);
        wm_set_menu(g_find, FIND_MENUS, 5, find_menu);
        wm_set_rclick(g_find, find_rclick);
        wm_set_close(g_find, find_close);
        wm_set_sizegrip(g_find);
        wm_set_text_cursor(g_find);
        run_find();
    }
    wm_show(g_find);
}

/* ===================== System Monitor (live CPU + memory) ==================== */
int  sys_read_file(const char *path, char *out, int n);   /* /proc reads (shims) */
int  sys_mem(int *total_mb, int *free_mb);
#define SM_HIST 120
static Window *g_sm;
static u8   g_sm_cpu[SM_HIST], g_sm_mem[SM_HIST];   /* usage % history (0..100) */
static int  g_sm_n;                                  /* samples collected */
static unsigned long long g_sm_last_tot, g_sm_last_idle;
static u32  g_sm_last_ms;
static void sm_close(void) { g_sm = 0; }
static unsigned long long sm_u64(const char **p) {   /* parse a decimal, advance past spaces */
    const char *s = *p; while (*s == ' ') s++;
    unsigned long long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    *p = s; return v;
}
static void sm_sample(void) {
    char buf[256];
    if (sys_read_file("/proc/stat", buf, sizeof buf) == 0) {   /* "cpu  u n s idle iowait ..." */
        const char *p = buf; while (*p && *p != ' ') p++;      /* skip "cpu" */
        unsigned long long u = sm_u64(&p), n = sm_u64(&p), s = sm_u64(&p), idle = sm_u64(&p);
        unsigned long long io = sm_u64(&p), irq = sm_u64(&p), sirq = sm_u64(&p);
        unsigned long long tot = u + n + s + idle + io + irq + sirq, idl = idle + io;
        int pct = 0;
        if (g_sm_last_tot && tot > g_sm_last_tot) {
            unsigned long long dt = tot - g_sm_last_tot, di = idl - g_sm_last_idle;
            pct = (int)((dt - di) * 100 / dt); if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        }
        g_sm_last_tot = tot; g_sm_last_idle = idl;
        int mt = 0, mf = 0, mp = 0; if (sys_mem(&mt, &mf) == 0 && mt > 0) mp = (mt - mf) * 100 / mt;
        if (g_sm_n < SM_HIST) { g_sm_cpu[g_sm_n] = (u8)pct; g_sm_mem[g_sm_n] = (u8)mp; g_sm_n++; }
        else { for (int i = 1; i < SM_HIST; i++) { g_sm_cpu[i-1] = g_sm_cpu[i]; g_sm_mem[i-1] = g_sm_mem[i]; }
               g_sm_cpu[SM_HIST-1] = (u8)pct; g_sm_mem[SM_HIST-1] = (u8)mp; }
    }
}
int sysmon_tick(void) {                              /* sample ~1/s while the window is open */
    if (!g_sm) return 0;
    u32 now = wm_now_ms(); if (now - g_sm_last_ms < 1000) return 0;
    g_sm_last_ms = now; sm_sample(); return 1;
}
static void sm_graph(int x, int y, int w, int h, const u8 *hist, u32 line, const char *title) {
    fb_fill_rect(x, y, w, h, RGB(0x00,0x00,0x00)); wm_bevel_sunken(x, y, w, h);
    for (int gy = 1; gy < 4; gy++) { int yy = y + h * gy / 4; for (int xx = x+1; xx < x+w-1; xx += 4) fb_pixel(xx, yy, RGB(0x00,0x40,0x00)); }
    int n = g_sm_n; if (n > w - 2) n = w - 2;
    for (int i = 1; i < n; i++) {
        int i0 = g_sm_n - n + i - 1, i1 = g_sm_n - n + i;
        int x0 = x + w - n + i - 1, x1 = x + w - n + i;
        int y0 = y + h - 2 - (h - 4) * hist[i0] / 100, y1 = y + h - 2 - (h - 4) * hist[i1] / 100;
        int dy = y1 - y0, ady = dy < 0 ? -dy : dy, sy = dy < 0 ? -1 : 1;
        if (ady <= 1) { fb_pixel(x1, y1, line); }
        else for (int k = 0; k <= ady; k++) fb_pixel(x1, y0 + sy*k, line);
        fb_pixel(x0, y0, line);
    }
    fb_text_t(x + 4, y + 2, title, RGB(0x00,0xFF,0x00));
}
static int sm_lbl(char *o, const char *t, int v) {   /* "<t>NN%" */
    int k = 0; for (; t[k]; k++) o[k] = t[k];
    char d[8]; int n = 0; if (v == 0) d[n++] = '0'; while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) o[k++] = d[--n]; o[k++] = '%'; o[k] = 0; return k;
}
static void sm_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, RGB(0xC0,0xC0,0xC0));
    int cur_cpu = g_sm_n ? g_sm_cpu[g_sm_n-1] : 0, cur_mem = g_sm_n ? g_sm_mem[g_sm_n-1] : 0;
    char l[40];
    sm_lbl(l, "Processor Usage: ", cur_cpu); fb_text_t(cx+8, cy+6, l, RGB(0,0,0));
    sm_graph(cx+8, cy+22, cw-16, (ch-40)/2 - 6, g_sm_cpu, RGB(0x00,0xFF,0x00), "CPU");
    int my = cy + 22 + (ch-40)/2;
    sm_lbl(l, "Memory Usage: ", cur_mem); fb_text_t(cx+8, my, l, RGB(0,0,0));
    sm_graph(cx+8, my+16, cw-16, (ch-40)/2 - 6, g_sm_mem, RGB(0xFF,0xFF,0x00), "MEM");
}
void sysmon_open(void) {
    if (!g_sm) {
        g_sm_n = 0; g_sm_last_tot = 0; g_sm_last_ms = 0; sm_sample();
        g_sm = wm_add_custom(180, 100, 320, 260, "System Monitor", sm_paint, 0, 0);
        wm_set_icon(g_sm, IC_APP);
        wm_set_close(g_sm, sm_close);
        wm_set_sizegrip(g_sm);
    }
    wm_show(g_sm);
}
