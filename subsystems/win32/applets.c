/* Debian95 — Control Panel backend for the Display applet + the applet router.
 *
 * Every Control-Panel applet is now a declarative property sheet in its own
 * cpl_*.c (built on the dialog engine). This file keeps only (1) the real
 * wallpaper / resolution / screen-saver / colour-scheme state that the Display
 * sheet (cpl_display.c) drives through the disp_* accessors — the single source
 * of truth — and (2) applet_open(), which routes each applet id to its sheet.
 * The old hand-drawn property-sheet framework (render_page/page_click/ps_*) was
 * removed once every applet was migrated. Arch-neutral. */
#include <debian95/win32.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/krt.h>
#include <debian95/nt.h>
#include <debian95/img.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
int  sys_set_resolution(int w, int h);
int  sys_list_resolutions(int *ws, int *hs, int max);   /* platform_linux.c */

/* applet ids (match control.c) */
enum { A_ACCESS, A_APPWIZ, A_DISPLAY, A_INTL, A_JOY, A_KEYBD, A_MODEM,
       A_MOUSE, A_MMSYS, A_NET, A_PASSWD, A_SYSTEM, A_TIMEDATE, A_MMEDIA, A_BLUETOOTH, A_FONTS, A_COUNT };

/* ---- Background: the classic Win95 wallpapers (on the FAT disk); 0 = none ---- */
static const char *g_wallpapers[] = {
    "(None)", "CLOUDS.BMP", "FOREST.BMP", "BUBBLES.BMP", "BAMBOO.BMP",
    "CIRCLES.BMP", "GATOR.BMP", "HOUNDS.BMP", "PYRAMID2.BMP", "REDTILE.BMP", "SAND.BMP",
    "SETUP.BMP", "RIVETS2.BMP", "THATCH2.BMP", "MESH.BMP", "EGYPT.BMP",
    "3DBLOCKS.BMP", "PSTRIPE.BMP", "WEAVE2.BMP", "HALFTONE.BMP", "OBSCURE.BMP",
};
#define NWALL ((int)(sizeof(g_wallpapers) / sizeof(g_wallpapers[0])))
static int  g_wall_sel;
static u32 *g_wall_cur;                  /* live wallpaper pixels (freed on change) */

static void apply_wallpaper(int i) {
    g_wall_sel = i;
    sys_set_setting("wallpaper", i <= 0 ? "(None)" : g_wallpapers[i]);   /* persist */
    if (i <= 0) { wm_set_wallpaper_enabled(0); return; }
    u8 *d; u32 n; u32 *px; int w, h;
    if (FsReadAlloc(g_wallpapers[i], &d, &n) == 0) {
        if (bmp_decode(d, n, &px, &w, &h) == 0) {
            wm_set_wallpaper_px(px, w, h);          /* keeps the pointer */
            wm_set_wallpaper_enabled(1);
            if (g_wall_cur) kfree(g_wall_cur);       /* free the previous one */
            g_wall_cur = px;
        }
        kfree(d);
    }
}
/* reflect the desktop's actual current wallpaper in the picker (read on open) */
static void wall_sync_from_settings(void) {
    char cur[40];
    if (sys_get_setting("wallpaper", cur, sizeof cur) != 0 || !cur[0]) { cur[0] = '('; cur[1] = 0; }   /* first run -> (None) */
    for (int i = 0; i < NWALL; i++) {
        int eq = 1; for (int k = 0;; k++) { char a = g_wallpapers[i][k], b = cur[k];
            if (a >= 'a' && a <= 'z') a -= 32; if (b >= 'a' && b <= 'z') b -= 32; if (a != b) { eq = 0; break; } if (!a) break; }
        if (eq) { g_wall_sel = i; return; }
    }
    g_wall_sel = 0;
}

/* ---- Settings: the display's REAL resolution list (its actual KMS modes) ---- */
static int g_res_list[24][2];
static int g_nres;
static int g_res_idx;
static void res_init(void) {
    if (g_nres) return;
    int ws[64], hs[64], n = sys_list_resolutions(ws, hs, 64);
    for (int i = 0; i < n; i++) {                     /* keep sane desktop sizes, de-dup */
        int w = ws[i], h = hs[i];
        if (w < 640 || h < 480 || w > 1600 || h > 1200) continue;
        int dup = 0; for (int j = 0; j < g_nres; j++) if (g_res_list[j][0]==w && g_res_list[j][1]==h) { dup = 1; break; }
        if (!dup && g_nres < 24) { g_res_list[g_nres][0]=w; g_res_list[g_nres][1]=h; g_nres++; }
    }
    if (!g_nres) {                                    /* fallback if enumeration failed */
        static const int def[][2] = { {640,480},{800,600},{1024,768},{1152,864},{1280,1024} };
        for (int i = 0; i < 5; i++) { g_res_list[i][0]=def[i][0]; g_res_list[i][1]=def[i][1]; } g_nres = 5;
    }
    for (int i = 0; i < g_nres; i++)                  /* ascending by pixel area (slider order) */
        for (int j = i+1; j < g_nres; j++)
            if (g_res_list[j][0]*g_res_list[j][1] < g_res_list[i][0]*g_res_list[i][1]) {
                int tw=g_res_list[i][0], th=g_res_list[i][1];
                g_res_list[i][0]=g_res_list[j][0]; g_res_list[i][1]=g_res_list[j][1];
                g_res_list[j][0]=tw; g_res_list[j][1]=th;
            }
}
static void res_sync_from_fb(void) {          /* reflect the actual current resolution */
    res_init();
    int w = (int)fb_width(), h = (int)fb_height();
    for (int i = 0; i < g_nres; i++) if (g_res_list[i][0] == w && g_res_list[i][1] == h) { g_res_idx = i; return; }
    g_res_idx = 0;
}

/* ---- Screen Saver: persisted name + wait ---- */
static const char *g_ss_names[] = { "(None)", "3D Flying Objects", "3D Pipes", "3D Text", "Blank Screen",
                                    "Flying Windows", "Mystify Your Mind", "Scrolling Marquee", "Starfield Simulation" };
#define NSS ((int)(sizeof g_ss_names / sizeof g_ss_names[0]))
static int g_ss_sel, g_ss_wait_min = 1;
static void ss_sync(void) {                       /* reflect persisted screen-saver state */
    char v[24];
    if (sys_get_setting("screensaver", v, sizeof v) == 0 && v[0]) {
        g_ss_sel = 0; for (int i = 0; i < NSS; i++) { int eq = 1; for (int k = 0;; k++) { if (g_ss_names[i][k] != v[k]) { eq = 0; break; } if (!v[k]) break; } if (eq) { g_ss_sel = i; break; } }
    }
    if (sys_get_setting("ss_wait", v, sizeof v) == 0 && v[0]) { int m = 0; for (char *p = v; *p >= '0' && *p <= '9'; p++) m = m*10 + (*p - '0'); if (m > 0) g_ss_wait_min = m; }
}

/* ---- Appearance: the Win95 colour schemes ---- */
static const struct { const char *name; u32 tl, tr, dk; } g_schemes[] = {
    { "Windows Standard",       RGB(0x00,0x00,0x80), RGB(0x10,0x84,0xD0), RGB(0x00,0x80,0x80) },
    { "Desert",                 RGB(0x80,0x40,0x00), RGB(0xC0,0x90,0x40), RGB(0x88,0x80,0x50) },
    { "Eggplant",               RGB(0x40,0x00,0x40), RGB(0x86,0x40,0x86), RGB(0x40,0x40,0x5c) },
    { "Rainy Day",              RGB(0x30,0x30,0x60), RGB(0x60,0x60,0xA0), RGB(0x48,0x50,0x60) },
    { "Rose",                   RGB(0x80,0x00,0x40), RGB(0xC0,0x40,0x80), RGB(0x80,0x48,0x60) },
    { "Slate",                  RGB(0x30,0x40,0x40), RGB(0x50,0x70,0x70), RGB(0x38,0x48,0x48) },
    { "High Contrast Black",    RGB(0x00,0x00,0x00), RGB(0x20,0x20,0x20), RGB(0x00,0x00,0x00) },
};
#define NSCHEME ((int)(sizeof(g_schemes) / sizeof(g_schemes[0])))

static void utoa(unsigned v, char *b) {
    char t[12]; int i = 0; if (!v) { b[0]='0'; b[1]=0; return; }
    while (v) { t[i++] = (char)('0'+v%10); v/=10; } int j=0; while(i) b[j++]=t[--i]; b[j]=0;
}

/* ---- accessors: cpl_display.c drives the real state through these (single
 * source of truth stays here). ---- */
int  disp_wall_count(void) { return NWALL; }
const char *disp_wall_name(int i) { return (i >= 0 && i < NWALL) ? g_wallpapers[i] : ""; }
int  disp_wall_current(void) { wall_sync_from_settings(); return g_wall_sel; }
void disp_wall_set(int i) { apply_wallpaper(i); }
int  disp_res_count(void) { res_init(); return g_nres; }
void disp_res_get(int i, int *w, int *h) { if (i >= 0 && i < g_nres) { *w = g_res_list[i][0]; *h = g_res_list[i][1]; } }
int  disp_res_current(void) { res_sync_from_fb(); return g_res_idx; }
int  disp_res_apply(int i) {
    if (i < 0 || i >= g_nres) return -1; int w = g_res_list[i][0], h = g_res_list[i][1];
    if (w == (int)fb_width() && h == (int)fb_height()) return 0;
    if (sys_set_resolution(w, h) != 0) return -1;
    char rs[16]; int k = 0; char b[8]; utoa((unsigned)w, b); for (char *s = b; *s; s++) rs[k++] = *s; rs[k++] = 'x';
    utoa((unsigned)h, b); for (char *s = b; *s; s++) rs[k++] = *s; rs[k] = 0; sys_set_setting("resolution", rs); return 0;
}
int  disp_ss_count(void) { return NSS; }
const char *disp_ss_name(int i) { return (i >= 0 && i < NSS) ? g_ss_names[i] : ""; }
int  disp_ss_current(void) { ss_sync(); return g_ss_sel; }
void disp_ss_set(int i) { if (i >= 0 && i < NSS) { g_ss_sel = i; sys_set_setting("screensaver", g_ss_names[i]); } }
int  disp_ss_wait(void) { return g_ss_wait_min; }
void disp_ss_set_wait(int m) { if (m < 1) m = 1; g_ss_wait_min = m; char b[8]; utoa((unsigned)m, b); sys_set_setting("ss_wait", b); }
int  disp_scheme_count(void) { return NSCHEME; }
const char *disp_scheme_name(int i) { return (i >= 0 && i < NSCHEME) ? g_schemes[i].name : ""; }
void disp_scheme_colors(int i, u32 *tl, u32 *tr, u32 *dk) { if (i >= 0 && i < NSCHEME) { if (tl) *tl = g_schemes[i].tl; if (tr) *tr = g_schemes[i].tr; if (dk) *dk = g_schemes[i].dk; } }

/* ---- the applet router: each id opens its migrated property sheet (cpl_*.c) ---- */
void applet_open(int a) {
    if (a == A_PASSWD) { extern void passwords_sheet_open(int); passwords_sheet_open(0); return; }
    if (a == A_NET)    { extern void network_sheet_open(int);   network_sheet_open(0);   return; }
    if (a == A_ACCESS) { extern void access_sheet_open(int);    access_sheet_open(0);    return; }
    if (a == A_APPWIZ) { extern void appwiz_sheet_open(int);    appwiz_sheet_open(0);    return; }
    if (a == A_SYSTEM) { extern void system_sheet_open(int);    system_sheet_open(0);    return; }
    if (a == A_MOUSE)  { extern void mouse_sheet_open(int);     mouse_sheet_open(0);     return; }
    if (a == A_KEYBD)  { extern void keyboard_sheet_open(int);  keyboard_sheet_open(0);  return; }
    if (a == A_JOY)    { extern void joystick_sheet_open(int);  joystick_sheet_open(0);  return; }
    if (a == A_MODEM)  { extern void modem_sheet_open(int);     modem_sheet_open(0);     return; }
    if (a == A_MMSYS)  { extern void sounds_sheet_open(int);    sounds_sheet_open(0);    return; }
    if (a == A_MMEDIA) { extern void multimedia_sheet_open(int);multimedia_sheet_open(0);return; }
    if (a == A_BLUETOOTH){ extern void bluetooth_sheet_open(int);bluetooth_sheet_open(0); return; }
    if (a == A_INTL)   { extern void regional_sheet_open(int);  regional_sheet_open(0);  return; }
    if (a == A_TIMEDATE){ extern void datetime_sheet_open(int); datetime_sheet_open(0);  return; }
    if (a == A_DISPLAY) { extern void display_sheet_open(int);  display_sheet_open(0);   return; }
    if (a == A_FONTS)  { extern void fonts_open(void);          fonts_open();            return; }
}
void applet_open_tab(int a, int tab) {   /* testing hook: open an applet on a specific tab */
    if (a == A_PASSWD) { extern void passwords_sheet_open(int); passwords_sheet_open(tab); return; }
    if (a == A_NET)    { extern void network_sheet_open(int);   network_sheet_open(tab);   return; }
    if (a == A_ACCESS) { extern void access_sheet_open(int);    access_sheet_open(tab);    return; }
    if (a == A_APPWIZ) { extern void appwiz_sheet_open(int);    appwiz_sheet_open(tab);    return; }
    if (a == A_SYSTEM) { extern void system_sheet_open(int);    system_sheet_open(tab);    return; }
    if (a == A_MOUSE)  { extern void mouse_sheet_open(int);     mouse_sheet_open(tab);     return; }
    if (a == A_KEYBD)  { extern void keyboard_sheet_open(int);  keyboard_sheet_open(tab);  return; }
    if (a == A_JOY)    { extern void joystick_sheet_open(int);  joystick_sheet_open(tab);  return; }
    if (a == A_MODEM)  { extern void modem_sheet_open(int);     modem_sheet_open(tab);     return; }
    if (a == A_MMSYS)  { extern void sounds_sheet_open(int);    sounds_sheet_open(tab);    return; }
    if (a == A_MMEDIA) { extern void multimedia_sheet_open(int);multimedia_sheet_open(tab);return; }
    if (a == A_BLUETOOTH){ extern void bluetooth_sheet_open(int);bluetooth_sheet_open(tab); return; }
    if (a == A_INTL)   { extern void regional_sheet_open(int);  regional_sheet_open(tab);  return; }
    if (a == A_TIMEDATE){ extern void datetime_sheet_open(int); datetime_sheet_open(tab);  return; }
    if (a == A_DISPLAY) { extern void display_sheet_open(int);  display_sheet_open(tab);   return; }
    if (a == A_FONTS)  { extern void fonts_open(void);          fonts_open();              return; }
    (void)tab;
}
