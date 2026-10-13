/* Debian95 — Network Neighborhood: browse the real SMB/CIFS network. Level 0 lists
 * the workgroup computers (sys_net_hosts); opening one lists its shared folders
 * (sys_net_shares); opening a share mounts it (sys_net_mount, guest CIFS) and hands
 * it to Explorer. Declarative WC_LISTVIEW dialog; the engine owns paint/scroll/sel.
 * On a network with no SMB servers (a VM) it shows "No computers found". */
#include <debian95/win32.h>

int  sys_net_hosts(char names[][64], int max);
int  sys_net_shares(const char *host, char shares[][64], int max);
int  sys_net_mount(const char *host, const char *share, char *mountpoint, int mn);
void files_open_at(const char *abspath);         /* filemgr.c: open a path in Explorer */
void wm_messagebox(const char *title, const char *msg);

enum { IDC_LIST = 100, IDC_OPEN, IDC_UP, IDC_REFRESH, IDC_STATUS };

static const DLGITEM nn_items[] = {
    { WC_LISTVIEW, LBS_NOTIFY,      7,   7, 286, 168, IDC_LIST,    0 },
    { WC_STATIC,   0,              7, 180, 160,   9, IDC_STATUS,  "" },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 112, 193, 50, 14, IDC_OPEN,    "Open" },
    { WC_BUTTON,   BS_PUSHBUTTON,  167, 193, 50,  14, IDC_UP,      "Up" },
    { WC_BUTTON,   BS_PUSHBUTTON,  243, 193, 50,  14, IDC_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L nn_tmpl = { "Network Neighborhood", 300, 214, nn_items, 5, DS_DLU };

static HWND g_dlg;
static int  g_level;                         /* 0 = computers, 1 = shares of g_host */
static char g_host[64];
static char g_items[64][64]; static int g_n;

static void nn_fill(HWND h) {
    static const char *titles[2]; static const int widths[2] = { 190, 90 };
    titles[0] = g_level ? "Shared Folder" : "Computer"; titles[1] = "Type";
    DlgLVColumns(h, IDC_LIST, titles, widths, 2);
    DlgListReset(h, IDC_LIST);
    g_n = g_level ? sys_net_shares(g_host, g_items, 64) : sys_net_hosts(g_items, 64);
    for (int i = 0; i < g_n; i++) {
        const char *cells[2] = { g_items[i], g_level ? "Shared Folder" : "Computer" };
        DlgLVAddRow(h, IDC_LIST, cells, 2);
    }
    SetDlgItemTextW32(h, IDC_STATUS,
        g_n ? (g_level ? g_host : "Entire Network")
            : (g_level ? "This computer has no shared folders." : "No computers found on the network."));
}

static LRESULT CALLBACK NN_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: g_level = 0; g_host[0] = 0; nn_fill(h); return 0;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_REFRESH) nn_fill(h);
            else if (id == IDC_UP) { if (g_level == 1) { g_level = 0; nn_fill(h); } }
            else if (id == IDC_OPEN) {
                int sel = DlgListGetSel(h, IDC_LIST); if (sel < 0 || sel >= g_n) return 0;
                if (g_level == 0) { int k = 0; for (; g_items[sel][k] && k < 63; k++) g_host[k] = g_items[sel][k]; g_host[k] = 0; g_level = 1; nn_fill(h); }
                else {
                    char mp[200];
                    if (sys_net_mount(g_host, g_items[sel], mp, sizeof mp) == 0) { files_open_at(mp); DestroyWindow(h); }
                    else wm_messagebox("Network Neighborhood", "Could not connect to the shared folder.\nCheck that the computer is on and sharing.");
                }
            }
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void netnbhd_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&nn_tmpl, NN_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 66, "Network Neighborhood", IC_NETWORK, 0, netnbhd_open, -1 };
#endif
