/* Debian95 — the Mouse Control-Panel applet as a Win32 PROPERTY SHEET: button
 * handedness (real: wm_set_swap_buttons) and a double-click-speed TRACKBAR (real:
 * wm_set_dclick_ms), persisted to SETTINGS.INI. Templates + a DlgProc; no pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
void wm_set_swap_buttons(int on);
void wm_set_dclick_ms(int ms);

enum { IDC_RIGHT = 700, IDC_LEFT, IDC_DCLICK, IDC_MOT };

static const DLGITEM p_btn[] = {
    { WC_BUTTON, BS_GROUPBOX,        8,   6, 236, 40, -1, "Button configuration" },
    { WC_BUTTON, BS_AUTORADIOBUTTON,18,  20, 216, 10, IDC_RIGHT, "Right-handed" },
    { WC_BUTTON, BS_AUTORADIOBUTTON,18,  32, 216, 10, IDC_LEFT,  "Left-handed (swap buttons)" },
    { WC_BUTTON, BS_GROUPBOX,        8,  52, 236, 46, -1, "Double-click speed" },
    { WC_STATIC, 0,                 18,  70, 30, 9, -1, "Slow" },
    { WC_STATIC, 0,                210,  70, 30, 9, -1, "Fast" },
    { WC_TRACKBAR, 0,               48,  66, 158, 16, IDC_DCLICK, 0 },
};
static const DLGTEMPLATE_L t_btn = { "", 252, 116, p_btn, 7, DS_DLU };

static const DLGITEM p_mot[] = {
    { WC_STATIC, 0, 14, 20, 224, 9, -1, "Pointer speed and trails are managed by the system." },
};
static const DLGTEMPLATE_L t_mot = { "", 252, 116, p_mot, 1, DS_DLU };

static HWND g_sheet;

static LRESULT CALLBACK Mouse_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            char v[8]; int swap = (sys_get_setting("mouse_swap", v, sizeof v) == 0 && v[0] == '1');
            CheckDlgButton(h, IDC_RIGHT, !swap); CheckDlgButton(h, IDC_LEFT, swap);
            DlgTrackRange(h, IDC_DCLICK, 0, 20);
            int ms = 500; char m[8]; if (sys_get_setting("dclick", m, sizeof m) == 0 && m[0]) { ms = 0; for (int i=0;m[i]>='0'&&m[i]<='9';i++) ms = ms*10 + (m[i]-'0'); }
            int pos = (900 - ms) / 40; if (pos < 0) pos = 0; if (pos > 20) pos = 20;
            DlgTrackPos(h, IDC_DCLICK, pos);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_RIGHT: wm_set_swap_buttons(0); sys_set_setting("mouse_swap", "0"); break;
                case IDC_LEFT:  wm_set_swap_buttons(1); sys_set_setting("mouse_swap", "1"); break;
                case IDC_DCLICK: if (HIWORD(wParam) == TBN_CHANGED) {
                    int ms = 900 - DlgTrackGetPos(h, IDC_DCLICK) * 40; wm_set_dclick_ms(ms);
                    char b[8]; int k=0,v=ms; char t[8]; int j=0; if(!v)t[j++]='0'; while(v){t[j++]=(char)('0'+v%10);v/=10;} while(j)b[k++]=t[--j]; b[k]=0;
                    sys_set_setting("dclick", b);
                } break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L mouse_pages[] = {
    { "Buttons", &t_btn, Mouse_DlgProc },
    { "Motion",  &t_mot, Mouse_DlgProc },
};
static const PROPSHEETHEADER_L mouse_psh = { "Mouse Properties", 252, 116, mouse_pages, 2, DS_DLU };

void mouse_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&mouse_psh, startpage); }
