/* Debian95 — Control Panel (classic Win95). A folder of applets with real module
 * icons, two-line labels, single-click selection (with a description in the
 * status bar) and double-click to open. Arch-neutral. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/icons.h>
#include <debian95/krt.h>

#define C_FACE  RGB(0xC0, 0xC0, 0xC0)
#define C_DK    RGB(0x00, 0x00, 0x00)
#define C_SEL   RGB(0x00, 0x00, 0x80)
#define C_WHITE RGB(0xFF, 0xFF, 0xFF)

/* The classic Win95 applet set. Each has a real icon (module + RT_ICON id), a
 * two-line label, a Control-Panel description, and the applet action id used by
 * applet_open(). */
typedef struct { const char *l1, *l2; const char *mod; u16 id; const char *desc; int act; } Applet;
/* act = the applet id for applet_open() (enum A_* in applets.c); -1 = no sheet yet */
enum { CP_ACCESS=0, CP_APPWIZ=1, CP_DISPLAY=2, CP_INTL=3, CP_JOY=4, CP_KEYBD=5, CP_MODEM=6,
       CP_MOUSE=7, CP_MMSYS=8, CP_NET=9, CP_PASSWD=10, CP_SYSTEM=11, CP_TIMEDATE=12, CP_MMEDIA=13, CP_BLUETOOTH=14, CP_FONTS=15 };
static const Applet g_ap[] = {
    { "Accessibility", "Options",  "SHELL32.DLL", IC_SETTINGS, "Changes accessibility settings for your system.", CP_ACCESS },
    { "Bluetooth",     "",         "SHELL32.DLL", IC_NETWORK,  "Configures Bluetooth adapters and devices.", CP_BLUETOOTH },
    { "Add New",       "Hardware", "SHELL32.DLL", IC_APP,      "Adds new hardware to your system.", -1 },
    { "Add/Remove",    "Programs", "SHELL32.DLL", IC_APP,      "Installs and removes programs and Windows components.", CP_APPWIZ },
    { "Date/Time",     "",         "MAIN.CPL",    14,          "Changes the date, time and time zone for your computer.", CP_TIMEDATE },
    { "Display",       "",         "SHELL32.DLL", IC_GRAPHICS, "Changes the appearance of your desktop, such as the background, screen saver, colors, font sizes and screen resolution.", CP_DISPLAY },
    { "Fonts",         "",         "MAIN.CPL",    5,           "Displays and manages fonts on your computer.", CP_FONTS },
    { "Internet",      "",         "SHELL32.DLL", 31,          "Configures your Internet display and connection settings.", -1 },
    { "Joystick",      "",         "MAIN.CPL",    10,          "Configures the joysticks installed on your computer.", CP_JOY },
    { "Keyboard",      "",         "MAIN.CPL",    2,           "Changes settings for your keyboard.", CP_KEYBD },
    { "Mail",          "",         "SHELL32.DLL", 30,          "Microsoft Mail and Fax profile settings.", -1 },
    { "Microsoft Mail","Postoffice","SHELL32.DLL",30,          "Administers a Microsoft Mail postoffice.", -1 },
    { "Modems",        "",         "SHELL32.DLL", IC_APP,      "Installs a new modem and changes modem properties.", CP_MODEM },
    { "Mouse",         "",         "MAIN.CPL",    1,           "Changes settings for your mouse.", CP_MOUSE },
    { "Multimedia",    "",         "MMSYS.CPL",   1,           "Changes settings for multimedia audio, video, MIDI and CD music.", CP_MMEDIA },
    { "Network",       "",         "SHELL32.DLL", IC_NETWORK,  "Configures network hardware and software.", CP_NET },
    { "Passwords",     "",         "SHELL32.DLL", 47,          "Changes passwords and sets security options.", CP_PASSWD },
    { "Power",         "",         "SHELL32.DLL", IC_APP,      "Changes Power Management settings for your computer.", -1 },
    { "Printers",      "",         "MAIN.CPL",    4,           "Adds, removes and configures printers.", -1 },
    { "Regional",      "Settings", "MAIN.CPL",    12,          "Changes how numbers, currencies, dates and times are displayed.", CP_INTL },
    { "Sounds",        "",         "MMSYS.CPL",   7,           "Assigns sounds to system and program events.", CP_MMSYS },
    { "System",        "",         "SHELL32.DLL", IC_MYCOMPUTER,"Provides system information and changes advanced settings.", CP_SYSTEM },
};
#define NAP ((int)(sizeof g_ap / sizeof g_ap[0]))

static HWND g_cp;             /* the single Control Panel window (Win32 veneer) */
static int g_sel = -1;
void applet_open(int applet);

/* grid geometry */
#define COLS 5
#define CELLW 88
#define CELLH 62
#define GX 10
#define GY 6

static void cp_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, C_FACE);
    int lx = cx + 2, ly = cy + 2, lw = cw - 4, lh = ch - 4 - 20;
    fb_fill_rect(lx, ly, lw, lh, C_WHITE); wm_bevel_sunken(lx, ly, lw, lh);
    int fh = (int)font_height();
    for (int i = 0; i < NAP; i++) {
        int col = i % COLS, row = i / COLS;
        int x = lx + GX + col * CELLW, y = ly + GY + row * CELLH;
        static u32 ic[32 * 32];
        int gx = x + (CELLW - 32) / 2;
        if (icon_get_from(g_ap[i].mod, g_ap[i].id, 32, C_WHITE, ic) == 0) {
            if (i == g_sel) {                              /* 50% blue wash when selected */
                for (int j = 0; j < 32; j++) for (int k = 0; k < 32; k++) {
                    u32 c = ic[j*32+k]; fb_pixel(gx+k, y+j, (c == C_WHITE) ? C_WHITE : (((c>>1)&0x7F7F7F)+((C_SEL>>1)&0x7F7F7F))); }
            } else fb_blit(gx, y, 32, 32, ic);
        } else { fb_fill_rect(gx, y, 32, 32, RGB(0xD0,0xC0,0x40)); wm_bevel_raised(gx, y, 32, 32); }
        /* two-line centred label; selected -> white on blue */
        const char *lines[2] = { g_ap[i].l1, g_ap[i].l2 };
        for (int t = 0; t < 2 && lines[t] && lines[t][0]; t++) {
            int tw = (int)font_measure(lines[t]), tx = x + (CELLW - tw) / 2, ty = y + 34 + t * (fh + 1);
            if (i == g_sel) { fb_fill_rect(tx - 1, ty, tw + 2, fh + 1, C_SEL); fb_text_t(tx, ty, lines[t], C_WHITE); }
            else fb_text_t(tx, ty, lines[t], C_DK);
        }
    }
    /* status bar: description of the selected applet, else the object count */
    int sy = cy + ch - 20;
    fb_fill_rect(cx, sy, cw, 20, C_FACE);
    fb_fill_rect(cx + 2, sy, cw - 4, 18, C_FACE); wm_bevel_sunken(cx + 2, sy, cw - 4, 18);
    char cnt[40];
    const char *msg;
    if (g_sel >= 0 && g_sel < NAP) msg = g_ap[g_sel].desc;
    else { int n = NAP, k = 0; char t[8], nb[8]; if(!n)t[k++]='0'; while(n){t[k++]=(char)('0'+n%10);n/=10;} int j=0; while(k)nb[j++]=t[--k]; nb[j]=0;
        int p=0; for(int q=0;nb[q];q++)cnt[p++]=nb[q]; const char *w=" object(s)"; for(int q=0;w[q];q++)cnt[p++]=w[q]; cnt[p]=0; msg=cnt; }
    fb_text_t(cx + 8, sy + 4, msg, C_DK);
}

static int cp_hit(int rx, int ry, int cx, int cy) {
    int lx = 2 + GX, ly = 2 + GY;                          /* client-relative grid origin */
    (void)cx; (void)cy;
    for (int i = 0; i < NAP; i++) {
        int col = i % COLS, row = i / COLS;
        int x = lx + col * CELLW, y = ly + row * CELLH;
        if (rx >= x && rx < x + CELLW && ry >= y && ry < y + CELLH) return i;
    }
    return -1;
}
static u32 g_last_ms; static int g_last_i = -1;
static void cp_click(int rx, int ry) {
    int i = cp_hit(rx, ry, 0, 0);
    if (i < 0) { g_sel = -1; return; }
    u32 now = wm_now_ms();
    if (i == g_sel && i == g_last_i && now - g_last_ms < 400) {    /* double-click opens */
        if (g_ap[i].act >= 0) applet_open(g_ap[i].act); g_last_i = -1; return;
    }
    g_sel = i; g_last_i = i; g_last_ms = now;
}

static const MenuItemW32 CM_file[] = { { "Close", 9 } };
static const MenuItemW32 CM_help[] = { { "About Control Panel", 0 } };
static const MenuW32 CP_MENUS[] = { { "File", CM_file, 1 }, { "Edit", CM_file, 0 }, { "View", CM_file, 0 }, { "Help", CM_help, 1 } };

/* ---- Win32 veneer: WndProc ---- */
static LRESULT CALLBACK Control_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, CP_MENUS, 4);
            SetWindowIcon(hwnd, IC_SETTINGS);
            SetWindowSizeGrip(hwnd);
            g_cp = hwnd; g_sel = -1;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            cp_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: cp_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND: if (LOWORD(wParam) == 9) DestroyWindow(hwnd); return 0;
        case WM_DESTROY: g_cp = 0; g_sel = -1; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void control_open(void) {
    if (g_cp) return;                                     /* single instance */
    int rows = (NAP + COLS - 1) / COLS;
    CreateAppWindow(80, 60, COLS * CELLW + 24, rows * CELLH + GY + 30 + WM_TITLEH + 16, "Control Panel", Control_WndProc, 0);
}
