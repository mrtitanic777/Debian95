/* Debian95 — the Date/Time Control-Panel applet as a Win32 PROPERTY SHEET: a real
 * MONTH-CALENDAR + editable time (sys_set_datetime), a Time Zone note, and an
 * Internet Time (NTP via timedatectl) page. Templates + a DlgProc; no pixels. */
#include <debian95/win32.h>

void sys_date(int *y, int *mo, int *d, int *dow, int *hh, int *mm, int *ss);
int  sys_set_datetime(int y, int mo, int d, int hh, int mm, int ss);
int  sys_time_ntp(int *enabled, int *synced);
int  sys_time_set_ntp(int on);

enum { IDC_CAL = 1200, IDC_TIME, IDC_TZ, IDC_NTP, IDC_NTPMSG, IDC_UPDATE };

static const DLGITEM p_dt[] = {
    { WC_MONTHCAL, 0,  8,  6, 158, 66, IDC_CAL, 0 },
    { WC_STATIC,   0,  8, 78, 92, 9, -1, "Time (HH:MM:SS):" },
    { WC_EDIT,     0,102, 76, 70, 12, IDC_TIME, "" },
};
static const DLGTEMPLATE_L t_dt = { "", 252, 96, p_dt, 3, DS_DLU };

static const DLGITEM p_tz[] = {
    { WC_STATIC, 0, 12, 16, 228, 9, -1, "The system time zone follows /etc/localtime." },
    { WC_STATIC, 0, 12, 30, 228, 9, -1, "Set it with `timedatectl set-timezone <Area/City>`." },
};
static const DLGTEMPLATE_L t_tz = { "", 252, 96, p_tz, 2, DS_DLU };

static const DLGITEM p_ntp[] = {
    { WC_BUTTON, BS_AUTOCHECKBOX, 12, 12, 228, 10, IDC_NTP, "Automatically synchronize with an Internet time server" },
    { WC_STATIC, 0,               30, 30, 210, 9, -1, "Uses systemd-timesyncd against the configured NTP servers." },
    { WC_STATIC, 0,               12, 48, 228, 9, IDC_NTPMSG, "" },
    { WC_BUTTON, BS_PUSHBUTTON,   12, 62, 90, 14, IDC_UPDATE, "Update Now" },
};
static const DLGTEMPLATE_L t_ntp = { "", 252, 96, p_ntp, 4, DS_DLU };

static HWND g_sheet; static int g_loaded;

static LRESULT CALLBACK Dt_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            if (!g_loaded) {
                int y, mo, d, hh, mm, ss; sys_date(&y, &mo, &d, 0, &hh, &mm, &ss);
                DlgCalSet(h, IDC_CAL, y, mo, d);
                char t[12]; int k = 0; t[k++]=(char)('0'+hh/10); t[k++]=(char)('0'+hh%10); t[k++]=':';
                t[k++]=(char)('0'+mm/10); t[k++]=(char)('0'+mm%10); t[k++]=':'; t[k++]=(char)('0'+ss/10); t[k++]=(char)('0'+ss%10); t[k]=0;
                SetDlgItemTextW32(h, IDC_TIME, t); g_loaded = 1;
            }
            int en = 0, sy = 0; int avail = (sys_time_ntp(&en, &sy) == 0);
            CheckDlgButton(h, IDC_NTP, avail && en);
            SetDlgItemTextW32(h, IDC_NTPMSG, !avail ? "Status: time sync unavailable." : !en ? "Status: automatic sync is off." :
                              sy ? "Status: the clock is synchronized." : "Status: sync enabled (not yet in sync).");
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_NTP: sys_time_set_ntp(IsDlgButtonChecked(h, IDC_NTP)); break;
                case IDC_UPDATE: sys_time_set_ntp(1); CheckDlgButton(h, IDC_NTP, 1); SetDlgItemTextW32(h, IDC_NTPMSG, "Status: synchronization requested."); break;
                case IDOK: {   /* Apply/OK: commit the picked date + typed time to the real clock */
                    int y, mo, d; DlgCalGet(h, IDC_CAL, &y, &mo, &d);
                    char t[16]; GetDlgItemTextW32(h, IDC_TIME, t, sizeof t);
                    int hh = 0, mm = 0, ss = 0, f = 0, cur = 0;
                    for (int i = 0; t[i]; i++) { if (t[i] == ':') { if (f==0) hh=cur; else if (f==1) mm=cur; cur=0; f++; } else if (t[i]>='0'&&t[i]<='9') cur=cur*10+(t[i]-'0'); }
                    if (f>=2) ss=cur; else if (f==1) mm=cur; else hh=cur;
                    sys_set_datetime(y, mo, d, hh, mm, ss);
                    break;
                }
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; g_loaded = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L dt_pages[] = {
    { "Date & Time",   &t_dt,  Dt_DlgProc },
    { "Time Zone",     &t_tz,  Dt_DlgProc },
    { "Internet Time", &t_ntp, Dt_DlgProc },
};
static const PROPSHEETHEADER_L dt_psh = { "Date/Time Properties", 252, 96, dt_pages, 3, DS_DLU };

void datetime_sheet_open(int startpage) { if (g_sheet) return; g_loaded = 0; g_sheet = PropertySheetL(&dt_psh, startpage); }
