/* Debian95 — Links bar editor: add and remove the shortcuts shown on the taskbar
 * Links deskband. Entirely user-populated (Debian95 never ships fabricated
 * bookmarks). Declarative dialog-engine UI; link storage lives in wm.c and
 * persists in the settings. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

/* link store (wm.c) */
int         wm_links_count(void);
const char *wm_link_name(int i);
const char *wm_link_url(int i);
void        wm_links_add(const char *name, const char *url);
void        wm_links_remove(int i);

void wm_messagebox(const char *title, const char *msg);

enum { IDC_LIST = 100, IDC_NLBL, IDC_NAME, IDC_ULBL, IDC_URL, IDC_ADD, IDC_REMOVE, IDC_CLOSE };

static const DLGITEM d_items[] = {
    { WC_STATIC,   0,                 7,   6, 300, 10, -1,        "Links shown on the taskbar (click one to open it in the browser):" },
    { WC_LISTVIEW, LBS_NOTIFY,        7,  20, 300, 110, IDC_LIST, 0 },
    { WC_STATIC,   0,                 7, 136,  40, 10, IDC_NLBL,  "Name:" },
    { WC_EDIT,     0,                42, 134,  90, 13, IDC_NAME,  "" },
    { WC_STATIC,   0,               140, 136,  28, 10, IDC_ULBL,  "URL:" },
    { WC_EDIT,     0,               168, 134, 139, 13, IDC_URL,   "" },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 7, 154,  70, 14, IDC_ADD,    "Add" },
    { WC_BUTTON,   BS_PUSHBUTTON,   83, 154,  70, 14, IDC_REMOVE, "Remove" },
    { WC_BUTTON,   BS_PUSHBUTTON,  237, 154,  70, 14, IDC_CLOSE,  "Close" },
};
static const DLGTEMPLATE_L d_tmpl = { "Links", 314, 174, d_items, 9, DS_DLU };

static HWND g_dlg;

static void d_fill(HWND h) {
    static const char *const t[2] = { "Name", "Address" };
    static const int w[2] = { 110, 320 };
    DlgLVColumns(h, IDC_LIST, t, w, 2);
    DlgListReset(h, IDC_LIST);
    int n = wm_links_count();
    for (int i = 0; i < n; i++) { const char *c[2] = { wm_link_name(i), wm_link_url(i) }; DlgLVAddRow(h, IDC_LIST, c, 2); }
}

static LRESULT CALLBACK D_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: d_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_ADD) {
                char name[32], url[128];
                GetDlgItemTextW32(h, IDC_NAME, name, sizeof name);
                GetDlgItemTextW32(h, IDC_URL, url, sizeof url);
                if (!name[0] || !url[0]) { wm_messagebox("Links", "Enter both a name and a URL."); return 0; }
                wm_links_add(name, url);
                SetDlgItemTextW32(h, IDC_NAME, ""); SetDlgItemTextW32(h, IDC_URL, "");
                d_fill(h);
            } else if (id == IDC_REMOVE) {
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0) { wm_messagebox("Links", "Select a link to remove."); return 0; }
                wm_links_remove(s); d_fill(h);
            } else if (id == IDC_CLOSE) EndDialogL(h);
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void links_manage_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&d_tmpl, D_DlgProc, 0); }
