/* Debian95 — Task Manager, built the Win32 way: the window is a declarative
 * dialog TEMPLATE driven by a DlgProc. It lists the REAL operating-system
 * processes (sys_proc_list from /proc), sorted by memory, in a report listview;
 * End Process sends SIGTERM to the selection. No hand-placed pixels. */
#include <debian95/win32.h>
#include <debian95/ke.h>      /* struct lg_proc + sys_proc_list / sys_proc_kill */

enum { IDC_LIST = 100, IDC_STATUS = 101, IDC_END = 102, IDC_REFRESH = 103 };

static const DLGITEM tm_items[] = {
    { WC_LISTVIEW, LBS_NOTIFY,       7,   7, 286, 168, IDC_LIST,    0 },
    { WC_STATIC,   0,                7, 180, 150,   9, IDC_STATUS,  "" },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 200, 193, 93, 14, IDC_END,     "End Process" },
    { WC_BUTTON,   BS_PUSHBUTTON,    141, 193, 55, 14, IDC_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L tm_tmpl = { "Task Manager", 300, 214, tm_items, 4, DS_DLU };

static HWND g_dlg;
static struct lg_proc g_proc[256]; static int g_nproc;

static void utoa_(unsigned long v, char *b) {
    char t[24]; int i = 0; if (!v) { b[0]='0'; b[1]=0; return; }
    while (v) { t[i++] = (char)('0'+v%10); v/=10; } int j=0; while (i) b[j++]=t[--i]; b[j]=0;
}
static void tm_fill(HWND h) {
    g_nproc = sys_proc_list(g_proc, 256);
    for (int i = 0; i < g_nproc; i++)                    /* sort by memory, descending */
        for (int j = i+1; j < g_nproc; j++)
            if (g_proc[j].rss_kb > g_proc[i].rss_kb) { struct lg_proc t = g_proc[i]; g_proc[i] = g_proc[j]; g_proc[j] = t; }
    DlgListReset(h, IDC_LIST);
    for (int i = 0; i < g_nproc; i++) {
        char pid[16], mem[16]; utoa_((unsigned long)g_proc[i].pid, pid); utoa_(g_proc[i].rss_kb, mem);
        const char *cells[3] = { g_proc[i].name[0] ? g_proc[i].name : "?", pid, mem };
        DlgLVAddRow(h, IDC_LIST, cells, 3);
    }
    char st[40]; utoa_((unsigned long)g_nproc, st); { int k = 0; while (st[k]) k++;
        const char *s = " processes"; for (const char *p = s; *p; p++) st[k++] = *p; st[k] = 0; }
    SetDlgItemTextW32(h, IDC_STATUS, st);
}

static LRESULT CALLBACK Task_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            static const char *const titles[3] = { "Image Name", "PID", "Mem (K)" };
            static const int widths[3] = { 150, 55, 70 };
            DlgLVColumns(h, IDC_LIST, titles, widths, 3);
            tm_fill(h);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam), sel = DlgListGetSel(h, IDC_LIST);
            if (id == IDC_END && sel >= 0 && sel < g_nproc) { sys_proc_kill(g_proc[sel].pid); tm_fill(h); }
            else if (id == IDC_REFRESH) tm_fill(h);
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void taskmgr_open(void) {
    if (g_dlg) return;
    g_dlg = CreateDialogL(&tm_tmpl, Task_DlgProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 31, "Task Manager", IC_APP, 0, taskmgr_open, -1 };
#endif
