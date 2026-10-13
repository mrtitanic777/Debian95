/* Debian95 — the Sounds Control-Panel applet as a Win32 PROPERTY SHEET: a listview
 * of system events + their assigned .wav, a COMBO to change the assignment, and a
 * Play button, all over the real persisted settings (snd_<event>) and the real
 * mixer (snd_play). Templates + a DlgProc; no hand-placed pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
void snd_play(const char *name);

static const char *snd_ev[]  = { "start", "exit", "beep", "error", "warning", "question", "menu", "open" };
static const char *snd_evl[] = { "Windows Start", "Windows Exit", "Default Beep", "Critical Stop", "Exclamation", "Question", "Menu command", "Open program" };
#define NSNDEV 8
static const char *snd_choices[] = { "(None)", "ding.wav", "chord.wav", "chimes.wav", "tada.wav", "mssound.wav" };
#define NSNDCH 6

static int strcmp_(const char *a, const char *b);

enum { IDC_EVLIST = 1000, IDC_COMBO, IDC_PLAY };

static const DLGITEM p_snd[] = {
    { WC_STATIC,   0,           8,  6, 100, 9, -1, "Events:" },
    { WC_LISTVIEW, LBS_NOTIFY,  8, 18, 236, 66, IDC_EVLIST, 0 },
    { WC_STATIC,   0,           8, 92,  40, 9, -1, "Sound:" },
    { WC_COMBO,    0,          44, 90, 110, 14, IDC_COMBO, 0 },
    { WC_BUTTON,   BS_PUSHBUTTON,196, 90, 48, 14, IDC_PLAY, "Play" },
};
static const DLGTEMPLATE_L t_snd = { "", 252, 116, p_snd, 5, DS_DLU };

static HWND g_sheet;

static const char *snd_default(const char *ev) {
    if (!strcmp_(ev,"start")) return "mssound.wav"; if (!strcmp_(ev,"error")) return "chord.wav";
    if (!strcmp_(ev,"warning")) return "chord.wav"; if (!strcmp_(ev,"question")) return "chord.wav";
    if (!strcmp_(ev,"beep")) return "ding.wav"; if (!strcmp_(ev,"exit")) return "tada.wav";
    return "(None)";
}
static int strcmp_(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static void assign_get(int e, char *out, int n) {
    char key[48]; int k = 0; const char *p = "snd_"; for (; p[k]; k++) key[k] = p[k];
    for (const char *s = snd_ev[e]; *s; s++) key[k++] = *s; key[k] = 0;
    if (sys_get_setting(key, out, n) == 0 && out[0]) return;
    const char *d = snd_default(snd_ev[e]); int i = 0; for (; d[i] && i < n-1; i++) out[i] = d[i]; out[i] = 0;
}
static void assign_set(int e, const char *file) {
    char key[48]; int k = 0; const char *p = "snd_"; for (; p[k]; k++) key[k] = p[k];
    for (const char *s = snd_ev[e]; *s; s++) key[k++] = *s; key[k] = 0;
    sys_set_setting(key, file);
}
static void ev_fill(HWND h) {
    DlgListReset(h, IDC_EVLIST);
    for (int i = 0; i < NSNDEV; i++) { char cur[40]; assign_get(i, cur, sizeof cur);
        const char *cells[2] = { snd_evl[i], cur }; DlgLVAddRow(h, IDC_EVLIST, cells, 2); }
}
static void combo_to_event(HWND h, int e) {   /* point the combo at event e's current assignment */
    char cur[40]; assign_get(e, cur, sizeof cur); int idx = 0;
    for (int i = 0; i < NSNDCH; i++) if (strcmp_(cur, snd_choices[i]) == 0) { idx = i; break; }
    DlgListSetSel(h, IDC_COMBO, idx);
}

static LRESULT CALLBACK Snd_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            static const char *const titles[2] = { "Event", "Sound" };
            static const int widths[2] = { 130, 100 };
            DlgLVColumns(h, IDC_EVLIST, titles, widths, 2);
            ev_fill(h);
            for (int i = 0; i < NSNDCH; i++) DlgListAdd(h, IDC_COMBO, snd_choices[i]);
            DlgListSetSel(h, IDC_EVLIST, 0); combo_to_event(h, 0);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_EVLIST: if (HIWORD(wParam) == LBN_SELCHANGE) { int e = DlgListGetSel(h, IDC_EVLIST); if (e >= 0) combo_to_event(h, e); } break;
                case IDC_COMBO: if (HIWORD(wParam) == CBN_SELCHANGE) {
                    int e = DlgListGetSel(h, IDC_EVLIST), c = DlgListGetSel(h, IDC_COMBO);
                    if (e >= 0 && c >= 0) { assign_set(e, snd_choices[c]); ev_fill(h); DlgListSetSel(h, IDC_EVLIST, e); }
                } break;
                case IDC_PLAY: { int c = DlgListGetSel(h, IDC_COMBO); if (c > 0) snd_play(snd_choices[c]); } break;
            }
            return 0;
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L snd_pages[] = { { "Sounds", &t_snd, Snd_DlgProc } };
static const PROPSHEETHEADER_L snd_psh = { "Sounds Properties", 252, 116, snd_pages, 1, DS_DLU };

void sounds_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&snd_psh, startpage); }
