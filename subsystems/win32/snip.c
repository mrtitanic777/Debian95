/* Debian95 — Snipping Tool: capture the screen to a picture (scrot via sys_snip),
 * then open it in the Image Viewer to look at or save. Declarative dialog. */
#include <debian95/win32.h>

int  sys_snip(const char *relname);
void viewer_open(const char *name);          /* subsystems/win32/viewer.c */

enum { IDC_NEW = 100, IDC_VIEW, IDC_STATUS };

static const DLGITEM sn_items[] = {
    { WC_STATIC, 0,                10, 10, 244, 22, -1, "Capture the whole screen to a picture you can view and save." },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 10, 42, 82, 16, IDC_NEW,    "New Snip" },
    { WC_BUTTON, BS_PUSHBUTTON,   100, 42, 82, 16, IDC_VIEW,   "View Last" },
    { WC_STATIC, 0,                10, 68, 244, 12, IDC_STATUS, "" },
};
static const DLGTEMPLATE_L sn_tmpl = { "Snipping Tool", 266, 92, sn_items, 4, DS_DLU };

static int g_have;

static LRESULT CALLBACK SN_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_NEW) {
                if (sys_snip("Snip.png") == 0) {
                    g_have = 1;
                    SetDlgItemTextW32(h, IDC_STATUS, "Saved as Snip.png in your files.");
                    viewer_open("Snip.png");
                } else SetDlgItemTextW32(h, IDC_STATUS, "Capture failed (no screenshot tool).");
            } else if (LOWORD(wParam) == IDC_VIEW) {
                if (g_have) viewer_open("Snip.png");
                else SetDlgItemTextW32(h, IDC_STATUS, "No snip yet - click New Snip first.");
            }
            return 0;
        case WM_DESTROY: return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void snip_open(void) { CreateDialogL(&sn_tmpl, SN_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 69, "Snipping Tool", IC_DOC, 0, snip_open, -1 };
#endif
