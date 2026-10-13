/* Debian95 — the Win95 "Open With" dialog: pick a program to open a file, with
 * the classic "Always use this program to open this type of file" checkbox.
 * Declared as a resource template + a DlgProc; the file manager does the actual
 * launch and remembers the association. */
#include <debian95/win32.h>

/* the programs the user can choose from (index == the stored association id) */
const char *g_ow_progs[] = {
    "Notepad", "WordPad", "Paint", "Image Viewer", "Media Player", "Sound Recorder"
};
#define NOW ((int)(sizeof g_ow_progs / sizeof g_ow_progs[0]))

void fm_openwith_launch(int prog, int always);   /* filemgr.c: run + (optionally) persist */
const char *fm_openwith_file(void);               /* filemgr.c: the file's display name */

enum { IDC_PROG = 200, IDC_ALWAYS, IDC_FILE };

static const DLGITEM ow_items[] = {
    { WC_STATIC,  0,                10,   8, 316, 12, -1, "Click the program you want to use to open this file:" },
    { WC_STATIC,  0,                10,  22, 316, 12, IDC_FILE, "" },           /* filename (set at init) */
    { WC_LISTBOX, LBS_NOTIFY,       10,  38, 300, 118, IDC_PROG, 0 },
    { WC_BUTTON,  BS_AUTOCHECKBOX,  10, 162, 316, 16, IDC_ALWAYS, "Always use this program to open this type of file" },
    { WC_BUTTON,  BS_DEFPUSHBUTTON,322,  38, 70, 24, IDOK,     "OK" },
    { WC_BUTTON,  BS_PUSHBUTTON,   322,  68, 70, 24, IDCANCEL, "Cancel" },
};
static const DLGTEMPLATE_L ow_tmpl = { "Open With", 402, 192, ow_items, 6 };

static LRESULT CALLBACK OW_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            const char *f = fm_openwith_file();
            char line[160]; int k = 0; const char *p = "File:  ";
            for (int i = 0; p[i]; i++) line[k++] = p[i];
            for (int i = 0; f && f[i] && k < 150; i++) line[k++] = f[i];
            line[k] = 0;
            SetDlgItemTextW32(h, IDC_FILE, line);
            for (int i = 0; i < NOW; i++) DlgListAdd(h, IDC_PROG, g_ow_progs[i]);
            DlgListSetSel(h, IDC_PROG, 0);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_PROG: break;
                case IDOK: {
                    int prog = DlgListGetSel(h, IDC_PROG);
                    int always = IsDlgButtonChecked(h, IDC_ALWAYS);
                    if (prog >= 0) fm_openwith_launch(prog, always);
                    EndDialogL(h);
                    break;
                }
                case IDCANCEL: EndDialogL(h); break;
            }
            return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void openwith_open(void) { CreateDialogL(&ow_tmpl, OW_DlgProc, 0); }
