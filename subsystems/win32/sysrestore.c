/* Debian95 — System Restore: save restore points of your desktop state (settings,
 * themes, wallpaper choice, per-user profiles) and roll back to them. Snapshots are
 * stored on the encrypted data volume. Declarative WC_LISTVIEW dialog. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>

struct lg_restore { char id[96]; char label[64]; char when[32]; };
int sys_restore_create(const char *label);
int sys_restore_list(struct lg_restore *out, int max);
int sys_restore_apply(const char *id);
int sys_restore_del(const char *id);

enum { IDC_INTRO = 100, IDC_LIST, IDC_CREATE, IDC_RESTORE, IDC_DELETE, IDC_REFRESH };

static const DLGITEM sr_items[] = {
    { WC_STATIC,   0,                 7,   7, 286, 20, IDC_INTRO,
      "Restore points snapshot your whole system (btrfs) plus your settings. Create one before big changes, then roll back any time." },
    { WC_LISTVIEW, LBS_NOTIFY,        7,  30, 286, 145, IDC_LIST,   0 },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 22, 193,  86, 14, IDC_CREATE, "Create Restore Point" },
    { WC_BUTTON,   BS_PUSHBUTTON,   112, 193,  70, 14, IDC_RESTORE,"Restore" },
    { WC_BUTTON,   BS_PUSHBUTTON,   186, 193,  50, 14, IDC_DELETE, "Delete" },
    { WC_BUTTON,   BS_PUSHBUTTON,   240, 193,  53, 14, IDC_REFRESH,"Refresh" },
};
static const DLGTEMPLATE_L sr_tmpl = { "System Restore", 300, 214, sr_items, 6, DS_DLU };

static HWND g_dlg;
static struct lg_restore g_rp[128]; static int g_nrp;

static void sr_fill(HWND h) {
    static const char *const t[2] = { "Restore point", "Created" };
    static const int w[2] = { 180, 100 };
    DlgLVColumns(h, IDC_LIST, t, w, 2);
    DlgListReset(h, IDC_LIST);
    g_nrp = sys_restore_list(g_rp, 128);
    for (int i = 0; i < g_nrp; i++) { const char *c[2] = { g_rp[i].label, g_rp[i].when }; DlgLVAddRow(h, IDC_LIST, c, 2); }
}

/* ---- Create dialog (label the point) ---- */
enum { IDC_NAME = 200 };
static const DLGITEM cr_items[] = {
    { WC_STATIC, 0,                10, 10, 250, 12, -1, "Describe this restore point:" },
    { WC_EDIT,   0,                10, 26, 250, 14, IDC_NAME,  "Before my changes" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 96, 50,  74, 16, IDOK,     "Create" },
    { WC_BUTTON, BS_PUSHBUTTON,   174, 50,  74, 16, IDCANCEL, "Cancel" },
};
static const DLGTEMPLATE_L cr_tmpl = { "Create a Restore Point", 270, 76, cr_items, 4, DS_DLU };

static LRESULT CALLBACK Cr_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                char nm[96]; GetDlgItemTextW32(h, IDC_NAME, nm, sizeof nm);
                int r = sys_restore_create(nm[0] ? nm : "Restore point");
                EndDialogL(h); if (g_dlg) sr_fill(g_dlg);
                MessageBoxW32(0, r == 0 ? "Restore point created." : "Could not create a restore point.",
                              "System Restore", r == 0 ? MB_ICONINFO : MB_ICONERROR);
            } else if (LOWORD(wParam) == IDCANCEL) EndDialogL(h);
            return 0;
        case WM_DESTROY: return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK SR_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: sr_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_CREATE) CreateDialogL(&cr_tmpl, Cr_DlgProc, 0);
            else if (id == IDC_RESTORE) {
                int s = DlgListGetSel(h, IDC_LIST);
                if (s >= 0 && s < g_nrp) {
                    if (sys_restore_apply(g_rp[s].id) == 0)
                        MessageBoxW32(h, "Settings restored. Restart Debian95 (Start > Shut Down > Restart) to apply them.", "System Restore", MB_ICONINFO);
                    else
                        MessageBoxW32(h, "Could not apply that restore point.", "System Restore", MB_ICONERROR);
                }
            } else if (id == IDC_DELETE) { int s = DlgListGetSel(h, IDC_LIST); if (s >= 0 && s < g_nrp) { sys_restore_del(g_rp[s].id); sr_fill(h); } }
            else if (id == IDC_REFRESH) sr_fill(h);
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void sysrestore_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&sr_tmpl, SR_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 75, "System Restore", IC_SETTINGS, 0, sysrestore_open, -1 };
#endif
