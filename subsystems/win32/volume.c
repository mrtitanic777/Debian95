/* Debian95 — Volume Control (sndvol32), built the Win32 way as a declarative
 * dialog with a real VERTICAL trackbar + a Mute checkbox, wired to the actual
 * mixer (sys_set_volume/sys_set_mute -> wpctl/pactl/amixer). The level persists
 * in SETTINGS.INI. No hand-placed pixels. */
#include <debian95/win32.h>

extern void sys_set_volume(int pct);
extern void sys_set_mute(int on);
extern int  sys_get_setting(const char *key, char *buf, int max);
extern void sys_set_setting(const char *key, const char *val);

enum { IDC_VOL = 1, IDC_VOLTXT, IDC_MUTE };
static const DLGITEM vol_items[] = {
    { WC_STATIC,   0,             8,  6, 50, 9, -1, "Volume" },
    { WC_TRACKBAR, TBS_VERT,     16, 18, 22, 96, IDC_VOL, 0 },
    { WC_STATIC,   0,            44, 56, 24, 9, IDC_VOLTXT, "" },
    { WC_BUTTON,   BS_AUTOCHECKBOX, 8, 120, 56, 10, IDC_MUTE, "Mute" },
};
static const DLGTEMPLATE_L vol_tmpl = { "Volume", 74, 136, vol_items, 4, DS_DLU };
static HWND g_vol;

static void vol_show(HWND h) {
    int v = DlgTrackGetPos(h, IDC_VOL);
    char t[8]; int k = 0; if (v >= 100) t[k++] = '1'; if (v >= 10) t[k++] = (char)('0'+(v/10)%10); t[k++] = (char)('0'+v%10); t[k++] = '%'; t[k] = 0;
    SetDlgItemTextW32(h, IDC_VOLTXT, t);
}

static LRESULT CALLBACK Vol_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            DlgTrackRange(h, IDC_VOL, 0, 100);
            int v = 75; char b[8]; if (sys_get_setting("volume", b, sizeof b) == 0 && b[0]) { v = 0; for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++) v = v*10 + (b[i]-'0'); if (v < 0 || v > 100) v = 75; }
            DlgTrackPos(h, IDC_VOL, v); vol_show(h);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_VOL && HIWORD(wParam) == TBN_CHANGED) {
                int v = DlgTrackGetPos(h, IDC_VOL); sys_set_volume(v);
                char b[8]; int k = 0; int t = v; char tt[8]; int j = 0; if(!t)tt[j++]='0'; while(t){tt[j++]=(char)('0'+t%10);t/=10;} while(j)b[k++]=tt[--j]; b[k]=0;
                sys_set_setting("volume", b); vol_show(h);
            } else if (LOWORD(wParam) == IDC_MUTE) sys_set_mute(IsDlgButtonChecked(h, IDC_MUTE));
            return 0;
        case WM_DESTROY: g_vol = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void volume_open(void) { if (g_vol) return; g_vol = CreateDialogL(&vol_tmpl, Vol_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 25, "Volume Control", IC_APP, 0, volume_open, -1, 0 };
#endif
