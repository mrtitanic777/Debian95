/* Debian95 — Device Manager: a Win95-style collapsible tree of the REAL hardware
 * (PCI/USB devices, processors, disks via sys_device_list -> lspci/lsusb/cpuinfo/
 * lsblk), grouped into the familiar categories. Built the resource-template way
 * (WC_TREEVIEW + a DlgProc); the dialog engine owns layout, paint, expand/collapse
 * and selection. Doubles as a hardware-readiness dashboard for a real-metal boot. */
#include <debian95/win32.h>

struct lg_device { int cat; char name[96]; };
int sys_device_list(struct lg_device *out, int max);
int sys_net_info(char *host, int hn, char *ip, int in, char *dev, int dn);

/* category names - index MUST match the DC_* ids in shims_linux.c */
static const char *DEVCAT[] = {
    "Disk drives", "Display adapters", "Sound, video and game controllers",
    "Network adapters", "Bluetooth radios", "Processors",
    "Universal Serial Bus controllers", "System devices",
};
#define NCAT ((int)(sizeof DEVCAT / sizeof DEVCAT[0]))

enum { IDC_TREE = 100, IDC_REFRESH };

static const DLGITEM dm_items[] = {
    { WC_TREEVIEW, 0,              7,   7, 286, 182, IDC_TREE,    0 },
    { WC_BUTTON,   BS_PUSHBUTTON, 243, 194, 50, 14,  IDC_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L dm_tmpl = { "Device Manager", 300, 214, dm_items, 2, DS_DLU };

static HWND g_dlg;
static struct lg_device g_dev[256]; static int g_ndev;

static void dm_fill(HWND h) {
    g_ndev = sys_device_list(g_dev, 256);
    DlgTreeReset(h, IDC_TREE);
    char host[48] = "", ip[40], dev[32];
    if (sys_net_info(host, sizeof host, ip, sizeof ip, dev, sizeof dev) != 0 || !host[0])
        { const char *d = "Debian95"; int k = 0; for (; d[k]; k++) host[k] = d[k]; host[k] = 0; }
    int root = DlgTreeAdd(h, IDC_TREE, host, 0, 1);
    DlgTreeExpand(h, IDC_TREE, root, 1);
    for (int c = 0; c < NCAT; c++) {
        int cnt = 0; for (int i = 0; i < g_ndev; i++) if (g_dev[i].cat == c) cnt++;
        if (!cnt) continue;
        int cat = DlgTreeAdd(h, IDC_TREE, DEVCAT[c], 1, 1);
        DlgTreeExpand(h, IDC_TREE, cat, 1);
        for (int i = 0; i < g_ndev; i++) if (g_dev[i].cat == c) DlgTreeAdd(h, IDC_TREE, g_dev[i].name, 2, 0);
    }
}

static LRESULT CALLBACK DM_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: dm_fill(h); return 0;
        case WM_COMMAND: if (LOWORD(wParam) == IDC_REFRESH) dm_fill(h); return 0;
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void devmgr_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&dm_tmpl, DM_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 64, "Device Manager", IC_MYCOMPUTER, 0, devmgr_open, -1 };
#endif
