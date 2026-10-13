/* Debian95 — Scheduled Tasks: a Win-style front-end to the user's crontab (cron).
 * Lists scheduled tasks (Schedule / Command), and adds or deletes them. Declarative
 * WC_LISTVIEW dialog + a small New-Task dialog. */
#include <debian95/win32.h>

struct lg_cron { char when[48]; char cmd[160]; };
int sys_cron_list(struct lg_cron *out, int max);
int sys_cron_add(const char *when, const char *cmd);
int sys_cron_del(int index);

enum { IDC_LIST = 100, IDC_NEW, IDC_DEL, IDC_REFRESH };

static const DLGITEM ts_items[] = {
    { WC_LISTVIEW, LBS_NOTIFY,      7,   7, 286, 168, IDC_LIST,    0 },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 88, 193, 62, 14, IDC_NEW,     "New Task..." },
    { WC_BUTTON,   BS_PUSHBUTTON,  155, 193, 55, 14, IDC_DEL,     "Delete" },
    { WC_BUTTON,   BS_PUSHBUTTON,  238, 193, 55, 14, IDC_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L ts_tmpl = { "Scheduled Tasks", 300, 214, ts_items, 4, DS_DLU };

static HWND g_dlg;
static struct lg_cron g_cron[64]; static int g_ncron;

static void ts_fill(HWND h) {
    static const char *const titles[2] = { "Schedule", "Command" };
    static const int widths[2] = { 90, 190 };
    DlgLVColumns(h, IDC_LIST, titles, widths, 2);
    DlgListReset(h, IDC_LIST);
    g_ncron = sys_cron_list(g_cron, 64);
    for (int i = 0; i < g_ncron; i++) { const char *c[2] = { g_cron[i].when, g_cron[i].cmd }; DlgLVAddRow(h, IDC_LIST, c, 2); }
}

/* ---- New Task dialog ---- */
enum { IDC_WHEN = 200, IDC_CMD };
static const DLGITEM add_items[] = {
    { WC_STATIC, 0,                10, 12, 82, 12, -1, "Schedule (cron):" },
    { WC_EDIT,   0,                96, 10, 158, 14, IDC_WHEN, "0 9 * * *" },
    { WC_STATIC, 0,                10, 34, 82, 12, -1, "Command:" },
    { WC_EDIT,   0,                96, 32, 158, 14, IDC_CMD, "" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 96, 58, 74, 16, IDOK,     "OK" },
    { WC_BUTTON, BS_PUSHBUTTON,   174, 58, 74, 16, IDCANCEL, "Cancel" },
};
static const DLGTEMPLATE_L add_tmpl = { "New Scheduled Task", 266, 86, add_items, 6, DS_DLU };

static LRESULT CALLBACK Add_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                char when[64], cmd[192];
                GetDlgItemTextW32(h, IDC_WHEN, when, sizeof when);
                GetDlgItemTextW32(h, IDC_CMD, cmd, sizeof cmd);
                if (when[0] && cmd[0]) sys_cron_add(when, cmd);
                EndDialogL(h);
                if (g_dlg) ts_fill(g_dlg);
            } else if (LOWORD(wParam) == IDCANCEL) EndDialogL(h);
            return 0;
        case WM_DESTROY: return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK TS_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: ts_fill(h); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_NEW) CreateDialogL(&add_tmpl, Add_DlgProc, 0);
            else if (LOWORD(wParam) == IDC_DEL) { int s = DlgListGetSel(h, IDC_LIST); if (s >= 0 && s < g_ncron) { sys_cron_del(s); ts_fill(h); } }
            else if (LOWORD(wParam) == IDC_REFRESH) ts_fill(h);
            return 0;
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void taskschd_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&ts_tmpl, TS_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 68, "Scheduled Tasks", IC_SETTINGS, 0, taskschd_open, -1 };
#endif
