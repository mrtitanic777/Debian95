/* Debian95 — the Joystick and Modem Control-Panel applets as Win32 PROPERTY
 * SHEETS with honest, real hardware status (sys_has_joystick / sys_has_modem).
 * Templates + DlgProcs; no hand-placed pixels. */
#include <debian95/win32.h>

int sys_has_joystick(void);
int sys_has_modem(char *dev, int n);

/* ---------- Joystick ---------- */
enum { IDC_JOY_A = 900, IDC_JOY_B, IDC_JOY_REFRESH };
static const DLGITEM p_joy[] = {
    { WC_BUTTON, BS_GROUPBOX,   8,  8, 236, 60, -1, "Game Controllers" },
    { WC_STATIC, 0,            18, 26, 216, 9, IDC_JOY_A, "" },
    { WC_STATIC, 0,            18, 42, 216, 9, IDC_JOY_B, "" },
    { WC_BUTTON, BS_PUSHBUTTON,170, 74, 66, 14, IDC_JOY_REFRESH, "Refresh" },
};
static const DLGTEMPLATE_L t_joy = { "", 252, 100, p_joy, 4, DS_DLU };
static HWND g_joy;
static void joy_fill(HWND h) {
    int have = sys_has_joystick();
    SetDlgItemTextW32(h, IDC_JOY_A, have ? "Controller 1:  Detected (Ready)" : "No game controllers are installed.");
    SetDlgItemTextW32(h, IDC_JOY_B, have ? "Status:  OK" : "Connect a controller, then click Refresh.");
}
static LRESULT CALLBACK Joy_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: joy_fill(h); return 0;
        case WM_COMMAND: if (LOWORD(wParam) == IDC_JOY_REFRESH) joy_fill(h); return 0;
        case WM_DESTROY: g_joy = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
static const PROPSHEETPAGE_L joy_pages[] = { { "Game Controllers", &t_joy, Joy_DlgProc } };
static const PROPSHEETHEADER_L joy_psh = { "Game Controllers", 252, 100, joy_pages, 1, DS_DLU };
void joystick_sheet_open(int startpage) { if (g_joy) return; g_joy = PropertySheetL(&joy_psh, startpage); }

/* ---------- Modem ---------- */
enum { IDC_MDM_STATUS = 910, IDC_MDM_DIAG };
static const DLGITEM p_mdm_gen[] = {
    { WC_BUTTON, BS_GROUPBOX,   8,  8, 236, 56, -1, "The following modems are set up:" },
    { WC_STATIC, 0,            18, 28, 216, 9, IDC_MDM_STATUS, "" },
    { WC_BUTTON, BS_PUSHBUTTON, 96, 74, 66, 14, -1, "Add..." },
    { WC_BUTTON, BS_PUSHBUTTON,170, 74, 66, 14, -1, "Remove" },
};
static const DLGTEMPLATE_L t_mdm_gen = { "", 252, 100, p_mdm_gen, 4, DS_DLU };
static const DLGITEM p_mdm_diag[] = {
    { WC_STATIC, 0, 12, 16, 228, 9, IDC_MDM_DIAG, "" },
};
static const DLGTEMPLATE_L t_mdm_diag = { "", 252, 100, p_mdm_diag, 1, DS_DLU };
static HWND g_mdm;
static LRESULT CALLBACK Mdm_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            char dev[24]; int have = sys_has_modem(dev, sizeof dev);
            if (have) { char l[48] = "Modem on "; int k = 9; for (int i = 0; dev[i] && k < 47; i++) l[k++] = dev[i]; l[k] = 0; SetDlgItemTextW32(h, IDC_MDM_STATUS, l); }
            else SetDlgItemTextW32(h, IDC_MDM_STATUS, "No modem is installed.");
            SetDlgItemTextW32(h, IDC_MDM_DIAG, have ? "Driver installed; click More Info to query." : "No modem to diagnose.");
            return 0;
        }
        case WM_DESTROY: g_mdm = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
static const PROPSHEETPAGE_L mdm_pages[] = {
    { "General",     &t_mdm_gen,  Mdm_DlgProc },
    { "Diagnostics", &t_mdm_diag, Mdm_DlgProc },
};
static const PROPSHEETHEADER_L mdm_psh = { "Modems Properties", 252, 100, mdm_pages, 2, DS_DLU };
void modem_sheet_open(int startpage) { if (g_mdm) return; g_mdm = PropertySheetL(&mdm_psh, startpage); }
