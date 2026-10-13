/* Debian95 — the Accessibility Control-Panel applet as a Win32 PROPERTY SHEET:
 * four pages of real, persisted options (SETTINGS.INI), with High Contrast applied
 * live. Declared as resource templates + a DlgProc; no hand-placed pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
void wm_set_high_contrast(int on);

enum {
    IDC_STICKY = 400, IDC_FILTER, IDC_TOGGLE,   /* Keyboard */
    IDC_HICON = 410,                            /* Display */
    IDC_MOUSEK = 420,                           /* Mouse */
};

static const DLGITEM p_kbd[] = {
    { WC_BUTTON, BS_GROUPBOX,     8,  6, 236, 30, -1, "StickyKeys" },
    { WC_BUTTON, BS_AUTOCHECKBOX,18, 20, 216, 10, IDC_STICKY, "Use StickyKeys (one modifier key at a time)" },
    { WC_BUTTON, BS_GROUPBOX,     8, 42, 236, 30, -1, "FilterKeys" },
    { WC_BUTTON, BS_AUTOCHECKBOX,18, 56, 216, 10, IDC_FILTER, "Use FilterKeys (ignore brief/repeated keystrokes)" },
    { WC_BUTTON, BS_GROUPBOX,     8, 78, 236, 30, -1, "ToggleKeys" },
    { WC_BUTTON, BS_AUTOCHECKBOX,18, 92, 216, 10, IDC_TOGGLE, "Use ToggleKeys (tones on Caps/Num/Scroll Lock)" },
};
static const DLGTEMPLATE_L t_kbd = { "", 252, 130, p_kbd, 6, DS_DLU };

static const DLGITEM p_snd[] = {
    { WC_STATIC, 0, 14, 20, 224, 9, -1, "SoundSentry and ShowSounds need no audio device." },
};
static const DLGTEMPLATE_L t_snd = { "", 252, 130, p_snd, 1, DS_DLU };

static const DLGITEM p_disp[] = {
    { WC_BUTTON, BS_GROUPBOX,      8,  8, 236, 44, -1, "High Contrast" },
    { WC_BUTTON, BS_AUTOCHECKBOX, 18, 24, 216, 10, IDC_HICON, "Use High Contrast" },
    { WC_STATIC, 0,               30, 40, 210, 9, -1, "Switches the desktop to a black high-contrast scheme." },
};
static const DLGTEMPLATE_L t_disp = { "", 252, 130, p_disp, 3, DS_DLU };

static const DLGITEM p_mouse[] = {
    { WC_BUTTON, BS_GROUPBOX,      8,  8, 236, 30, -1, "MouseKeys" },
    { WC_BUTTON, BS_AUTOCHECKBOX, 18, 22, 216, 10, IDC_MOUSEK, "Move the pointer with the numeric keypad" },
};
static const DLGTEMPLATE_L t_mouse = { "", 252, 130, p_mouse, 2, DS_DLU };

static HWND g_sheet;
static int getb(const char *key) { char v[8]; return (sys_get_setting(key, v, sizeof v) == 0 && v[0] == '1'); }

static LRESULT CALLBACK Acc_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            CheckDlgButton(h, IDC_STICKY, getb("acc_sticky"));
            CheckDlgButton(h, IDC_FILTER, getb("acc_filter"));
            CheckDlgButton(h, IDC_TOGGLE, getb("acc_toggle"));
            CheckDlgButton(h, IDC_HICON,  getb("acc_hc"));
            CheckDlgButton(h, IDC_MOUSEK, getb("acc_mousek"));
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_STICKY: sys_set_setting("acc_sticky", IsDlgButtonChecked(h, IDC_STICKY) ? "1" : "0"); break;
                case IDC_FILTER: sys_set_setting("acc_filter", IsDlgButtonChecked(h, IDC_FILTER) ? "1" : "0"); break;
                case IDC_TOGGLE: sys_set_setting("acc_toggle", IsDlgButtonChecked(h, IDC_TOGGLE) ? "1" : "0"); break;
                case IDC_HICON: { int on = IsDlgButtonChecked(h, IDC_HICON); wm_set_high_contrast(on); sys_set_setting("acc_hc", on ? "1" : "0"); break; }
                case IDC_MOUSEK: sys_set_setting("acc_mousek", IsDlgButtonChecked(h, IDC_MOUSEK) ? "1" : "0"); break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L acc_pages[] = {
    { "Keyboard", &t_kbd,   Acc_DlgProc },
    { "Sound",    &t_snd,   Acc_DlgProc },
    { "Display",  &t_disp,  Acc_DlgProc },
    { "Mouse",    &t_mouse, Acc_DlgProc },
};
static const PROPSHEETHEADER_L acc_psh = { "Accessibility Properties", 252, 130, acc_pages, 4, DS_DLU };

void access_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&acc_psh, startpage); }
