/* Debian95 — a Find dialog, declared entirely as a resource template (data) and
 * driven by a DlgProc. Reference for building dialogs the declarative way:
 * static text + edit + auto-checkboxes + default/normal buttons, with the engine
 * handling all layout, painting and input. Mirrors a classic Win95 Find box. */
#include <debian95/win32.h>

enum { IDC_FINDWHAT = 100, IDC_MATCHCASE = 101, IDC_WRAP = 102 };

/* the .rc-style template: controls are DATA, positioned in client pixels */
static const DLGITEM find_items[] = {
    { WC_STATIC, 0,                 10, 16,  60, 14, -1,           "Find what:" },
    { WC_EDIT,   0,                 74, 13, 150, 20, IDC_FINDWHAT, "" },
    { WC_BUTTON, BS_AUTOCHECKBOX,   12, 46, 130, 16, IDC_MATCHCASE,"Match case" },
    { WC_BUTTON, BS_AUTOCHECKBOX,   12, 68, 130, 16, IDC_WRAP,     "Wrap around" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 236, 12,  84, 24, IDOK,         "Find Next" },
    { WC_BUTTON, BS_PUSHBUTTON,    236, 44,  84, 24, IDCANCEL,     "Cancel" },
};
static const DLGTEMPLATE_L find_tmpl = { "Find", 332, 96, find_items, 6 };

static LRESULT CALLBACK Find_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            CheckDlgButton(h, IDC_WRAP, 1);           /* wrap on by default */
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    char what[64]; GetDlgItemTextW32(h, IDC_FINDWHAT, what, sizeof what);
                    int mc = IsDlgButtonChecked(h, IDC_MATCHCASE);
                    char msg[160]; int k = 0;
                    const char *a = "Searching for \""; for (int i = 0; a[i]; i++) msg[k++] = a[i];
                    for (int i = 0; what[i] && k < 120; i++) msg[k++] = what[i];
                    const char *b = "\"\nMatch case: "; for (int i = 0; b[i]; i++) msg[k++] = b[i];
                    const char *yn = mc ? "Yes" : "No"; for (int i = 0; yn[i]; i++) msg[k++] = yn[i];
                    msg[k] = 0;
                    MessageBoxW32(h, msg, "Find", MB_ICONINFO);
                    EndDialogL(h);
                    break;
                }
                case IDCANCEL: EndDialogL(h); break;
            }
            return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void finddlg_open(void) { CreateDialogL(&find_tmpl, Find_DlgProc, 0); }
