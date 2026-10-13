/* Debian95 — Registry Editor (regedit): the classic Win95 two-pane editor, built
 * the Win32 way on the dialog engine — a WC_TREEVIEW of the standard HKEY hives
 * on the left, a WC_LISTVIEW of Name/Data values on the right, and a real menu
 * bar (Registry/Edit/View/Help). The editable branch HKEY_CURRENT_USER\Control
 * Panel\Debian95 is backed by the live persisted store (SETTINGS.INI), so New /
 * Modify / Delete change real, reboot-surviving state. No hand-placed pixels. */
#include <debian95/win32.h>

int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
int  sys_list_settings(char keys[][64], char vals[][192], int max);
void sys_del_setting(const char *key);
const char *hal_arch(void);
void wm_messagebox(const char *title, const char *msg);

static void scpy(char *d, const char *s) { int i = 0; for (; s[i]; i++) d[i] = s[i]; d[i] = 0; }

enum { C_NONE, C_CU_DEBIAN95, C_LM_DEBIAN95, C_CR_TXT, C_CR_BMP, C_CR_WAV };
typedef struct { const char *name; int depth, parent, content, haskids; } RegNode;
static RegNode g_node[] = {
    { "HKEY_CLASSES_ROOT",   0, -1, C_NONE,       1 },
    { ".txt",                1,  0, C_CR_TXT,     0 },
    { ".bmp",                1,  0, C_CR_BMP,     0 },
    { ".wav",                1,  0, C_CR_WAV,     0 },
    { "HKEY_CURRENT_USER",   0, -1, C_NONE,       1 },
    { "Control Panel",       1,  4, C_NONE,       1 },
    { "Debian95",             2,  5, C_CU_DEBIAN95, 0 },
    { "HKEY_LOCAL_MACHINE",  0, -1, C_NONE,       1 },
    { "SOFTWARE",            1,  7, C_NONE,       1 },
    { "Debian95",             2,  8, C_LM_DEBIAN95, 0 },
    { "HKEY_USERS",          0, -1, C_NONE,       0 },
    { "HKEY_CURRENT_CONFIG", 0, -1, C_NONE,       0 },
};
#define NNODE ((int)(sizeof g_node / sizeof g_node[0]))

enum { IDC_TREE = 100, IDC_VALS, IDC_STATUS };
enum { IDM_EXIT = 1, IDM_NEW, IDM_MODIFY, IDM_DELETE, IDM_REFRESH, IDM_ABOUT };

static char g_vname[64][64], g_vdata[64][192]; static int g_nval, g_editable;
static HWND g_win;

static const DLGITEM reg_items[] = {
    { WC_TREEVIEW, 0,          6,  6, 150, 196, IDC_TREE,   0 },
    { WC_LISTVIEW, LBS_NOTIFY,160,  6, 216, 196, IDC_VALS,   0 },
    { WC_STATIC,   0,          6, 206, 370,  9, IDC_STATUS, "" },
};
static const DLGTEMPLATE_L reg_tmpl = { "Registry Editor", 384, 220, reg_items, 3, DS_DLU };

static const MenuItemW32 M_registry[] = { { "Exit", IDM_EXIT } };
static const MenuItemW32 M_edit[]     = { { "New String Value", IDM_NEW }, { "Modify", IDM_MODIFY }, { "", -1 }, { "Delete", IDM_DELETE } };
static const MenuItemW32 M_view[]     = { { "Refresh", IDM_REFRESH } };
static const MenuItemW32 M_help[]     = { { "About Registry Editor", IDM_ABOUT } };
static const MenuW32 REG_MENUS[] = { { "Registry", M_registry, 1 }, { "Edit", M_edit, 4 }, { "View", M_view, 1 }, { "Help", M_help, 1 } };

static void load_values(HWND h, int node) {
    g_nval = 0; g_editable = 0;
    int c = g_node[node].content;
    if (c == C_CU_DEBIAN95) { g_nval = sys_list_settings(g_vname, g_vdata, 64); g_editable = 1; }
    else if (c == C_LM_DEBIAN95) {
        scpy(g_vname[0], "ProductName");    scpy(g_vdata[0], "Debian95");
        scpy(g_vname[1], "CurrentVersion"); scpy(g_vdata[1], "1.0");
        scpy(g_vname[2], "Architecture");   scpy(g_vdata[2], hal_arch());
        scpy(g_vname[3], "ProductType");    scpy(g_vdata[3], "Windows 95 desktop on Linux"); g_nval = 4;
    } else if (c == C_CR_TXT) { scpy(g_vname[0], "(Default)"); scpy(g_vdata[0], "txtfile"); g_nval = 1; }
    else if (c == C_CR_BMP)   { scpy(g_vname[0], "(Default)"); scpy(g_vdata[0], "Paint.Picture"); g_nval = 1; }
    else if (c == C_CR_WAV)   { scpy(g_vname[0], "(Default)"); scpy(g_vdata[0], "SoundRec"); g_nval = 1; }
    DlgListReset(h, IDC_VALS);
    for (int i = 0; i < g_nval; i++) { const char *cells[2] = { g_vname[i], g_vdata[i] }; DlgLVAddRow(h, IDC_VALS, cells, 2); }
    /* status = full key path */
    char path[256]; int chain[8], n = 0;
    for (int i = node; i >= 0; i = g_node[i].parent) chain[n++] = i;
    path[0] = 0; int k = 0; for (int i = n - 1; i >= 0; i--) { const char *nm = g_node[chain[i]].name;
        for (int j = 0; nm[j] && k < 254; j++) path[k++] = nm[j]; if (i && k < 254) path[k++] = '\\'; } path[k] = 0;
    SetDlgItemTextW32(h, IDC_STATUS, path);
}

/* ---- the New/Modify string-value sub-dialog ---- */
enum { IDC_ED_NAME = 200, IDC_ED_DATA };
static const DLGITEM ed_items[] = {
    { WC_STATIC, 0,             8,  8, 120, 9, -1, "Value name:" },
    { WC_EDIT,   0,             8, 20, 190, 12, IDC_ED_NAME, "" },
    { WC_STATIC, 0,             8, 38, 120, 9, -1, "Value data:" },
    { WC_EDIT,   0,             8, 50, 190, 12, IDC_ED_DATA, "" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 96, 70, 48, 14, IDOK, "OK" },
    { WC_BUTTON, BS_PUSHBUTTON,   150, 70, 48, 14, IDCANCEL, "Cancel" },
};
static const DLGTEMPLATE_L ed_tmpl = { "Edit String", 206, 92, ed_items, 6, DS_DLU };
static HWND g_ed; static int g_ed_isnew;

static LRESULT CALLBACK Ed_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            if (!g_ed_isnew) { SetDlgItemTextW32(h, IDC_ED_NAME, g_vname[DlgListGetSel(g_win, IDC_VALS) < 0 ? 0 : DlgListGetSel(g_win, IDC_VALS)]);
                               SetDlgItemTextW32(h, IDC_ED_DATA, g_vdata[DlgListGetSel(g_win, IDC_VALS) < 0 ? 0 : DlgListGetSel(g_win, IDC_VALS)]); }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) { char nm[64], dt[192]; GetDlgItemTextW32(h, IDC_ED_NAME, nm, sizeof nm); GetDlgItemTextW32(h, IDC_ED_DATA, dt, sizeof dt);
                if (nm[0]) { sys_set_setting(nm, dt); if (g_win) load_values(g_win, DlgTreeGetSel(g_win, IDC_TREE)); } EndDialogL(h); }
            else if (LOWORD(wParam) == IDCANCEL) EndDialogL(h);
            return 0;
        case WM_DESTROY: g_ed = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}
static void edit_value(int isnew) {
    if (!g_editable) { wm_messagebox("Registry Editor", "This key cannot be edited."); return; }
    if (!isnew && DlgListGetSel(g_win, IDC_VALS) < 0) { wm_messagebox("Registry Editor", "Select a value to modify."); return; }
    g_ed_isnew = isnew; if (g_ed) return;
    g_ed = CreateDialogL(&ed_tmpl, Ed_DlgProc, 0);
}

static LRESULT CALLBACK Reg_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            g_win = h;
            SetMenuW32(h, REG_MENUS, 4);
            for (int i = 0; i < NNODE; i++) DlgTreeAdd(h, IDC_TREE, g_node[i].name, g_node[i].depth, g_node[i].haskids);
            DlgTreeExpand(h, IDC_TREE, 4, 1);   /* HKEY_CURRENT_USER */
            DlgTreeExpand(h, IDC_TREE, 5, 1);   /* Control Panel */
            DlgTreeSetSel(h, IDC_TREE, 6);      /* Debian95 (editable) */
            static const char *const titles[2] = { "Name", "Data" };
            static const int widths[2] = { 90, 120 };
            DlgLVColumns(h, IDC_VALS, titles, widths, 2);
            load_values(h, 6);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_TREE: if (HIWORD(wParam) == TVN_SELCHANGED) { int nd = DlgTreeGetSel(h, IDC_TREE); if (nd >= 0) load_values(h, nd); } break;
                case IDM_NEW:     edit_value(1); break;
                case IDM_MODIFY:  edit_value(0); break;
                case IDM_DELETE:
                    if (g_editable) { int s = DlgListGetSel(h, IDC_VALS); if (s >= 0 && s < g_nval) { sys_del_setting(g_vname[s]); load_values(h, DlgTreeGetSel(h, IDC_TREE)); } }
                    else wm_messagebox("Registry Editor", "Select a value in an editable key first.");
                    break;
                case IDM_REFRESH: load_values(h, DlgTreeGetSel(h, IDC_TREE)); break;
                case IDM_ABOUT:   wm_messagebox("Registry Editor", "Debian95 Registry Editor\nHKEY_CURRENT_USER\\Control Panel\\Debian95 is\nbacked by the live settings store."); break;
                case IDM_EXIT:    EndDialogL(h); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

void regedit_open(void) { if (g_win) return; g_win = CreateDialogL(&reg_tmpl, Reg_DlgProc, 0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 29, "Registry Editor", IC_APP, 0, regedit_open, -1 };
#endif
