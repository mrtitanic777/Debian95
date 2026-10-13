/* Debian95 — Windows Update: a friendly front-end to apt. Lists the packages that
 * can be upgraded (apt list --upgradable), refreshes the package lists (apt-get
 * update), and installs all upgrades (apt-get upgrade). Declarative dialog-engine
 * UI (WC_LISTVIEW), real apt backend (shims_linux.c). No hand-placed pixels. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

/* real apt backend (shims_linux.c) */
struct lg_update { char name[48]; char ver[48]; };
int sys_update_available(void);
int sys_update_check(struct lg_update *out, int max);
int sys_update_refresh(void);
int sys_update_apply(void);

void wm_messagebox(const char *title, const char *msg);
void wm_confirm(const char *title, const char *text, void (*onyes)(void));

#define MAXU 512
enum { IDC_STATUS = 100, IDC_LIST, IDC_CHECK, IDC_INSTALL };

static const DLGITEM u_items[] = {
    { WC_STATIC,   0,                 7,   8, 300, 10, IDC_STATUS, "Checking for updates..." },
    { WC_LISTVIEW, LBS_NOTIFY,        7,  22, 300, 160, IDC_LIST,  0 },
    { WC_BUTTON,   BS_DEFPUSHBUTTON,  7, 188, 110, 14, IDC_CHECK,  "Check for Updates" },
    { WC_BUTTON,   BS_PUSHBUTTON,   197, 188, 110, 14, IDC_INSTALL,"Install Updates" },
};
static const DLGTEMPLATE_L u_tmpl = { "Windows Update", 314, 208, u_items, 4, DS_DLU };

static HWND g_dlg;
static struct lg_update g_up[MAXU]; static int g_nu;

static void u_fill(HWND h) {
    static const char *const t[2] = { "Package", "New Version" };
    static const int w[2] = { 180, 150 };
    DlgLVColumns(h, IDC_LIST, t, w, 2);
    DlgListReset(h, IDC_LIST);
    g_nu = sys_update_check(g_up, MAXU);
    for (int i = 0; i < g_nu; i++) { const char *c[2] = { g_up[i].name, g_up[i].ver }; DlgLVAddRow(h, IDC_LIST, c, 2); }
    char cap[64];
    if (g_nu == 0) snprintf(cap, sizeof cap, "Your computer is up to date.");
    else snprintf(cap, sizeof cap, "%d update(s) are available for your computer:", g_nu);
    SetDlgItemTextW32(h, IDC_STATUS, cap);
}

static void u_do_install(void) {
    sys_update_apply();
    wm_messagebox("Windows Update",
        "Installing updates in the background.\nThis may take a few minutes; click\n\"Check for Updates\" afterward to confirm.");
}

static LRESULT CALLBACK U_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            if (!sys_update_available()) { SetDlgItemTextW32(h, IDC_STATUS, "Windows Update needs 'apt', which is not installed on this machine."); return 1; }
            u_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_CHECK) {
                sys_update_refresh();                    /* refresh lists in the background */
                u_fill(h);                               /* show what the cached lists hold now */
            } else if (id == IDC_INSTALL) {
                if (g_nu == 0) { wm_messagebox("Windows Update", "There are no updates to install."); return 0; }
                wm_confirm("Windows Update", "Download and install all available updates now?", u_do_install);
            }
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void update_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&u_tmpl, U_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 77, "Windows Update", IC_SETTINGS, 0, update_open, -1 };
#endif
