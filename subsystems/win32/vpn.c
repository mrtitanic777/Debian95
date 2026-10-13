/* Debian95 — Dial-Up Networking: connect to your VPNs through NetworkManager.
 * Lists the configured VPN connections (with live up/down state), connects or
 * disconnects one, and imports an OpenVPN .ovpn profile. Declarative dialog-engine
 * UI (WC_LISTVIEW), real nmcli backend (shims_linux.c). No hand-placed pixels. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

/* real nmcli VPN backend (shims_linux.c) */
struct lg_vpn { char name[64]; char type[16]; int up; };
int sys_vpn_available(void);
int sys_vpn_list(struct lg_vpn *out, int max);
int sys_vpn_up(const char *name);
int sys_vpn_down(const char *name);
int sys_vpn_import(const char *path);

void wm_messagebox(const char *title, const char *msg);

#define MAXV 64
enum { IDC_INTRO = 100, IDC_LIST, IDC_CONNECT, IDC_DISC, IDC_IMPORT, IDC_REFRESH };

static const DLGITEM v_items[] = {
    { WC_STATIC,   0,                 7,   7, 300, 10, IDC_INTRO,  "Make or manage a connection to a private network:" },
    { WC_LISTVIEW, LBS_NOTIFY,        7,  20, 300, 150, IDC_LIST,  0 },
    { WC_BUTTON,   BS_DEFPUSHBUTTON,  7, 176,  62, 14, IDC_CONNECT,"Connect" },
    { WC_BUTTON,   BS_PUSHBUTTON,    75, 176,  70, 14, IDC_DISC,   "Disconnect" },
    { WC_BUTTON,   BS_PUSHBUTTON,   151, 176,  94, 14, IDC_IMPORT, "Import .ovpn..." },
    { WC_BUTTON,   BS_PUSHBUTTON,   251, 176,  56, 14, IDC_REFRESH,"Refresh" },
};
static const DLGTEMPLATE_L v_tmpl = { "Dial-Up Networking", 314, 196, v_items, 6, DS_DLU };

static HWND g_dlg;
static struct lg_vpn g_v[MAXV]; static int g_nv;

static void v_fill(HWND h) {
    static const char *const t[3] = { "Connection", "Type", "Status" };
    static const int w[3] = { 170, 70, 80 };
    DlgLVColumns(h, IDC_LIST, t, w, 3);
    DlgListReset(h, IDC_LIST);
    g_nv = sys_vpn_list(g_v, MAXV);
    for (int i = 0; i < g_nv; i++) {
        const char *c[3] = { g_v[i].name, g_v[i].type, g_v[i].up ? "Connected" : "Disconnected" };
        DlgLVAddRow(h, IDC_LIST, c, 3);
    }
    if (g_nv == 0) SetDlgItemTextW32(h, IDC_INTRO, "No VPN connections yet. Use \"Import .ovpn...\" to add one.");
}

static void v_on_import(const char *path) {
    int r = sys_vpn_import(path);
    if (g_dlg) v_fill(g_dlg);
    wm_messagebox("Dial-Up Networking", r == 0
        ? "The VPN profile was imported.\nSelect it and click Connect."
        : "Could not import the profile.\nIs 'network-manager-openvpn' installed?");
}

static LRESULT CALLBACK V_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            if (!sys_vpn_available()) { SetDlgItemTextW32(h, IDC_INTRO, "Networking needs NetworkManager (nmcli), which is not installed."); return 1; }
            v_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_IMPORT) {
                static OPENFILENAME ofn; memset(&ofn, 0, sizeof ofn);
                ofn.title = "Import OpenVPN Profile"; ofn.filter = "*.OVPN;*.CONF"; ofn.onok = v_on_import;
                GetOpenFileName(&ofn);
            } else if (id == IDC_REFRESH) v_fill(h);
            else if (id == IDC_CONNECT || id == IDC_DISC) {
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0 || s >= g_nv) { wm_messagebox("Dial-Up Networking", "Select a connection first."); return 0; }
                int r = (id == IDC_CONNECT) ? sys_vpn_up(g_v[s].name) : sys_vpn_down(g_v[s].name);
                v_fill(h);
                if (r != 0) wm_messagebox("Dial-Up Networking", id == IDC_CONNECT
                    ? "Could not connect.\nCheck the profile and your credentials."
                    : "Could not disconnect.");
            }
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void vpn_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&v_tmpl, V_DlgProc, 0); }
