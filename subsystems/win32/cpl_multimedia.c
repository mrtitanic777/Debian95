/* Debian95 — the Multimedia Control-Panel applet (MMSYS.CPL) as a Win32 PROPERTY
 * SHEET. The "Audio" tab drives the REAL audio stack: a "Preferred device"
 * combo that enumerates the live PulseAudio/PipeWire sinks and switches the
 * default output, a master-volume trackbar on the real mixer, and the taskbar
 * volume-control toggle. Templates + a DlgProc; no hand-placed pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
int  sys_audio_outputs(char descs[][96], char ids[][128], int max);
int  sys_audio_current(char *id, int n);
void sys_audio_set_output(const char *id);
void sys_set_volume(int pct);

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

enum { IDC_DEV = 1000, IDC_VOL, IDC_TRAY };

#define MAXDEV 16
static char g_desc[MAXDEV][96];
static char g_id[MAXDEV][128];
static int  g_ndev;

static const DLGITEM p_audio[] = {
    { WC_BUTTON,   BS_GROUPBOX,    8,  4, 236, 70, -1, "Playback" },
    { WC_STATIC,   0,             18, 22,  72,  9, -1, "Preferred device:" },
    { WC_COMBO,    0,             18, 33, 218, 14, IDC_DEV,  0 },
    { WC_STATIC,   0,             18, 52,  40,  9, -1, "Volume:" },
    { WC_TRACKBAR, 0,             58, 50, 178, 14, IDC_VOL,  0 },
    { WC_BUTTON,   BS_AUTOCHECKBOX, 8, 86, 236, 11, IDC_TRAY, "Show volume control on the taskbar" },
};
static const DLGTEMPLATE_L t_audio = { "", 252, 116, p_audio, 6, DS_DLU };

static HWND g_sheet;

static int cur_volume(void) {
    char v[8]; if (sys_get_setting("volume", v, sizeof v) == 0 && v[0]) {
        int n = 0; for (char *p = v; *p >= '0' && *p <= '9'; p++) n = n*10 + (*p - '0'); if (n >= 0 && n <= 100) return n;
    }
    return 75;
}

static LRESULT CALLBACK MM_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            g_ndev = sys_audio_outputs(g_desc, g_id, MAXDEV);
            if (g_ndev <= 0) {
                DlgListAdd(h, IDC_DEV, "(none)");
                DlgListSetSel(h, IDC_DEV, 0);
            } else {
                char curid[128]; int sel = 0;
                int have = sys_audio_current(curid, sizeof curid) == 0;
                for (int i = 0; i < g_ndev; i++) { DlgListAdd(h, IDC_DEV, g_desc[i]); if (have && streq(curid, g_id[i])) sel = i; }
                DlgListSetSel(h, IDC_DEV, sel);
            }
            DlgTrackRange(h, IDC_VOL, 0, 100);
            DlgTrackPos(h, IDC_VOL, cur_volume());
            char tv[8]; int tray = 1;
            if (sys_get_setting("tray_volume", tv, sizeof tv) == 0 && tv[0] == '0') tray = 0;
            CheckDlgButton(h, IDC_TRAY, tray);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_DEV: if (HIWORD(wParam) == CBN_SELCHANGE) {
                    int s = DlgListGetSel(h, IDC_DEV);
                    if (s >= 0 && s < g_ndev) sys_audio_set_output(g_id[s]);
                } break;
                case IDC_VOL: if (HIWORD(wParam) == TBN_CHANGED) {
                    int v = DlgTrackGetPos(h, IDC_VOL); sys_set_volume(v);
                    char b[8]; int k = 0, t = v; char tmp[8]; if (!t) tmp[k++]='0'; while (t){tmp[k++]=(char)('0'+t%10);t/=10;}
                    for (int i = 0; i < k; i++) b[i] = tmp[k-1-i];
                    b[k] = 0; sys_set_setting("volume", b);
                } break;
                case IDC_TRAY: sys_set_setting("tray_volume", IsDlgButtonChecked(h, IDC_TRAY) ? "1" : "0"); break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L mm_pages[] = { { "Audio", &t_audio, MM_DlgProc } };
static const PROPSHEETHEADER_L mm_psh = { "Multimedia Properties", 252, 116, mm_pages, 1, DS_DLU };

void multimedia_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&mm_psh, startpage); }
