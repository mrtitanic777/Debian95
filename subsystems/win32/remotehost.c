/* Debian95 - Remote Access: host this desktop over VNC so another machine can view
 * and control it (x11vnc on display :0, guarded by a password). Like Windows'
 * "Remote Desktop" settings, but the server side. Declarative dialog; the real
 * work is in sys_vnc_* (shims_linux.c). The boot LUKS logon is untouched. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

int  sys_vnc_available(void);
int  sys_vnc_running(void);
int  sys_vnc_has_password(void);
int  sys_vnc_set_password(const char *pw);
int  sys_vnc_start(void);
void sys_vnc_stop(void);
void sys_vnc_info(char *ip, int iplen, int *port);
void sys_set_setting(const char *key, const char *val);

enum { IDC_ENABLE = 100, IDC_PWLBL, IDC_PW, IDC_APPLY, IDC_STATUS, IDC_ADDR, IDC_HINT, IDC_HINT2 };

static const DLGITEM rh_items[] = {
    { WC_STATIC,  0,              10,  8, 272, 12, -1,
      "Share this desktop with another computer over the network." },
    { WC_BUTTON,  BS_AUTOCHECKBOX, 10, 26, 250, 12, IDC_ENABLE, "Allow remote connections to this computer" },
    { WC_STATIC,  0,              24, 46,  70, 12, IDC_PWLBL,  "Connection password:" },
    { WC_EDIT,    ES_PASSWORD,    96, 44, 120, 14, IDC_PW,     "" },
    { WC_BUTTON,  BS_DEFPUSHBUTTON, 222, 43, 56, 16, IDC_APPLY, "Apply" },
    { WC_STATIC,  0,              10, 68, 272, 12, IDC_STATUS, "Status: checking..." },
    { WC_STATIC,  0,              10, 82, 272, 12, IDC_ADDR,   "" },
    { WC_STATIC,  0,              10, 98, 272, 12, IDC_HINT,
      "On the other machine, open any VNC viewer and connect" },
    { WC_STATIC,  0,              10,110, 272, 12, IDC_HINT2,  "to the address shown above." },
};
static const DLGTEMPLATE_L rh_tmpl = { "Remote Access", 292, 130, rh_items, 9, DS_DLU };

static void rh_refresh(HWND h) {
    char line[160], ip[96]; int port = 5900;
    if (!sys_vnc_available()) {
        SetDlgItemTextW32(h, IDC_STATUS, "Status: the VNC host (x11vnc) is not installed on this machine.");
        SetDlgItemTextW32(h, IDC_ADDR, "");
        return;
    }
    int on = sys_vnc_running();
    SetDlgItemTextW32(h, IDC_STATUS, on ? "Status: ON - this computer is accepting remote connections."
                                        : "Status: off - remote connections are not allowed.");
    CheckDlgButton(h, IDC_ENABLE, on);
    if (on) {
        sys_vnc_info(ip, sizeof ip, &port);
        snprintf(line, sizeof line, "This computer's address:   %s:%d   (VNC display :0)", ip, port);
        SetDlgItemTextW32(h, IDC_ADDR, line);
    } else {
        SetDlgItemTextW32(h, IDC_ADDR, "");
    }
}

/* turn the host on: ensure a password (from the field, or one stored earlier),
 * start x11vnc, persist the preference so it comes back on next boot. */
static void rh_enable(HWND h) {
    char pw[128]; pw[0] = 0;
    GetDlgItemTextW32(h, IDC_PW, pw, sizeof pw);
    if (pw[0]) {
        if (sys_vnc_set_password(pw) != 0) {
            MessageBoxW32(h, "Could not set the connection password.", "Remote Access", MB_ICONERROR);
            return;
        }
    } else if (!sys_vnc_has_password()) {
        MessageBoxW32(h, "Please set a connection password first - it protects this computer from "
                         "anyone on the network.", "Remote Access", MB_ICONWARNING);
        CheckDlgButton(h, IDC_ENABLE, 0);
        return;
    }
    int r = sys_vnc_start();
    if (r != 0) {
        MessageBoxW32(h, "Could not start the remote host. Is x11vnc installed?", "Remote Access", MB_ICONERROR);
        CheckDlgButton(h, IDC_ENABLE, 0);
        return;
    }
    sys_set_setting("vnc_enabled", "1");
    rh_refresh(h);
}

static void rh_disable(HWND h) {
    sys_vnc_stop();
    sys_set_setting("vnc_enabled", "0");
    rh_refresh(h);
}

static LRESULT CALLBACK RH_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: rh_refresh(h); return 1;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_APPLY) {
                if (IsDlgButtonChecked(h, IDC_ENABLE)) rh_enable(h); else rh_disable(h);
            } else if (LOWORD(wParam) == IDC_ENABLE) {
                /* toggling the box takes effect immediately, like Windows' checkbox */
                if (IsDlgButtonChecked(h, IDC_ENABLE)) rh_enable(h); else rh_disable(h);
            }
            return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void remotehost_open(void) { CreateDialogL(&rh_tmpl, RH_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 73, "Remote Access", IC_APP, 0, remotehost_open, -1, 0 };
#endif
