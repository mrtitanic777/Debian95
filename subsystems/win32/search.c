/* Debian95 — Search: an instant, indexed file finder. Builds an in-memory index
 * of your files once (sys_search_build) and answers substring queries from memory
 * (sys_search_query) — the fast complement to Find's live walk. Declarative
 * dialog-engine UI (WC_LISTVIEW), real filesystem backend (shims_linux.c). */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

/* real indexed-search backend (shims_linux.c) */
int sys_search_build(void);
int sys_search_query(const char *q, char names[][64], char paths[][192], int max);

void wm_messagebox(const char *title, const char *msg);
extern void files_open_at(const char *abspath);   /* filemgr.c: open Explorer at a path */

#define MAXR 256
enum { IDC_PROMPT = 100, IDC_QUERY, IDC_FIND, IDC_REBUILD, IDC_LIST, IDC_OPEN, IDC_STATUS };

static const DLGITEM s_items[] = {
    { WC_STATIC, 0,                 7,   9, 60, 10, IDC_PROMPT, "Named:" },
    { WC_EDIT,   0,                50,   7, 160, 13, IDC_QUERY,  "" },
    { WC_BUTTON, BS_DEFPUSHBUTTON,216,   6,  40, 14, IDC_FIND,   "Find" },
    { WC_BUTTON, BS_PUSHBUTTON,   260,   6,  54, 14, IDC_REBUILD,"Reindex" },
    { WC_LISTVIEW, LBS_NOTIFY,      7,  26, 307, 158, IDC_LIST,  0 },
    { WC_BUTTON, BS_PUSHBUTTON,     7, 188,  90, 14, IDC_OPEN,   "Open Location" },
    { WC_STATIC, 0,               104, 190, 210, 10, IDC_STATUS, "" },
};
static const DLGTEMPLATE_L s_tmpl = { "Search", 321, 208, s_items, 7, DS_DLU };

static HWND g_dlg;
static char g_rname[MAXR][64], g_rpath[MAXR][192]; static int g_nr;

static void s_find(HWND h) {
    char q[128]; GetDlgItemTextW32(h, IDC_QUERY, q, sizeof q);
    static const char *const t[2] = { "Name", "In Folder" };
    static const int w[2] = { 150, 320 };
    DlgLVColumns(h, IDC_LIST, t, w, 2);
    DlgListReset(h, IDC_LIST);
    g_nr = q[0] ? sys_search_query(q, g_rname, g_rpath, MAXR) : 0;
    for (int i = 0; i < g_nr; i++) {
        char folder[192]; snprintf(folder, sizeof folder, "%s", g_rpath[i]);
        char *sl = strrchr(folder, '/'); if (sl) *sl = 0; else folder[0] = 0;
        const char *c[2] = { g_rname[i], folder[0] ? folder : "(top level)" };
        DlgLVAddRow(h, IDC_LIST, c, 2);
    }
    char st[48]; snprintf(st, sizeof st, q[0] ? "%d item(s) found." : "Type a name, then Find.", g_nr);
    SetDlgItemTextW32(h, IDC_STATUS, st);
}

static LRESULT CALLBACK S_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: SetDlgItemTextW32(h, IDC_STATUS, "Type a name, then Find."); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_FIND) s_find(h);
            else if (id == IDC_REBUILD) { int n = sys_search_build();
                char st[48]; snprintf(st, sizeof st, "Index rebuilt: %d item(s).", n); SetDlgItemTextW32(h, IDC_STATUS, st); }
            else if (id == IDC_OPEN) {
                int sidx = DlgListGetSel(h, IDC_LIST);
                if (sidx < 0 || sidx >= g_nr) { wm_messagebox("Search", "Select a result first."); return 0; }
                char folder[192]; snprintf(folder, sizeof folder, "%s", g_rpath[sidx]);
                char *sl = strrchr(folder, '/'); if (sl) *sl = 0; else folder[0] = 0;
                files_open_at(folder);
            }
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void search_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&s_tmpl, S_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 79, "Search", IC_FIND, 0, search_open, -1 };
#endif
