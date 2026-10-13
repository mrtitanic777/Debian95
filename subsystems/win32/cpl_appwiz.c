/* Debian95 — Add/Remove Programs as a Win32 PROPERTY SHEET page and a real front
 * end to the Debian package system: an empty search box lists installed programs
 * (Remove); a query runs apt-cache search (Install). Declared as a resource
 * template + a DlgProc; the engine owns layout, the listbox and the buttons. */
#include <debian95/win32.h>

int sys_pkg_installed(char names[][48], int max);
int sys_pkg_search(const char *q, char names[][48], char descs[][96], int max);
int sys_pkg_install(const char *pkg);
int sys_pkg_remove(const char *pkg);

enum { IDC_SEARCH = 500, IDC_LIST, IDC_FIND, IDC_ACTION, IDC_MSG };

static const DLGITEM p_appwiz[] = {
    { WC_STATIC,  0,              8,   6, 244, 9, -1, "Type to search Debian, then Find; or pick an installed program:" },
    { WC_EDIT,    0,              8,  18, 190, 12, IDC_SEARCH, "" },
    { WC_BUTTON,  BS_PUSHBUTTON, 202, 18,  50, 12, IDC_FIND,   "Find" },
    { WC_LISTBOX, LBS_NOTIFY,     8,  36, 244, 70, IDC_LIST,   0 },
    { WC_STATIC,  0,              8, 112, 150, 9, IDC_MSG,     "" },
    { WC_BUTTON,  BS_DEFPUSHBUTTON,180,110, 72, 14, IDC_ACTION, "Remove" },
};
static const DLGTEMPLATE_L t_appwiz = { "", 260, 132, p_appwiz, 6, DS_DLU };

static HWND g_sheet;
static char g_names[48][48], g_desc[48][96]; static int g_n, g_searchmode;

static void appwiz_load(HWND h) {
    char q[48]; GetDlgItemTextW32(h, IDC_SEARCH, q, sizeof q);
    g_searchmode = (q[0] != 0);
    if (g_searchmode) g_n = sys_pkg_search(q, g_names, g_desc, 48);
    else { g_n = sys_pkg_installed(g_names, 48); for (int i = 0; i < g_n; i++) g_desc[i][0] = 0; }
    DlgListReset(h, IDC_LIST);
    for (int i = 0; i < g_n; i++) {
        char line[80]; int k = 0; for (; g_names[i][k] && k < 39; k++) line[k] = g_names[i][k]; line[k] = 0;
        DlgListAdd(h, IDC_LIST, line);
    }
    SetDlgItemTextW32(h, IDC_ACTION, g_searchmode ? "Install" : "Remove");
    SetDlgItemTextW32(h, IDC_MSG, g_n ? "" : (g_searchmode ? "No matches." : "No user programs installed."));
}

static LRESULT CALLBACK Appwiz_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: appwiz_load(h); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_FIND: appwiz_load(h); break;
                case IDC_ACTION: {
                    int sel = DlgListGetSel(h, IDC_LIST);
                    if (sel < 0 || sel >= g_n) { SetDlgItemTextW32(h, IDC_MSG, "Select a program first."); break; }
                    if (g_searchmode) { sys_pkg_install(g_names[sel]); SetDlgItemTextW32(h, IDC_MSG, "Installing in the background..."); }
                    else { sys_pkg_remove(g_names[sel]); SetDlgItemTextW32(h, IDC_MSG, "Removing in the background..."); }
                    break;
                }
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L aw_pages[] = { { "Install/Uninstall", &t_appwiz, Appwiz_DlgProc } };
static const PROPSHEETHEADER_L aw_psh = { "Add/Remove Programs", 260, 132, aw_pages, 1, DS_DLU };

void appwiz_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&aw_psh, startpage); }
