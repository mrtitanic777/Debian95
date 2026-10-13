/* Debian95 — Control Panel > Fonts: the classic Fonts folder, backed by real
 * fontconfig. Lists every installed font family (fc-list), installs a new
 * .ttf/.otf/.ttc into the user font dir + rebuilds the cache (fc-cache), and
 * double-click shows a family's real file + style metadata. Declarative
 * dialog-engine UI; no faked glyph preview. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

/* real fontconfig backend (shims_linux.c) */
int sys_fonts_available(void);
int sys_fonts_list(char fam[][64], char file[][96], int max);
int sys_font_info(const char *family, char *out, int n);
int sys_font_install(const char *src);
int sys_font_delete(const char *file);

void wm_messagebox(const char *title, const char *msg);
void wm_confirm(const char *title, const char *text, void (*onyes)(void));
void fontprev_open(const char *file, const char *family);   /* fontprev.c: real stb_truetype preview */

#define MAXF 512
enum { IDC_COUNT = 100, IDC_LIST, IDC_INSTALL, IDC_PREVIEW, IDC_PROPS, IDC_DELETE, IDC_REFRESH };

static const DLGITEM f_items[] = {
    { WC_STATIC,   0,                 7,   7, 320, 10, IDC_COUNT,  "Fonts installed on this computer:" },
    { WC_LISTVIEW, LBS_NOTIFY,        7,  20, 320, 170, IDC_LIST,  0 },
    { WC_BUTTON,   BS_DEFPUSHBUTTON,  7, 196,  96, 14, IDC_INSTALL,"Install New Font..." },
    { WC_BUTTON,   BS_PUSHBUTTON,   107, 196,  60, 14, IDC_PREVIEW,"Preview" },
    { WC_BUTTON,   BS_PUSHBUTTON,   171, 196,  66, 14, IDC_PROPS,  "Properties" },
    { WC_BUTTON,   BS_PUSHBUTTON,   241, 196,  46, 14, IDC_DELETE, "Delete" },
    { WC_BUTTON,   BS_PUSHBUTTON,   291, 196,  36, 14, IDC_REFRESH,"Refresh" },
};
static const DLGTEMPLATE_L f_tmpl = { "Fonts", 334, 216, f_items, 7, DS_DLU };

static HWND g_dlg;
static char g_fam[MAXF][64], g_file[MAXF][96]; static int g_nf;
static char g_del_file[96];

static void f_fill(HWND h) {
    static const char *const t[2] = { "Font Name", "File" };
    static const int w[2] = { 180, 300 };
    DlgLVColumns(h, IDC_LIST, t, w, 2);
    DlgListReset(h, IDC_LIST);
    g_nf = sys_fonts_list(g_fam, g_file, MAXF);
    for (int i = 0; i < g_nf; i++) { const char *c[2] = { g_fam[i], g_file[i] }; DlgLVAddRow(h, IDC_LIST, c, 2); }
    char cap[64]; snprintf(cap, sizeof cap, "%d font(s) installed on this computer:", g_nf);
    SetDlgItemTextW32(h, IDC_COUNT, cap);
}

/* GetOpenFileName delivers the pick here */
static void f_on_pick(const char *path) {
    int r = sys_font_install(path);
    if (g_dlg) f_fill(g_dlg);
    wm_messagebox("Fonts",
        r == 0  ? "The font was installed and the font cache rebuilt." :
        r == -2 ? "Not a font file.\nChoose a .ttf, .otf or .ttc file." :
                  "Could not install the font.\nCheck the file and try again.");
}

static void f_do_delete(void) {
    int r = sys_font_delete(g_del_file);
    if (g_dlg) f_fill(g_dlg);
    if (r == -2) wm_messagebox("Fonts", "That is a system font and cannot be removed.\nOnly fonts you installed can be deleted.");
}

static LRESULT CALLBACK F_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            if (!sys_fonts_available()) { SetDlgItemTextW32(h, IDC_COUNT, "Fonts needs 'fontconfig', which is not installed on this machine."); return 1; }
            f_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_INSTALL) {
                static OPENFILENAME ofn; memset(&ofn, 0, sizeof ofn);
                ofn.title = "Install New Font"; ofn.filter = "*.TTF;*.OTF;*.TTC"; ofn.onok = f_on_pick;
                GetOpenFileName(&ofn);
            } else if (id == IDC_REFRESH) f_fill(h);
            else if (id == IDC_PREVIEW) {                   /* real rendered sample (stb_truetype) */
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0 || s >= g_nf) { wm_messagebox("Fonts", "Select a font to preview."); return 0; }
                fontprev_open(g_file[s], g_fam[s]);
            }
            else if (id == IDC_PROPS) {                     /* real file + style metadata */
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0 || s >= g_nf) { wm_messagebox("Fonts", "Select a font to see its properties."); return 0; }
                static char info[1024]; char body[1152];
                if (sys_font_info(g_fam[s], info, sizeof info) == 0)
                    snprintf(body, sizeof body, "%s\n\n%s", g_fam[s], info);
                else snprintf(body, sizeof body, "%s\n\n%s", g_fam[s], g_file[s]);
                wm_messagebox(g_fam[s], body);
            } else if (id == IDC_DELETE) {
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0 || s >= g_nf) { wm_messagebox("Fonts", "Select a font to delete."); return 0; }
                snprintf(g_del_file, sizeof g_del_file, "%s", g_file[s]);
                wm_confirm("Delete Font", "Remove the selected font from this computer?", f_do_delete);
            }
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void fonts_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&f_tmpl, F_DlgProc, 0); }
