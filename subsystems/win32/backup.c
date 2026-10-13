/* Debian95 — Microsoft Backup: a friendly front-end to tar. Tick the folders to
 * save, name the set, and "Back Up Now" writes a real gzip tarball under
 * <data>/Backups. Existing sets list with their date and size; "Restore" extracts
 * one back over your files and "Delete" removes it. Declarative dialog-engine UI
 * (checkboxes + WC_LISTVIEW), real backend (shims_linux.c). No hand-placed pixels. */
#include <debian95/win32.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* real tar backend (shims_linux.c) */
struct lg_backup { char name[64]; unsigned long long size; unsigned mtime; };
int sys_backup_available(void);
int sys_backup_sources(char labels[][48], int *exists, int max);
int sys_backup_create(const char *name, const int *pick, int npick);
int sys_backup_list(struct lg_backup *out, int max);
int sys_backup_restore(const char *name);
int sys_backup_delete(const char *name);

void wm_messagebox(const char *title, const char *msg);
void wm_confirm(const char *title, const char *text, void (*onyes)(void));

#define MAXSRC 4
enum { IDC_INTRO = 100, IDC_WHAT, IDC_SRC0, IDC_SRC1, IDC_SRC2, IDC_SRC3,
       IDC_BACKUP, IDC_HAVE, IDC_LIST, IDC_RESTORE, IDC_DELETE, IDC_REFRESH };

static const DLGITEM bk_items[] = {
    { WC_STATIC,   0,                 7,   7, 300, 20, IDC_INTRO,
      "Save your files to a compressed backup set, or restore a previous one." },
    { WC_BUTTON,   BS_GROUPBOX,       7,  30, 300, 70, IDC_WHAT,   "What to back up" },
    { WC_BUTTON,   BS_AUTOCHECKBOX,  16,  44, 140, 11, IDC_SRC0,   "" },
    { WC_BUTTON,   BS_AUTOCHECKBOX,  16,  57, 140, 11, IDC_SRC1,   "" },
    { WC_BUTTON,   BS_AUTOCHECKBOX, 160,  44, 140, 11, IDC_SRC2,   "" },
    { WC_BUTTON,   BS_AUTOCHECKBOX, 160,  57, 140, 11, IDC_SRC3,   "" },
    { WC_BUTTON,   BS_DEFPUSHBUTTON, 16,  80,  90, 14, IDC_BACKUP, "Back Up Now..." },
    { WC_STATIC,   0,                 7, 106, 300, 10, IDC_HAVE,   "Existing backup sets:" },
    { WC_LISTVIEW, LBS_NOTIFY,        7, 118, 300, 100, IDC_LIST,  0 },
    { WC_BUTTON,   BS_PUSHBUTTON,   118, 222,  60, 14, IDC_RESTORE,"Restore" },
    { WC_BUTTON,   BS_PUSHBUTTON,   182, 222,  60, 14, IDC_DELETE, "Delete" },
    { WC_BUTTON,   BS_PUSHBUTTON,   246, 222,  60, 14, IDC_REFRESH,"Refresh" },
};
static const DLGTEMPLATE_L bk_tmpl = { "Microsoft Backup", 314, 242, bk_items, 12, DS_DLU };

static HWND g_dlg;
static char g_lbl[MAXSRC][48]; static int g_exist[MAXSRC], g_nsrc;
static struct lg_backup g_sets[128]; static int g_nsets;
static char g_sel_name[64];

static void human_size(unsigned long long b, char *out, int n) {
    if (b >= 1024ULL * 1024) snprintf(out, n, "%.1f MB", b / (1024.0 * 1024));
    else if (b >= 1024)      snprintf(out, n, "%llu KB", b / 1024);
    else                     snprintf(out, n, "%llu B", b);
}

static void bk_fill(HWND h) {
    static const char *const t[3] = { "Name", "Date", "Size" };
    static const int w[3] = { 150, 110, 70 };
    DlgLVColumns(h, IDC_LIST, t, w, 3);
    DlgListReset(h, IDC_LIST);
    g_nsets = sys_backup_list(g_sets, 128);
    for (int i = 0; i < g_nsets; i++) {
        char date[32] = "", size[24];
        time_t tt = (time_t)g_sets[i].mtime; struct tm *tm = localtime(&tt);
        if (tm) strftime(date, sizeof date, "%Y-%m-%d %H:%M", tm);
        human_size(g_sets[i].size, size, sizeof size);
        const char *c[3] = { g_sets[i].name, date, size };
        DlgLVAddRow(h, IDC_LIST, c, 3);
    }
}

/* ---- name-the-set dialog (shown before a backup runs) ---- */
enum { IDC_NAME = 200, IDC_OK };
static const DLGITEM nm_items[] = {
    { WC_STATIC, 0,                10, 10, 250, 10, -1,       "Name for this backup set:" },
    { WC_EDIT,   0,                10, 24, 250, 14, IDC_NAME, "" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 96, 48,  70, 16, IDC_OK,   "OK" },
    { WC_BUTTON, BS_PUSHBUTTON,   190, 48,  70, 16, IDCANCEL, "Cancel" },
};
static const DLGTEMPLATE_L nm_tmpl = { "Back Up", 270, 74, nm_items, 4, DS_DLU };

static LRESULT CALLBACK Name_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            char def[32]; time_t tt = time(0); struct tm *tm = localtime(&tt);
            if (tm) strftime(def, sizeof def, "Backup %Y-%m-%d", tm); else snprintf(def, sizeof def, "Backup");
            SetDlgItemTextW32(h, IDC_NAME, def); return 1;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_OK) {
                char name[64]; GetDlgItemTextW32(h, IDC_NAME, name, sizeof name);
                int pick[MAXSRC]; for (int i = 0; i < MAXSRC; i++) pick[i] = g_dlg ? IsDlgButtonChecked(g_dlg, IDC_SRC0 + i) : 0;
                EndDialogL(h);
                int r = sys_backup_create(name, pick, g_nsrc);
                if (g_dlg) bk_fill(g_dlg);
                wm_messagebox("Microsoft Backup",
                    r == 0  ? "Backup complete.\nThe set was saved and is listed below." :
                    r == -2 ? "Nothing selected to back up.\nTick at least one folder that exists." :
                              "Backup failed.\nIs tar installed and the data folder writable?");
            } else if (id == IDCANCEL) EndDialogL(h);
            return 0;
        }
        case WM_DESTROY: return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static void bk_do_restore(void) {
    int r = sys_backup_restore(g_sel_name);
    if (g_dlg) bk_fill(g_dlg);
    wm_messagebox("Microsoft Backup", r == 0 ? "Restore complete.\nYour files were extracted from the set."
                                             : "Restore failed.\nThe set may be missing or unreadable.");
}
static void bk_do_delete(void) {
    sys_backup_delete(g_sel_name);
    if (g_dlg) bk_fill(g_dlg);
}

static LRESULT CALLBACK BK_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            if (!sys_backup_available()) { SetDlgItemTextW32(h, IDC_INTRO, "Backup needs 'tar', which is not installed on this machine."); return 1; }
            g_nsrc = sys_backup_sources(g_lbl, g_exist, MAXSRC);
            for (int i = 0; i < MAXSRC; i++) {
                if (i < g_nsrc) {
                    char lab[64]; snprintf(lab, sizeof lab, "%s%s", g_lbl[i], g_exist[i] ? "" : " (none)");
                    SetDlgItemTextW32(h, IDC_SRC0 + i, lab);
                    CheckDlgButton(h, IDC_SRC0 + i, g_exist[i]);
                    EnableDlgItem(h, IDC_SRC0 + i, g_exist[i]);
                } else EnableDlgItem(h, IDC_SRC0 + i, 0);
            }
            bk_fill(h); return 1;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_BACKUP) CreateDialogL(&nm_tmpl, Name_DlgProc, 0);
            else if (id == IDC_REFRESH) bk_fill(h);
            else if (id == IDC_RESTORE) {
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0 || s >= g_nsets) { wm_messagebox("Microsoft Backup", "Select a backup set to restore."); return 0; }
                snprintf(g_sel_name, sizeof g_sel_name, "%s", g_sets[s].name);
                wm_confirm("Restore Backup", "Restoring overwrites current files with the\nsaved copies. Continue?", bk_do_restore);
            } else if (id == IDC_DELETE) {
                int s = DlgListGetSel(h, IDC_LIST);
                if (s < 0 || s >= g_nsets) { wm_messagebox("Microsoft Backup", "Select a backup set to delete."); return 0; }
                snprintf(g_sel_name, sizeof g_sel_name, "%s", g_sets[s].name);
                wm_confirm("Delete Backup", "Delete the selected backup set?\nThis cannot be undone.", bk_do_delete);
            }
            return 0;
        }
        case WM_DESTROY: g_dlg = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void backup_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&bk_tmpl, BK_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 76, "Backup", IC_APP, 0, backup_open, -1 };
#endif
