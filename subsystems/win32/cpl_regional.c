/* Debian95 — the Regional Settings Control-Panel applet as a Win32 PROPERTY SHEET:
 * a locale listbox and 12/24-hour + short-date COMBO, over the real persisted
 * settings (locale/time_24h/date_fmt) and the live clock (wm_set_time_24h).
 * Templates + a DlgProc; no hand-placed pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
void wm_set_time_24h(int on);

struct Locale { const char *name; int t24; int datefmt; };
static const struct Locale g_loc[] = {
    { "English (United States)", 0, 0 },
    { "English (United Kingdom)",0, 1 },
    { "German (Germany)",        1, 1 },
    { "French (France)",         1, 1 },
    { "ISO 8601 (Universal)",    1, 2 },
};
#define NLOC ((int)(sizeof g_loc / sizeof g_loc[0]))
static const char *g_datefmt_name[] = { "M/d/yy", "d/M/yy", "yyyy-MM-dd" };

enum { IDC_LOC = 1100, IDC_SAMP, IDC_T12, IDC_T24, IDC_DATEFMT, IDC_DSAMP };

static const DLGITEM p_reg[] = {
    { WC_STATIC,  0,          8,  8, 236, 9, -1, "Select the locale for numbers, currency, time and date:" },
    { WC_LISTBOX, LBS_NOTIFY, 8, 20, 236, 66, IDC_LOC, 0 },
    { WC_STATIC,  0,          8, 92, 236, 9, IDC_SAMP, "" },
};
static const DLGTEMPLATE_L t_reg = { "", 252, 116, p_reg, 3, DS_DLU };

static const DLGITEM p_time[] = {
    { WC_BUTTON, BS_GROUPBOX,        8,  8, 236, 40, -1, "Time format" },
    { WC_BUTTON, BS_AUTORADIOBUTTON,18, 22, 100, 10, IDC_T12, "12-hour (1:23:45 PM)" },
    { WC_BUTTON, BS_AUTORADIOBUTTON,130,22, 100, 10, IDC_T24, "24-hour (13:23:45)" },
    { WC_STATIC, 0,                  8, 60,  60, 9, -1, "Short date:" },
    { WC_COMBO,  0,                 70, 58, 120, 14, IDC_DATEFMT, 0 },
    { WC_STATIC, 0,                  8, 82, 236, 9, IDC_DSAMP, "" },
};
static const DLGTEMPLATE_L t_time = { "", 252, 116, p_time, 6, DS_DLU };

static HWND g_sheet;
static int g_sel, g_t24, g_df;

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void samples(HWND h) {
    SetDlgItemTextW32(h, IDC_SAMP, g_t24 ? "Sample:  1 234,56    13:23:45" : "Sample:  1,234.56    1:23:45 PM");
    char d[48] = "Date sample:  "; const char *s = g_df == 0 ? "9/22/26" : g_df == 1 ? "22/9/26" : "2026-09-22";
    int k = 14; for (int i = 0; s[i]; i++) d[k++] = s[i]; d[k] = 0; SetDlgItemTextW32(h, IDC_DSAMP, d);
}
static void apply(HWND h) {
    sys_set_setting("locale", g_loc[g_sel].name);
    char b[4]; b[0] = (char)('0' + g_t24); b[1] = 0; sys_set_setting("time_24h", b); wm_set_time_24h(g_t24);
    b[0] = (char)('0' + g_df); sys_set_setting("date_fmt", b);
    samples(h);
}

static LRESULT CALLBACK Reg_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            char v[48]; g_sel = 0;
            if (sys_get_setting("locale", v, sizeof v) == 0 && v[0]) for (int i = 0; i < NLOC; i++) if (streq(v, g_loc[i].name)) { g_sel = i; break; }
            g_t24 = g_loc[g_sel].t24; g_df = g_loc[g_sel].datefmt;
            if (sys_get_setting("time_24h", v, sizeof v) == 0 && v[0]) g_t24 = (v[0] != '0');
            if (sys_get_setting("date_fmt", v, sizeof v) == 0 && v[0]) g_df = v[0] - '0';
            DlgListReset(h, IDC_LOC); for (int i = 0; i < NLOC; i++) DlgListAdd(h, IDC_LOC, g_loc[i].name);
            DlgListSetSel(h, IDC_LOC, g_sel);
            for (int i = 0; i < 3; i++) DlgListAdd(h, IDC_DATEFMT, g_datefmt_name[i]);
            DlgListSetSel(h, IDC_DATEFMT, g_df);
            CheckDlgButton(h, IDC_T12, !g_t24); CheckDlgButton(h, IDC_T24, g_t24);
            samples(h);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_LOC: if (HIWORD(wParam) == LBN_SELCHANGE) { int s = DlgListGetSel(h, IDC_LOC); if (s >= 0) { g_sel = s; g_t24 = g_loc[s].t24; g_df = g_loc[s].datefmt;
                    CheckDlgButton(h, IDC_T12, !g_t24); CheckDlgButton(h, IDC_T24, g_t24); DlgListSetSel(h, IDC_DATEFMT, g_df); apply(h); } } break;
                case IDC_T12: g_t24 = 0; apply(h); break;
                case IDC_T24: g_t24 = 1; apply(h); break;
                case IDC_DATEFMT: if (HIWORD(wParam) == CBN_SELCHANGE) { int s = DlgListGetSel(h, IDC_DATEFMT); if (s >= 0) { g_df = s; apply(h); } } break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L reg_pages[] = {
    { "Regional Settings", &t_reg,  Reg_DlgProc },
    { "Time & Date",       &t_time, Reg_DlgProc },
};
static const PROPSHEETHEADER_L reg_psh = { "Regional Settings Properties", 252, 116, reg_pages, 2, DS_DLU };

void regional_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&reg_psh, startpage); }
