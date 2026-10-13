/* Debian95 — Narrator: type text and the computer reads it aloud in the iconic
 * robotic "Microsoft Sam" voice (espeak-ng via sys_speak). Declarative dialog. */
#include <debian95/win32.h>

void sys_speak(const char *text);
void sys_speak_stop(void);

enum { IDC_TEXT = 100, IDC_SPEAK, IDC_STOP };

static const DLGITEM nr_items[] = {
    { WC_STATIC, 0,                10, 10, 248, 12, -1, "Type text and click Speak - the computer reads it aloud." },
    { WC_EDIT,   0,                10, 28, 248, 16, IDC_TEXT, "Welcome to Debian95. I am Microsoft Sam." },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 62, 54, 70, 16, IDC_SPEAK, "Speak" },
    { WC_BUTTON, BS_PUSHBUTTON,   142, 54, 70, 16, IDC_STOP,  "Stop" },
};
static const DLGTEMPLATE_L nr_tmpl = { "Narrator", 268, 82, nr_items, 4, DS_DLU };

static LRESULT CALLBACK NR_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_SPEAK) { char t[512]; GetDlgItemTextW32(h, IDC_TEXT, t, sizeof t); if (t[0]) sys_speak(t); }
            else if (LOWORD(wParam) == IDC_STOP) sys_speak_stop();
            return 0;
        case WM_DESTROY: sys_speak_stop(); return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void narrator_open(void) { CreateDialogL(&nr_tmpl, NR_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 70, "Narrator", IC_APP, 0, narrator_open, -1 };
#endif
