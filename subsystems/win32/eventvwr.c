/* Debian95 — Event Viewer: the real systemd journal as a Win95-style columnar log
 * (Type / Time / Source / Event), newest first, with a Type filter. Built the
 * resource-template way (WC_LISTVIEW + a combo + a DlgProc); the dialog engine
 * owns layout, paint, scroll and selection. Backed by sys_log_list (journalctl). */
#include <debian95/win32.h>

struct lg_logent { int level; char time[24]; char source[40]; char msg[200]; };
int sys_log_list(struct lg_logent *out, int max);

enum { IDC_LIST = 100, IDC_FILTER, IDC_REFRESH, IDC_COUNT };

static const DLGITEM ev_items[] = {
    { WC_LISTVIEW, LBS_NOTIFY,     7,   7, 286, 168, IDC_LIST,   0 },
    { WC_STATIC,   0,              7, 182,  28,  9, -1,          "Show:" },
    { WC_COMBO,    0,             36, 180,  96, 14, IDC_FILTER,  0 },
    { WC_STATIC,   0,            140, 182,  96,  9, IDC_COUNT,   "" },
    { WC_BUTTON,   BS_PUSHBUTTON, 243, 193, 50, 14, IDC_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L ev_tmpl = { "Event Viewer", 300, 214, ev_items, 5, DS_DLU };

static const char *EV_FILTER[] = { "All events", "Errors", "Warnings", "Information" };
static const char *EV_TYPE[]   = { "Error", "Warning", "Information" };

static HWND g_dlg;
static struct lg_logent g_ev[400]; static int g_nev;

static void put_int(char *b, int *k, int v) {
    char t[12]; int m = 0; if (!v) t[m++] = '0'; while (v) { t[m++] = (char)('0'+v%10); v/=10; }
    while (m) b[(*k)++] = t[--m];
}
static void ev_fill(HWND h) {
    g_nev = sys_log_list(g_ev, 400);
    int f = DlgListGetSel(h, IDC_FILTER); if (f < 0) f = 0;
    DlgListReset(h, IDC_LIST);
    int shown = 0;
    for (int i = 0; i < g_nev; i++) {
        if (f == 1 && g_ev[i].level != 0) continue;
        if (f == 2 && g_ev[i].level != 1) continue;
        if (f == 3 && g_ev[i].level != 2) continue;
        const char *cells[4] = { EV_TYPE[g_ev[i].level], g_ev[i].time, g_ev[i].source, g_ev[i].msg };
        DlgLVAddRow(h, IDC_LIST, cells, 4); shown++;
    }
    char st[48]; int k = 0; put_int(st, &k, shown);
    const char *sfx = " events"; for (const char *p = sfx; *p; p++) st[k++] = *p; st[k] = 0;
    SetDlgItemTextW32(h, IDC_COUNT, st);
}

static LRESULT CALLBACK EV_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            static const char *const titles[4] = { "Type", "Time", "Source", "Event" };
            static const int widths[4] = { 54, 78, 70, 190 };
            DlgLVColumns(h, IDC_LIST, titles, widths, 4);
            for (int i = 0; i < 4; i++) DlgListAdd(h, IDC_FILTER, EV_FILTER[i]);
            DlgListSetSel(h, IDC_FILTER, 0);
            ev_fill(h);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_REFRESH) ev_fill(h);
            else if (LOWORD(wParam) == IDC_FILTER && HIWORD(wParam) == CBN_SELCHANGE) ev_fill(h);
            return 0;
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void eventvwr_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&ev_tmpl, EV_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 65, "Event Viewer", IC_DOC, 0, eventvwr_open, -1 };
#endif
