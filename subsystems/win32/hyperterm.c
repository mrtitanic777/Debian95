/* Debian95 — HyperTerminal: the Win95 comms app, reborn as a real SSH client. Enter
 * a host / user / port and Connect; it opens an interactive SSH session in the
 * built-in terminal. Declarative dialog + a DlgProc. */
#include <debian95/win32.h>

void terminal_run(const char *cmd);          /* terminal_linux.c: run a command in the built-in terminal */

enum { IDC_HOST = 100, IDC_USER, IDC_PORT, IDC_CONNECT };

static const DLGITEM ht_items[] = {
    { WC_STATIC, 0,                10, 12,  70, 12, -1, "Host name:" },
    { WC_EDIT,   0,                86, 10, 150, 14, IDC_HOST, "" },
    { WC_STATIC, 0,                10, 34,  70, 12, -1, "User name:" },
    { WC_EDIT,   0,                86, 32, 150, 14, IDC_USER, "" },
    { WC_STATIC, 0,                10, 56,  70, 12, -1, "Port:" },
    { WC_EDIT,   0,                86, 54,  48, 14, IDC_PORT, "22" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 86, 78,  72, 16, IDC_CONNECT, "Connect" },
    { WC_BUTTON, BS_PUSHBUTTON,   164, 78,  72, 16, IDCANCEL,    "Cancel" },
};
static const DLGTEMPLATE_L ht_tmpl = { "HyperTerminal", 248, 104, ht_items, 8, DS_DLU };

static int clean(const char *s, char *out, int n) {   /* allow only safe shell-arg chars */
    int k = 0; for (int i = 0; s[i] && k < n - 1; i++) {
        char c = s[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||c=='@') out[k++] = c;
    }
    out[k] = 0; return k;
}

static LRESULT CALLBACK HT_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_CONNECT) {
                char host[128], user[64], port[8];
                char rh[128], ru[64], rp[8];
                GetDlgItemTextW32(h, IDC_HOST, rh, sizeof rh);
                GetDlgItemTextW32(h, IDC_USER, ru, sizeof ru);
                GetDlgItemTextW32(h, IDC_PORT, rp, sizeof rp);
                if (!clean(rh, host, sizeof host)) return 0;                 /* need a host */
                clean(ru, user, sizeof user); clean(rp, port, sizeof port);
                char cmd[320]; int k = 0; const char *p;
                for (p = "ssh "; *p; p++) cmd[k++] = *p;
                if (port[0] && !(port[0]=='2' && port[1]=='2' && !port[2])) {   /* non-default port */
                    for (p = "-p "; *p; p++) cmd[k++] = *p;
                    for (p = port; *p; p++) cmd[k++] = *p;
                    cmd[k++] = ' ';
                }
                if (user[0]) { for (p = user; *p; p++) cmd[k++] = *p; cmd[k++] = '@'; }
                for (p = host; *p; p++) cmd[k++] = *p;
                cmd[k] = 0;
                terminal_run(cmd);
                EndDialogL(h);
            } else if (LOWORD(wParam) == IDCANCEL) EndDialogL(h);
            return 0;
        case WM_DESTROY: return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void hyperterm_open(void) { CreateDialogL(&ht_tmpl, HT_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 67, "HyperTerminal", IC_APP, 0, hyperterm_open, -1 };
#endif
