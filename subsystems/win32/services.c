/* Debian95 — Services (services.msc), built the Win32 way: the window is a
 * declarative dialog TEMPLATE (controls are data) driven by a DlgProc. The engine
 * owns all layout, painting, selection, the scrollbar and input; this file only
 * fills the list from the real systemd services and acts on the buttons. No
 * hand-placed pixels, no mirrored hit-testing. */
#include <debian95/win32.h>

/* real system services (shims_linux.c) */
struct lg_svc { char name[48]; char desc[64]; int running; int enabled; };
int  sys_svc_list(struct lg_svc *out, int max);
int  sys_svc_start(const char *name);
int  sys_svc_stop(const char *name);

enum { IDC_LIST = 100, IDC_STATUS = 101, IDC_START = 102, IDC_STOP = 103, IDC_REFRESH = 104 };

/* the .rc-style template — positions in Dialog Units, like a real resource */
static const DLGITEM svc_items[] = {
    { WC_LISTVIEW, LBS_NOTIFY,     7,   7, 286, 168, IDC_LIST,    0 },
    { WC_STATIC,   0,              7, 180, 150,   9, IDC_STATUS,  "" },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 133, 193, 50, 14, IDC_START,   "Start" },
    { WC_BUTTON,   BS_PUSHBUTTON,  188, 193, 50,  14, IDC_STOP,    "Stop" },
    { WC_BUTTON,   BS_PUSHBUTTON,  243, 193, 50,  14, IDC_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L svc_tmpl = { "Services", 300, 214, svc_items, 5, DS_DLU };

static HWND g_dlg;
static struct lg_svc g_svc[256]; static int g_nsvc;

static void svc_fill(HWND h) {
    g_nsvc = sys_svc_list(g_svc, 256);
    for (int i = 0; i < g_nsvc; i++)                 /* running first, then input order */
        for (int j = i + 1; j < g_nsvc; j++)
            if (g_svc[j].running > g_svc[i].running) { struct lg_svc t = g_svc[i]; g_svc[i] = g_svc[j]; g_svc[j] = t; }
    DlgListReset(h, IDC_LIST);
    for (int i = 0; i < g_nsvc; i++) {
        const char *cells[3] = { g_svc[i].name, g_svc[i].running ? "Running" : "Stopped",
                                 g_svc[i].enabled ? "Automatic" : "Manual" };
        DlgLVAddRow(h, IDC_LIST, cells, 3);
    }
    char st[40]; int k = 0, v = g_nsvc; char t[12]; int m = 0;
    if (!v) t[m++] = '0'; while (v) { t[m++] = (char)('0'+v%10); v/=10; } while (m) st[k++] = t[--m];
    const char *sfx = " services"; for (const char *p = sfx; *p; p++) st[k++] = *p; st[k] = 0;
    SetDlgItemTextW32(h, IDC_STATUS, st);
}

static LRESULT CALLBACK Svc_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            static const char *const titles[3] = { "Service", "Status", "Startup" };
            static const int widths[3] = { 150, 60, 70 };   /* Dialog Units */
            DlgLVColumns(h, IDC_LIST, titles, widths, 3);
            svc_fill(h);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam), sel = DlgListGetSel(h, IDC_LIST);
            if (id == IDC_START && sel >= 0 && sel < g_nsvc) { sys_svc_start(g_svc[sel].name); svc_fill(h); }
            else if (id == IDC_STOP && sel >= 0 && sel < g_nsvc) { sys_svc_stop(g_svc[sel].name); svc_fill(h); }
            else if (id == IDC_REFRESH) svc_fill(h);
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void services_open(void) {
    if (g_dlg) return;
    g_dlg = CreateDialogL(&svc_tmpl, Svc_DlgProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 33, "Services", IC_APP, 0, services_open, -1 };
#endif
