/* Debian95 — the Bluetooth Control-Panel applet as a Win32 PROPERTY SHEET.
 * Drives the REAL BlueZ stack through bluetoothctl: an adapter on/off toggle, a
 * listview of known/discovered devices with their connection state, and
 * Scan / Connect / Disconnect buttons. Templates + a DlgProc; no hand-placed
 * pixels. Shows "no adapter" cleanly on a machine (or VM) without Bluetooth. */
#include <debian95/win32.h>

int  sys_bt_available(void);
int  sys_bt_powered(void);
void sys_bt_power(int on);
void sys_bt_scan(int on);
int  sys_bt_list(char names[][64], char macs[][20], int *connected, int max);
void sys_bt_connect(const char *mac);
void sys_bt_disconnect(const char *mac);

enum { IDC_PWR = 1000, IDC_LIST, IDC_SCAN, IDC_CONN, IDC_DISC, IDC_REFRESH };

#define MAXBT 24
static char g_name[MAXBT][64];
static char g_mac[MAXBT][20];
static int  g_conn[MAXBT];
static int  g_nbt;

static const DLGITEM p_bt[] = {
    { WC_BUTTON,   BS_AUTOCHECKBOX, 8,  4, 160, 11, IDC_PWR,  "Turn on Bluetooth" },
    { WC_STATIC,   0,               8, 18, 236,  9, -1, "Devices:" },
    { WC_LISTVIEW, LBS_NOTIFY,      8, 28, 236, 58, IDC_LIST, 0 },
    { WC_BUTTON,   BS_PUSHBUTTON,   8, 90, 52, 14, IDC_SCAN,  "Scan" },
    { WC_BUTTON,   BS_PUSHBUTTON,  70, 90, 60, 14, IDC_CONN,  "Connect" },
    { WC_BUTTON,   BS_PUSHBUTTON, 138, 90, 72, 14, IDC_DISC,  "Disconnect" },
};
static const DLGTEMPLATE_L t_bt = { "", 252, 116, p_bt, 6, DS_DLU };

static HWND g_sheet;

static void bt_fill(HWND h) {
    DlgListReset(h, IDC_LIST);
    if (!sys_bt_available()) {
        const char *cells[2] = { "(no Bluetooth adapter)", "" };
        DlgLVAddRow(h, IDC_LIST, cells, 2); g_nbt = 0; return;
    }
    g_nbt = sys_bt_list(g_name, g_mac, g_conn, MAXBT);
    if (g_nbt == 0) { const char *cells[2] = { "(no devices - click Scan)", "" }; DlgLVAddRow(h, IDC_LIST, cells, 2); return; }
    for (int i = 0; i < g_nbt; i++) {
        const char *cells[2] = { g_name[i], g_conn[i] ? "Connected" : "Paired" };
        DlgLVAddRow(h, IDC_LIST, cells, 2);
    }
}

static LRESULT CALLBACK BT_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            static const char *const titles[2] = { "Device", "Status" };
            static const int widths[2] = { 150, 80 };
            DlgLVColumns(h, IDC_LIST, titles, widths, 2);
            CheckDlgButton(h, IDC_PWR, sys_bt_powered());
            bt_fill(h);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_PWR:  sys_bt_power(IsDlgButtonChecked(h, IDC_PWR)); break;
                case IDC_SCAN: sys_bt_scan(1); bt_fill(h); break;               /* results arrive async; re-Scan to refresh */
                case IDC_CONN: { int s = DlgListGetSel(h, IDC_LIST); if (s >= 0 && s < g_nbt) { sys_bt_connect(g_mac[s]); bt_fill(h); } } break;
                case IDC_DISC: { int s = DlgListGetSel(h, IDC_LIST); if (s >= 0 && s < g_nbt) { sys_bt_disconnect(g_mac[s]); bt_fill(h); } } break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L bt_pages[] = { { "Devices", &t_bt, BT_DlgProc } };
static const PROPSHEETHEADER_L bt_psh = { "Bluetooth Properties", 252, 116, bt_pages, 1, DS_DLU };

void bluetooth_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&bt_psh, startpage); }
