/* Debian95 — the Keyboard Control-Panel applet as a Win32 PROPERTY SHEET: real
 * repeat delay / repeat rate / caret blink TRACKBARs (sys_set_key_repeat via
 * EVIOCSREP + sys_set_caret_ms), persisted to SETTINGS.INI. No hand-placed pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
int  sys_set_key_repeat(int delay_ms, int rate_cps);
void sys_set_caret_ms(int ms);

enum { IDC_DELAY = 800, IDC_RATE, IDC_TEST, IDC_CARET, IDC_LANG };

static const DLGITEM p_speed[] = {
    { WC_BUTTON,   BS_GROUPBOX,  8,   6, 236, 72, -1, "Character repeat" },
    { WC_STATIC,   0,           16,  20,  70, 9, -1, "Repeat delay" },
    { WC_STATIC,   0,           90,  20,  30, 9, -1, "Long" },
    { WC_STATIC,   0,          210,  20,  30, 9, -1, "Short" },
    { WC_TRACKBAR, 0,          118,  18,  86,14, IDC_DELAY, 0 },
    { WC_STATIC,   0,           16,  40,  70, 9, -1, "Repeat rate" },
    { WC_STATIC,   0,           90,  40,  30, 9, -1, "Slow" },
    { WC_STATIC,   0,          210,  40,  30, 9, -1, "Fast" },
    { WC_TRACKBAR, 0,          118,  38,  86,14, IDC_RATE, 0 },
    { WC_EDIT,     0,           16,  58, 224,12, IDC_TEST, "Click here and type to test the repeat rate" },
    { WC_BUTTON,   BS_GROUPBOX,  8,  84, 236, 40, -1, "Cursor blink rate" },
    { WC_STATIC,   0,           16,  98,  30, 9, -1, "Slow" },
    { WC_STATIC,   0,          210,  98,  30, 9, -1, "Fast" },
    { WC_TRACKBAR, 0,           50,  96, 154,14, IDC_CARET, 0 },
};
static const DLGTEMPLATE_L t_speed = { "", 252, 132, p_speed, 14, DS_DLU };

static const DLGITEM p_lang[] = {
    { WC_STATIC,  0,          8,  6, 236, 9, -1, "Installed keyboard languages and layouts:" },
    { WC_LISTBOX, LBS_NOTIFY, 8, 18, 236, 60, IDC_LANG, 0 },
    { WC_STATIC,  0,          8, 84, 236, 9, -1, "Switch languages:  Left Alt+Shift" },
};
static const DLGTEMPLATE_L t_lang = { "", 252, 132, p_lang, 3, DS_DLU };

static HWND g_sheet;

static void kbd_apply(HWND h) {
    int delay = 1000 - DlgTrackGetPos(h, IDC_DELAY) * 750 / 100;    /* left=Long(1000) .. right=Short(250) */
    int rate  = 2 + DlgTrackGetPos(h, IDC_RATE) * 28 / 100;         /* left=Slow(2) .. right=Fast(30) */
    int caret = 1200 - DlgTrackGetPos(h, IDC_CARET) * 1000 / 100;   /* left=Slow(1200) .. right=Fast(200) */
    sys_set_key_repeat(delay, rate); sys_set_caret_ms(caret);
    char b[8];
    { int v=delay,k=0,j=0; char t[8]; if(!v)t[j++]='0'; while(v){t[j++]=(char)('0'+v%10);v/=10;} while(j)b[k++]=t[--j]; b[k]=0; sys_set_setting("kbd_delay", b); }
    { int v=rate,k=0,j=0; char t[8]; if(!v)t[j++]='0'; while(v){t[j++]=(char)('0'+v%10);v/=10;} while(j)b[k++]=t[--j]; b[k]=0; sys_set_setting("kbd_rate", b); }
    { int v=caret,k=0,j=0; char t[8]; if(!v)t[j++]='0'; while(v){t[j++]=(char)('0'+v%10);v/=10;} while(j)b[k++]=t[--j]; b[k]=0; sys_set_setting("caret_ms", b); }
}
static int getn(const char *key, int def) { char v[8]; if (sys_get_setting(key, v, sizeof v) == 0 && v[0]) { int n=0; for(int i=0;v[i]>='0'&&v[i]<='9';i++) n=n*10+(v[i]-'0'); return n; } return def; }

static LRESULT CALLBACK Kbd_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            DlgTrackRange(h, IDC_DELAY, 0, 100); DlgTrackRange(h, IDC_RATE, 0, 100); DlgTrackRange(h, IDC_CARET, 0, 100);
            int delay = getn("kbd_delay", 250), rate = getn("kbd_rate", 30), caret = getn("caret_ms", 530);
            DlgTrackPos(h, IDC_DELAY, (1000 - delay) * 100 / 750);
            DlgTrackPos(h, IDC_RATE,  (rate - 2) * 100 / 28);
            DlgTrackPos(h, IDC_CARET, (1200 - caret) * 100 / 1000);
            DlgListReset(h, IDC_LANG); DlgListAdd(h, IDC_LANG, "En  English (United States)   US");
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(wParam) == TBN_CHANGED) kbd_apply(h);
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L kbd_pages[] = {
    { "Speed",    &t_speed, Kbd_DlgProc },
    { "Language", &t_lang,  Kbd_DlgProc },
};
static const PROPSHEETHEADER_L kbd_psh = { "Keyboard Properties", 252, 132, kbd_pages, 2, DS_DLU };

void keyboard_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&kbd_psh, startpage); }
