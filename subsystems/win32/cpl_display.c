/* Debian95 — the Display Control-Panel applet as a Win32 PROPERTY SHEET:
 * Background (wallpaper listbox + an owner-drawn monitor preview), Screen Saver
 * (combo + wait), Appearance (scheme combo + owner-drawn preview), and Settings
 * (a resolution TRACKBAR that really mode-sets). Drives the real desktop through
 * accessors in applets.c. Templates + a DlgProc; no hand-placed pixels. */
#include <debian95/win32.h>
#include <debian95/fb.h>
#include <debian95/wm.h>
#include <debian95/font.h>

int  disp_wall_count(void); const char *disp_wall_name(int i); int disp_wall_current(void); void disp_wall_set(int i);
int  disp_res_count(void);  void disp_res_get(int i, int *w, int *h); int disp_res_current(void); int disp_res_apply(int i);
int  disp_ss_count(void);   const char *disp_ss_name(int i); int disp_ss_current(void); void disp_ss_set(int i);
int  disp_ss_wait(void);    void disp_ss_set_wait(int m);
int  disp_scheme_count(void); const char *disp_scheme_name(int i); void disp_scheme_colors(int i, u32 *tl, u32 *tr, u32 *dk);
int  sys_has_brightness(void); int sys_brightness_get(void); void sys_brightness_set(int pct);

enum { IDC_WALL = 1300, IDC_PREVIEW, IDC_SS, IDC_WAIT, IDC_SCHEME, IDC_APREVIEW, IDC_RES, IDC_RESLBL, IDC_BRIGHT, IDC_BRIGHTLBL };

static const DLGITEM p_bg[] = {
    { WC_STATIC,    0,          8,  6, 120, 9, -1, "Wallpaper:" },
    { WC_LISTBOX,   LBS_NOTIFY, 8, 18, 118, 96, IDC_WALL, 0 },
    { WC_OWNERDRAW, 0,        134, 18, 110, 96, IDC_PREVIEW, 0 },
};
static const DLGTEMPLATE_L t_bg = { "", 252, 122, p_bg, 3, DS_DLU };

static const DLGITEM p_ss[] = {
    { WC_STATIC, 0,             8, 10, 100, 9, -1, "Screen Saver:" },
    { WC_COMBO,  0,             8, 24, 160, 14, IDC_SS, 0 },
    { WC_STATIC, 0,             8, 48,  40, 9, -1, "Wait:" },
    { WC_EDIT,   0,            46, 46,  40, 12, IDC_WAIT, "" },
    { WC_STATIC, 0,            92, 48,  60, 9, -1, "minutes" },
};
static const DLGTEMPLATE_L t_ss = { "", 252, 122, p_ss, 5, DS_DLU };

static const DLGITEM p_app[] = {
    { WC_STATIC,    0,          8, 10, 100, 9, -1, "Scheme:" },
    { WC_COMBO,     0,          8, 24, 160, 14, IDC_SCHEME, 0 },
    { WC_OWNERDRAW, 0,          8, 46, 236, 64, IDC_APREVIEW, 0 },
};
static const DLGTEMPLATE_L t_app = { "", 252, 122, p_app, 3, DS_DLU };

static const DLGITEM p_set[] = {
    { WC_STATIC,   0,           8, 10, 120, 9, -1, "Screen area (drag, then Apply):" },
    { WC_TRACKBAR, 0,           8, 24, 236, 16, IDC_RES, 0 },
    { WC_STATIC,   0,           8, 46, 160, 9, IDC_RESLBL, "" },
    { WC_STATIC,   0,           8, 62, 236, 9, -1, "Adapter: virtio-gpu / VESA   Driver: framebuffer" },
    { WC_STATIC,   0,           8, 78,  70, 9, IDC_BRIGHTLBL, "Brightness:" },
    { WC_TRACKBAR, 0,          66, 76, 178, 16, IDC_BRIGHT, 0 },
};
static const DLGTEMPLATE_L t_set = { "", 252, 122, p_set, 6, DS_DLU };

static HWND g_sheet; static int g_scheme_sel;

static int put_uint(char *out, int k, int v) {   /* append v as decimal, return new length */
    char t[8]; int j = 0; if (!v) t[j++] = '0'; while (v) { t[j++] = (char)('0'+v%10); v /= 10; }
    while (j) out[k++] = t[--j]; return k;
}
static void res_label(HWND h) {
    int i = DlgTrackGetPos(h, IDC_RES), w = 0, ht = 0; disp_res_get(i, &w, &ht);
    char l[40] = "Resolution:  "; int k = 13;
    k = put_uint(l, k, w); l[k++] = ' '; l[k++] = 'x'; l[k++] = ' '; k = put_uint(l, k, ht); l[k] = 0;
    SetDlgItemTextW32(h, IDC_RESLBL, l);
}

static LRESULT CALLBACK Disp_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            DlgListReset(h, IDC_WALL);
            for (int i = 0; i < disp_wall_count(); i++) DlgListAdd(h, IDC_WALL, disp_wall_name(i));
            DlgListSetSel(h, IDC_WALL, disp_wall_current());
            for (int i = 0; i < disp_ss_count(); i++) DlgListAdd(h, IDC_SS, disp_ss_name(i));
            DlgListSetSel(h, IDC_SS, disp_ss_current());
            { char b[8]; int m = disp_ss_wait(), k=0,j=0; char t[8]; if(!m)t[j++]='0'; while(m){t[j++]=(char)('0'+m%10);m/=10;} while(j)b[k++]=t[--j]; b[k]=0; SetDlgItemTextW32(h, IDC_WAIT, b); }
            for (int i = 0; i < disp_scheme_count(); i++) DlgListAdd(h, IDC_SCHEME, disp_scheme_name(i));
            g_scheme_sel = 0; DlgListSetSel(h, IDC_SCHEME, 0);
            DlgTrackRange(h, IDC_RES, 0, disp_res_count() - 1);
            DlgTrackPos(h, IDC_RES, disp_res_current());
            res_label(h);
            { int b = sys_brightness_get();                       /* backlight: only meaningful on a laptop */
              DlgTrackRange(h, IDC_BRIGHT, 5, 100);
              if (b < 0) { SetDlgItemTextW32(h, IDC_BRIGHTLBL, "Brightness: (n/a)"); DlgTrackPos(h, IDC_BRIGHT, 100); }
              else       { DlgTrackPos(h, IDC_BRIGHT, b); } }
            return 0;
        }
        case WM_DRAWITEM: {
            RECT *rc = (RECT *)lParam; int x = rc->left, y = rc->top, w = rc->right - rc->left, ht = rc->bottom - rc->top;
            if ((int)wParam == IDC_PREVIEW) {                       /* a little monitor */
                fb_fill_rect(x, y, w, ht, RGB(0xC0,0xC0,0xC0));
                int mw = w - 20, mh = ht - 24, mx = x + 10, my = y + 6;
                fb_fill_rect(mx, my, mw, mh, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(mx, my, mw, mh);
                fb_fill_rect(mx + 6, my + 6, mw - 12, mh - 12, RGB(0x00,0x80,0x80)); wm_bevel_sunken(mx + 6, my + 6, mw - 12, mh - 12);
                int sel = DlgListGetSel(h, IDC_WALL); const char *nm = disp_wall_name(sel < 0 ? 0 : sel);
                fb_text_t(mx + 10, my + mh/2 - 4, nm, RGB(0xFF,0xFF,0xFF));
                fb_fill_rect(mx + mw/2 - 8, my + mh, 16, 6, RGB(0x80,0x80,0x80));   /* stand */
            } else if ((int)wParam == IDC_APREVIEW) {              /* title-bar gradient of the scheme */
                u32 tl = 0, tr = 0, dk = 0; disp_scheme_colors(g_scheme_sel, &tl, &tr, &dk);
                fb_fill_rect(x, y, w, ht, dk); wm_bevel_sunken(x, y, w, ht);
                int bx = x + 8, by = y + 8, bw = w - 16, bh = 20;
                fb_fill_rect(bx, by, bw, bh, RGB(0xC0,0xC0,0xC0)); wm_bevel_raised(bx, by, bw, bh);
                int lr=(tl>>16)&0xff,lg=(tl>>8)&0xff,lb=tl&0xff, rr=(tr>>16)&0xff,rg=(tr>>8)&0xff,rb=tr&0xff;
                for (int c = 0; c < bw - 4; c++) { int r=lr+(rr-lr)*c/(bw-4), g=lg+(rg-lg)*c/(bw-4), b=lb+(rb-lb)*c/(bw-4);
                    fb_fill_rect(bx + 2 + c, by + 2, 1, 14, RGB(r,g,b)); }
                fb_text_t(bx + 6, by + 5, "Active Window", RGB(0xFF,0xFF,0xFF));
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_WALL: if (HIWORD(wParam) == LBN_SELCHANGE) { int s = DlgListGetSel(h, IDC_WALL); if (s >= 0) disp_wall_set(s); } break;
                case IDC_SS:   if (HIWORD(wParam) == CBN_SELCHANGE) { int s = DlgListGetSel(h, IDC_SS); if (s >= 0) disp_ss_set(s); } break;
                case IDC_SCHEME: if (HIWORD(wParam) == CBN_SELCHANGE) { int s = DlgListGetSel(h, IDC_SCHEME); if (s >= 0) g_scheme_sel = s; } break;
                case IDC_RES:  if (HIWORD(wParam) == TBN_CHANGED) res_label(h); break;
                case IDC_BRIGHT: if (HIWORD(wParam) == TBN_CHANGED && sys_has_brightness()) sys_brightness_set(DlgTrackGetPos(h, IDC_BRIGHT)); break;
                case IDOK:
                    disp_res_apply(DlgTrackGetPos(h, IDC_RES));
                    { char w[8]; GetDlgItemTextW32(h, IDC_WAIT, w, sizeof w); int m = 0; for (int i=0; w[i]>='0'&&w[i]<='9'; i++) m = m*10 + (w[i]-'0'); if (m>0) disp_ss_set_wait(m); }
                    break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L disp_pages[] = {
    { "Background",   &t_bg,  Disp_DlgProc },
    { "Screen Saver", &t_ss,  Disp_DlgProc },
    { "Appearance",   &t_app, Disp_DlgProc },
    { "Settings",     &t_set, Disp_DlgProc },
};
static const PROPSHEETHEADER_L disp_psh = { "Display Properties", 252, 122, disp_pages, 4, DS_DLU };

void display_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&disp_psh, startpage); }
