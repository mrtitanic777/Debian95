/* Debian95 — shell dialogs: Run... and Shut Down, built the Win32 way as
 * declarative templates + DlgProcs. The engine owns layout, the edit box, the
 * radios and the buttons; the icon is painted via an owner-draw item (WM_DRAWITEM).
 * No hand-placed pixels. */
#include <debian95/win32.h>
#include <debian95/fb.h>
#include <debian95/icons.h>

extern int  ps_spawn(const char *name);
void        sys_shutdown(int restart);
void        wm_messagebox(const char *title, const char *msg);

#define ICON_KEY RGB(0xFF,0x00,0xFF)
static void draw_icon(RECT *rc, u16 icon_id) {
    static u32 buf[32*32];
    if (icon_get(icon_id, 32, ICON_KEY, buf) != 0) return;
    for (int j = 0; j < 32; j++) for (int i = 0; i < 32; i++) { u32 c = buf[j*32+i]; if (c != ICON_KEY) fb_pixel(rc->left + i, rc->top + j, c); }
}

/* ---- Run... ---- */
enum { IDC_RUN_ICON = 10, IDC_RUN_EDIT, IDC_RUN_BROWSE };
static const DLGITEM run_items[] = {
    { WC_OWNERDRAW, 0,              10, 10, 24, 24, IDC_RUN_ICON, 0 },
    { WC_STATIC,    0,              44, 10, 200, 9, -1, "Type the name of a program, folder, or document," },
    { WC_STATIC,    0,              44, 22, 200, 9, -1, "and Debian95 will open it for you." },
    { WC_STATIC,    0,              10, 42,  30, 9, -1, "Open:" },
    { WC_EDIT,      0,              42, 40, 202, 12, IDC_RUN_EDIT, "" },
    { WC_BUTTON,    BS_DEFPUSHBUTTON,96, 60, 46, 14, IDOK, "OK" },
    { WC_BUTTON,    BS_PUSHBUTTON, 146, 60, 46, 14, IDCANCEL, "Cancel" },
    { WC_BUTTON,    BS_PUSHBUTTON, 196, 60, 48, 14, IDC_RUN_BROWSE, "Browse..." },
};
static const DLGTEMPLATE_L run_tmpl = { "Run", 254, 80, run_items, 8, DS_DLU };
static HWND g_run;
static LRESULT CALLBACK Run_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: SetWindowHelp(h, "Type the name of a program, folder, or document, then click OK."); return 0;
        case WM_DRAWITEM: if ((int)wParam == IDC_RUN_ICON) draw_icon((RECT *)lParam, IC_APP); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) { char cmd[64]; GetDlgItemTextW32(h, IDC_RUN_EDIT, cmd, sizeof cmd); if (cmd[0]) ps_spawn(cmd); EndDialogL(h); }
            else if (LOWORD(wParam) == IDCANCEL) EndDialogL(h);
            return 0;
        case WM_DESTROY: g_run = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
void run_open(void) { if (g_run) return; g_run = CreateDialogL(&run_tmpl, Run_DlgProc, 0); }

/* ---- Shut Down ---- */
enum { IDC_SD_ICON = 20, IDC_SD_OFF, IDC_SD_STANDBY, IDC_SD_RESTART, IDC_SD_DOS, IDC_SD_HELP };
static const DLGITEM sd_items[] = {
    { WC_OWNERDRAW, 0,               10, 12, 24, 24, IDC_SD_ICON, 0 },
    { WC_STATIC,    0,               44, 14, 200, 9, -1, "Are you sure you want to shut down the computer?" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,44, 34, 190, 10, IDC_SD_OFF,     "Shut down the computer" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,44, 48, 190, 10, IDC_SD_STANDBY, "Stand by" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,44, 62, 190, 10, IDC_SD_RESTART, "Restart the computer" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,44, 76, 190, 10, IDC_SD_DOS,     "Restart in MS-DOS mode" },
    { WC_BUTTON,    BS_DEFPUSHBUTTON, 20, 98, 66, 14, IDOK,       "Yes" },
    { WC_BUTTON,    BS_PUSHBUTTON,    92, 98, 66, 14, IDCANCEL,   "No" },
    { WC_BUTTON,    BS_PUSHBUTTON,   164, 98, 66, 14, IDC_SD_HELP,"Help" },
};
static const DLGTEMPLATE_L sd_tmpl = { "Shut Down Windows", 250, 118, sd_items, 9, DS_DLU };
static HWND g_sd;
static LRESULT CALLBACK Shutdown_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: CheckDlgButton(h, IDC_SD_OFF, 1);
            SetWindowHelp(h, "Choose Shut down to power off, or Restart to reboot, then click Yes."); return 0;
        case WM_DRAWITEM: if ((int)wParam == IDC_SD_ICON) draw_icon((RECT *)lParam, IC_SHUTDOWN); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                if (IsDlgButtonChecked(h, IDC_SD_STANDBY)) { EndDialogL(h); sys_suspend(); }   /* Stand By: suspend */
                else { int restart = !IsDlgButtonChecked(h, IDC_SD_OFF); EndDialogL(h); wm_shutdown_begin(restart); }
            }
            else if (LOWORD(wParam) == IDCANCEL) EndDialogL(h);
            else if (LOWORD(wParam) == IDC_SD_HELP) wm_messagebox("Shut Down", "Choose an option, then click Yes.");
            return 0;
        case WM_DESTROY: g_sd = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
void shutdown_open(void) { if (g_sd) return; g_sd = CreateDialogL(&sd_tmpl, Shutdown_DlgProc, 0); }

/* ---- Welcome to Debian95 (first-run setup) ---- */
void sys_set_setting(const char *key, const char *val);
int  sys_set_hostname(const char *name);
void wm_post_action(int act);
void wm_setup_end(void);
enum { IDC_WEL_ICON = 40, IDC_WEL_NAME, IDC_WEL_95, IDC_WEL_98, IDC_WEL_XP, IDC_WEL_FLAT };
static const DLGITEM wel_items[] = {
    { WC_OWNERDRAW, 0,                12, 12,  32, 32, IDC_WEL_ICON, 0 },
    { WC_STATIC,    0,                52, 14, 216,  9, -1, "Welcome to Debian95!" },
    { WC_STATIC,    0,                52, 26, 216,  9, -1, "Let's set up your computer - it only takes a moment." },
    { WC_STATIC,    0,                14, 54,  70,  9, -1, "Computer name:" },
    { WC_EDIT,      0,                86, 52, 140, 12, IDC_WEL_NAME, "DEBIAN95" },
    { WC_STATIC,    0,                14, 76, 120,  9, -1, "Choose your look:" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,18, 90, 100, 10, IDC_WEL_95,   "Windows 95" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,18,102, 100, 10, IDC_WEL_98,   "Windows 98" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,140,90, 110, 10, IDC_WEL_XP,   "Windows XP" },
    { WC_BUTTON,    BS_AUTORADIOBUTTON,140,102,110, 10, IDC_WEL_FLAT, "Modern (Flat)" },
    { WC_BUTTON,    BS_DEFPUSHBUTTON, 202,128,  64, 16, IDOK, "Finish" },
};
static const DLGTEMPLATE_L wel_tmpl = { "Welcome to Debian95", 278, 154, wel_items, 11, DS_DLU };
static HWND g_wel;
static LRESULT CALLBACK Welcome_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: CheckDlgButton(h, IDC_WEL_95, 1); return 0;
        case WM_DRAWITEM: if ((int)wParam == IDC_WEL_ICON) draw_icon((RECT *)lParam, IC_WINLOGO); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                char nm[48]; GetDlgItemTextW32(h, IDC_WEL_NAME, nm, sizeof nm);
                if (nm[0]) sys_set_hostname(nm);
                int th = IsDlgButtonChecked(h, IDC_WEL_98) ? 1 :
                         IsDlgButtonChecked(h, IDC_WEL_XP) ? 2 :
                         IsDlgButtonChecked(h, IDC_WEL_FLAT) ? 3 : 0;
                wm_post_action(90 + th);              /* apply the chosen theme (shell + framed apps) */
                sys_set_setting("setup_done", "1");   /* only show this once */
                EndDialogL(h);
                wm_setup_end();                       /* reveal the desktop (OOBE done) */
                wm_messagebox("Debian95", "Setup complete. Welcome to Debian95!");
            }
            return 0;
        case WM_DESTROY: g_wel = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
void welcome_open(void) { if (g_wel) return; g_wel = CreateDialogL(&wel_tmpl, Welcome_DlgProc, 0); }

/* ---- Welcome tips: the Win95 "Did you know..." box, shown at startup until the
 * user clears "Show tips at startup". A lightbulb, a rotating tip, Next Tip/Close. */
static void draw_bulb(RECT *rc) {
    int cx = rc->left + 16, cy = rc->top + 13, r = 9;
    u32 Y = RGB(0xFF,0xE8,0x40), Yd = RGB(0xC8,0xA8,0x10), G = RGB(0x98,0x98,0x98), Gd = RGB(0x60,0x60,0x60);
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)   /* glass bulb */
        if (i*i + j*j <= r*r) fb_pixel(cx + i, cy + j, (i*i + j*j > (r-2)*(r-2)) ? Yd : Y);
    fb_fill_rect(cx-5, cy+r-1, 10, 3, G); fb_fill_rect(cx-4, cy+r+2, 8, 2, Gd); fb_fill_rect(cx-3, cy+r+4, 6, 2, G);  /* screw base */
    fb_fill_rect(cx-1, rc->top, 2, 3, Y);                          /* rays */
    fb_fill_rect(rc->left+2, cy-7, 3, 2, Y); fb_fill_rect(rc->left+27, cy-7, 3, 2, Y);
}
enum { IDC_TIP_ICON = 60, IDC_TIP_HDR, IDC_TIP_TEXT, IDC_TIP_SHOW, IDC_TIP_NEXT };
static const char *TIPS[] = {
    "You can change your wallpaper and colors in the Display control panel.",
    "Right-click the desktop to line up your icons or make a new folder.",
    "Every tool in Start > Programs > Accessories is real and fully working.",
    "Drag a desktop icon and it snaps neatly to the grid when you drop it.",
    "Open the Control Panel to set up sound, the network, printers and more.",
    "Hold Esc at boot to reach Safe Mode if the desktop ever won't start.",
    "Click Start, then Shut Down, when you are ready to turn off your computer.",
};
#define NTIPS ((int)(sizeof TIPS / sizeof TIPS[0]))
static const DLGITEM tip_items[] = {
    { WC_OWNERDRAW, 0,               12, 14, 32, 32, IDC_TIP_ICON, 0 },
    { WC_STATIC,    0,               56, 12, 214, 12, IDC_TIP_HDR,  "Did you know..." },
    { WC_STATIC,    0,               56, 30, 214, 34, IDC_TIP_TEXT, "" },
    { WC_BUTTON,    BS_AUTOCHECKBOX, 12, 78, 170, 12, IDC_TIP_SHOW, "Show tips at startup" },
    { WC_BUTTON,    BS_PUSHBUTTON,  142, 94, 62, 14, IDC_TIP_NEXT, "Next Tip" },
    { WC_BUTTON,    BS_DEFPUSHBUTTON,208,94, 62, 14, IDOK,         "Close" },
};
static const DLGTEMPLATE_L tip_tmpl = { "Welcome to Debian95", 282, 118, tip_items, 6, DS_DLU };
static HWND g_tip; static int g_tipn;
static LRESULT CALLBACK Tips_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: CheckDlgButton(h, IDC_TIP_SHOW, 1);
            SetDlgItemTextW32(h, IDC_TIP_TEXT, TIPS[g_tipn]);
            SetWindowHelp(h, "These tips introduce Debian95. Click Next Tip for more, or clear the box to stop showing them."); return 0;
        case WM_DRAWITEM: if ((int)wParam == IDC_TIP_ICON) draw_bulb((RECT *)lParam); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_TIP_NEXT) { g_tipn = (g_tipn + 1) % NTIPS; SetDlgItemTextW32(h, IDC_TIP_TEXT, TIPS[g_tipn]); }
            else if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                if (!IsDlgButtonChecked(h, IDC_TIP_SHOW)) sys_set_setting("show_tips", "0");
                EndDialogL(h);
            }
            return 0;
        case WM_DESTROY: g_tip = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
void welcome_tips_open(void) { if (g_tip) return; static int seed; g_tipn = (seed++) % NTIPS; g_tip = CreateDialogL(&tip_tmpl, Tips_DlgProc, 0); }
