/* Debian95 — Windows Firewall: a friendly front-end to ufw (the uncomplicated
 * firewall). Turn protection on/off, see the allow/block rules, and add or remove
 * them. Declarative WC_LISTVIEW dialog + a small Add-Rule dialog. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

struct lg_fwrule { int num; char text[96]; };
int sys_fw_available(void);
int sys_fw_status(void);
int sys_fw_set(int on);
int sys_fw_rules(struct lg_fwrule *out, int max);
int sys_fw_add(const char *spec);
int sys_fw_del(int num);

enum { IDC_STATUS = 100, IDC_TOGGLE, IDC_LBL, IDC_LIST, IDC_ADD, IDC_DEL, IDC_REFRESH };

static const DLGITEM fw_items[] = {
    { WC_STATIC,   0,                 7,   8, 210, 12, IDC_STATUS, "Firewall status: checking..." },
    { WC_BUTTON,   BS_PUSHBUTTON,   224,   6,  69, 14, IDC_TOGGLE, "Turn On" },
    { WC_STATIC,   0,                 7,  26, 286, 10, IDC_LBL,    "Allowed and blocked connections:" },
    { WC_LISTVIEW, LBS_NOTIFY,        7,  38, 286, 140, IDC_LIST,  0 },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 88, 193,  62, 14, IDC_ADD,    "Add Rule..." },
    { WC_BUTTON,   BS_PUSHBUTTON,   155, 193,  55, 14, IDC_DEL,    "Delete" },
    { WC_BUTTON,   BS_PUSHBUTTON,   238, 193,  55, 14, IDC_REFRESH,"Refresh" },
};
static const DLGTEMPLATE_L fw_tmpl = { "Windows Firewall", 300, 214, fw_items, 7, DS_DLU };

static HWND g_dlg;
static struct lg_fwrule g_rules[128]; static int g_nr;

static void fw_fill(HWND h) {
    int on = sys_fw_status();
    SetDlgItemTextW32(h, IDC_STATUS, on ? "Firewall status: ON (blocking unsolicited connections)"
                                        : "Firewall status: OFF");
    SetDlgItemTextW32(h, IDC_TOGGLE, on ? "Turn Off" : "Turn On");
    static const char *const t[1] = { "Rule" };
    static const int w[1] = { 280 };
    DlgLVColumns(h, IDC_LIST, t, w, 1);
    DlgListReset(h, IDC_LIST);
    g_nr = sys_fw_rules(g_rules, 128);
    for (int i = 0; i < g_nr; i++) { const char *c[1] = { g_rules[i].text }; DlgLVAddRow(h, IDC_LIST, c, 1); }
}

/* ---- Add Rule dialog ---- */
enum { IDC_PORT = 200, IDC_ALLOW, IDC_BLOCK };
static const DLGITEM add_items[] = {
    { WC_STATIC, 0,                10, 10, 250, 12, -1, "Port or service (e.g. 22/tcp, 80, ssh, http):" },
    { WC_EDIT,   0,                10, 26, 250, 14, IDC_PORT,  "" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 10, 50,  80, 16, IDC_ALLOW, "Allow" },
    { WC_BUTTON, BS_PUSHBUTTON,   100, 50,  80, 16, IDC_BLOCK, "Block" },
    { WC_BUTTON, BS_PUSHBUTTON,   190, 50,  70, 16, IDCANCEL,  "Cancel" },
};
static const DLGTEMPLATE_L add_tmpl = { "Add Firewall Rule", 270, 78, add_items, 5, DS_DLU };

static LRESULT CALLBACK Add_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_ALLOW || id == IDC_BLOCK) {
                char port[64]; GetDlgItemTextW32(h, IDC_PORT, port, sizeof port);
                if (port[0]) { char spec[96]; snprintf(spec, sizeof spec, "%s %s", id == IDC_ALLOW ? "allow" : "deny", port); sys_fw_add(spec); }
                EndDialogL(h); if (g_dlg) fw_fill(g_dlg);
            } else if (id == IDCANCEL) EndDialogL(h);
            return 0;
        }
        case WM_DESTROY: return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK FW_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            if (!sys_fw_available()) { SetDlgItemTextW32(h, IDC_STATUS, "The firewall (ufw) is not installed on this machine."); return 1; }
            fw_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_TOGGLE) { sys_fw_set(!sys_fw_status()); fw_fill(h); }
            else if (id == IDC_ADD) CreateDialogL(&add_tmpl, Add_DlgProc, 0);
            else if (id == IDC_DEL) { int s = DlgListGetSel(h, IDC_LIST); if (s >= 0 && s < g_nr) { sys_fw_del(g_rules[s].num); fw_fill(h); } }
            else if (id == IDC_REFRESH) fw_fill(h);
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void firewall_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&fw_tmpl, FW_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 74, "Windows Firewall", IC_SETTINGS, 0, firewall_open, -1 };
#endif
