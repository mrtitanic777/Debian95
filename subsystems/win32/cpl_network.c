/* Debian95 — the Network Control-Panel applet as a Win32 PROPERTY SHEET: four
 * pages (Configuration / Identification / Access Control / Wireless) declared as
 * resource templates and driven by a DlgProc over the real system — the adapter
 * and IP (sys_net_info), the hostname/workgroup, and live Wi-Fi scan + WPA join
 * (nmcli via sys_wifi_*). No hand-placed pixels. */
#include <debian95/win32.h>

int  sys_net_info(char *host, int hn, char *ip, int in, char *dev, int dn);
int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
struct lg_wifi { char ssid[40]; int signal; int secure; int active; };
int  sys_wifi_scan(struct lg_wifi *out, int max);
int  sys_wifi_connect(const char *ssid, const char *pass);
int  sys_wifi_active(char *out, int n);

enum {
    IDC_CFG_ADAPTER = 300, IDC_CFG_IP, IDC_CFG_STATUS,             /* Configuration */
    IDC_ID_HOST = 310, IDC_ID_WG, IDC_ID_DESC,                    /* Identification */
    IDC_ACL_SHARE = 320, IDC_ACL_USER,                            /* Access Control */
    IDC_WIFI_LIST = 330, IDC_WIFI_PASS, IDC_WIFI_SCAN, IDC_WIFI_CONNECT, IDC_WIFI_MSG,  /* Wireless */
};

static const DLGITEM p_cfg[] = {
    { WC_STATIC, 0,  8,  6, 236, 9, -1, "The following network components are installed:" },
    { WC_STATIC, 0, 14, 24, 236, 9, -1, "Client for Microsoft Networks" },
    { WC_STATIC, 0, 14, 38, 236, 9, IDC_CFG_ADAPTER, "Adapter:" },
    { WC_STATIC, 0, 14, 52, 236, 9, -1, "TCP/IP" },
    { WC_STATIC, 0, 14, 66, 236, 9, IDC_CFG_IP, "IP Address:" },
    { WC_STATIC, 0,  8, 92, 236, 9, IDC_CFG_STATUS, "" },
};
static const DLGTEMPLATE_L t_cfg = { "", 252, 140, p_cfg, 6, DS_DLU };

static const DLGITEM p_id[] = {
    { WC_STATIC, 0,  8,  8, 236, 9, -1, "This computer is identified on the network as:" },
    { WC_STATIC, 0, 14, 32, 64, 9, -1, "Computer name:" },
    { WC_EDIT,   0, 90, 30, 150, 12, IDC_ID_HOST, "" },
    { WC_STATIC, 0, 14, 52, 64, 9, -1, "Workgroup:" },
    { WC_EDIT,   0, 90, 50, 150, 12, IDC_ID_WG, "" },
    { WC_STATIC, 0, 14, 72, 64, 9, -1, "Description:" },
    { WC_EDIT,   0, 90, 70, 150, 12, IDC_ID_DESC, "Debian95 Workstation" },
};
static const DLGTEMPLATE_L t_id = { "", 252, 140, p_id, 7, DS_DLU };

static const DLGITEM p_acl[] = {
    { WC_BUTTON, BS_GROUPBOX,        8,  8, 236, 56, -1, "Control access to shared resources using:" },
    { WC_BUTTON, BS_AUTORADIOBUTTON, 18, 26, 216, 10, IDC_ACL_SHARE, "Share-level access control" },
    { WC_BUTTON, BS_AUTORADIOBUTTON, 18, 44, 216, 10, IDC_ACL_USER,  "User-level access control" },
};
static const DLGTEMPLATE_L t_acl = { "", 252, 140, p_acl, 3, DS_DLU };

static const DLGITEM p_wifi[] = {
    { WC_STATIC,  0,               8,   6, 236, 9, -1, "Available wireless networks:" },
    { WC_LISTBOX, LBS_NOTIFY,      8,  18, 236, 60, IDC_WIFI_LIST, 0 },
    { WC_STATIC,  0,               8,  84,  70, 9, -1, "Password (WPA):" },
    { WC_EDIT,    ES_PASSWORD,    78,  82, 100, 12, IDC_WIFI_PASS, "" },
    { WC_BUTTON,  BS_PUSHBUTTON,  182, 82,  28, 12, IDC_WIFI_SCAN, "Scan" },
    { WC_BUTTON,  BS_DEFPUSHBUTTON,214, 82, 30, 12, IDC_WIFI_CONNECT, "Join" },
    { WC_STATIC,  0,               8, 102, 236, 9, IDC_WIFI_MSG, "" },
};
static const DLGTEMPLATE_L t_wifi = { "", 252, 140, p_wifi, 7, DS_DLU };

static HWND g_sheet;
static struct lg_wifi g_wifi[24]; static int g_nwifi;

static void cfg_fill(HWND h) {
    char host[64] = "", ip[24] = "", dev[24] = "";
    int have = sys_net_info(host, sizeof host, ip, sizeof ip, dev, sizeof dev);
    char a[64] = "Adapter: "; int k = 9; const char *d = dev[0] ? dev : "(none)"; for (int i = 0; d[i] && k < 63; i++) a[k++] = d[i]; a[k] = 0;
    SetDlgItemTextW32(h, IDC_CFG_ADAPTER, a);
    char b[64] = "IP Address: "; k = 12; d = ip[0] ? ip : "(not connected)"; for (int i = 0; d[i] && k < 63; i++) b[k++] = d[i]; b[k] = 0;
    SetDlgItemTextW32(h, IDC_CFG_IP, b);
    SetDlgItemTextW32(h, IDC_CFG_STATUS, have == 0 ? "Status: connected" : "Status: no active adapter");
    if (host[0]) SetDlgItemTextW32(h, IDC_ID_HOST, host);
}
static void wifi_fill(HWND h) {
    g_nwifi = sys_wifi_scan(g_wifi, 24);
    DlgListReset(h, IDC_WIFI_LIST);
    for (int i = 0; i < g_nwifi; i++) {
        char line[64]; int k = 0;
        if (g_wifi[i].active) { line[k++] = '*'; line[k++] = ' '; }
        for (int j = 0; g_wifi[i].ssid[j] && k < 40; j++) line[k++] = g_wifi[i].ssid[j];
        line[k++] = ' '; line[k++] = '('; int s = g_wifi[i].signal; if (s < 0) s = 0; if (s > 100) s = 100;
        if (s >= 100) line[k++] = '1'; if (s >= 10) line[k++] = (char)('0'+(s/10)%10); line[k++] = (char)('0'+s%10); line[k++] = '%'; line[k++] = ')';
        if (g_wifi[i].secure) { const char *lk = " [key]"; for (int j = 0; lk[j]; j++) line[k++] = lk[j]; }
        line[k] = 0;
        DlgListAdd(h, IDC_WIFI_LIST, line);
    }
    char cur[40];
    SetDlgItemTextW32(h, IDC_WIFI_MSG, sys_wifi_active(cur, sizeof cur) ? "Connected." :
                      (g_nwifi ? "Select a network and Join." : "No wireless networks found."));
}

static LRESULT CALLBACK Net_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            cfg_fill(h);
            char wg[32] = "WORKGROUP"; if (sys_get_setting("workgroup", wg, sizeof wg) != 0 || !wg[0]) { wg[0]='W';wg[1]='O';wg[2]='R';wg[3]='K';wg[4]='G';wg[5]='R';wg[6]='O';wg[7]='U';wg[8]='P';wg[9]=0; }
            SetDlgItemTextW32(h, IDC_ID_WG, wg);
            char v[8]; int acl = 0; if (sys_get_setting("net_acl", v, sizeof v) == 0 && v[0]) acl = (v[0] != '0');
            CheckDlgButton(h, IDC_ACL_SHARE, !acl); CheckDlgButton(h, IDC_ACL_USER, acl);
            wifi_fill(h);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_ACL_SHARE: sys_set_setting("net_acl", "0"); break;
                case IDC_ACL_USER:  sys_set_setting("net_acl", "1"); break;
                case IDC_WIFI_SCAN: wifi_fill(h); break;
                case IDC_WIFI_CONNECT: {
                    int sel = DlgListGetSel(h, IDC_WIFI_LIST);
                    if (sel < 0 || sel >= g_nwifi) { SetDlgItemTextW32(h, IDC_WIFI_MSG, "Select a network first."); break; }
                    char pass[64]; GetDlgItemTextW32(h, IDC_WIFI_PASS, pass, sizeof pass);
                    int rc = sys_wifi_connect(g_wifi[sel].ssid, pass[0] ? pass : (const char *)0);
                    SetDlgItemTextW32(h, IDC_WIFI_MSG, rc == 0 ? "Connected." : "Could not connect.");
                    SetDlgItemTextW32(h, IDC_WIFI_PASS, "");
                    if (rc == 0) wifi_fill(h);
                    break;
                }
                case IDOK: {   /* Apply: persist the workgroup name */
                    char wg[32]; GetDlgItemTextW32(h, IDC_ID_WG, wg, sizeof wg); if (wg[0]) sys_set_setting("workgroup", wg);
                    break;
                }
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L net_pages[] = {
    { "Configuration",  &t_cfg,  Net_DlgProc },
    { "Identification", &t_id,   Net_DlgProc },
    { "Access Control", &t_acl,  Net_DlgProc },
    { "Wireless",       &t_wifi, Net_DlgProc },
};
static const PROPSHEETHEADER_L net_psh = { "Network", 252, 140, net_pages, 4, DS_DLU };

void network_sheet_open(int startpage) {
    if (g_sheet) return;
    g_sheet = PropertySheetL(&net_psh, startpage);
}
