/* Debian95 — the Passwords Control-Panel applet, built the Win32 way as a
 * PROPERTY SHEET: three pages, each a resource template, driven by a DlgProc.
 * The engine owns the tab strip, layout, painting, the listview + scrollbar and
 * OK/Cancel/Apply. This file only fills controls from, and acts on, the real
 * system (/etc/shadow password, prof setting, /etc/passwd accounts). */
#include <debian95/win32.h>

#define LG_ACCOUNT "administrator"

int  sys_has_password(const char *user);
int  sys_set_password(const char *user, const char *pass);
int  sys_get_setting(const char *key, char *out, int n);
void sys_set_setting(const char *key, const char *val);
struct lg_user { char name[32]; int uid; int is_admin; };
int  sys_user_list(struct lg_user *out, int max);
int  sys_user_add(const char *name, const char *pass, int admin);
int  sys_user_del(const char *name);

enum {
    IDC_NEWPW = 200, IDC_SETPW, IDC_PWMSG,                       /* page 1 */
    IDC_PROF_SAME = 210, IDC_PROF_CUSTOM,                        /* page 2 */
    IDC_ULIST = 220, IDC_NEWUSER, IDC_UADMIN, IDC_UADD, IDC_UREMOVE, IDC_UMSG,   /* page 3 */
};

/* ---- page 1: Change Password ---- */
static const DLGITEM p_change[] = {
    { WC_STATIC, 0,                8,  8, 236,  9, -1,        "Set the password for the administrator account." },
    { WC_STATIC, 0,                8, 30,  66,  9, -1,        "New password:" },
    { WC_EDIT,   ES_PASSWORD,     78, 28, 120, 12, IDC_NEWPW, "" },
    { WC_BUTTON, BS_DEFPUSHBUTTON, 8, 50,  96, 14, IDC_SETPW, "Set Password" },
    { WC_STATIC, 0,                8, 74, 236,  9, IDC_PWMSG, "" },
};
static const DLGTEMPLATE_L t_change = { "", 252, 130, p_change, 5, DS_DLU };

/* ---- page 2: User Profiles ---- */
static const DLGITEM p_prof[] = {
    { WC_BUTTON, BS_GROUPBOX,        8,  8, 236, 60, -1,             "User Profile Settings" },
    { WC_BUTTON, BS_AUTORADIOBUTTON, 18, 26, 216, 10, IDC_PROF_SAME,   "All users share the same preferences and desktop." },
    { WC_BUTTON, BS_AUTORADIOBUTTON, 18, 44, 216, 10, IDC_PROF_CUSTOM, "Users can customize their preferences and desktop." },
};
static const DLGTEMPLATE_L t_prof = { "", 252, 130, p_prof, 3, DS_DLU };

/* ---- page 3: User Accounts ---- */
static const DLGITEM p_users[] = {
    { WC_STATIC,   0,               8,   6, 160,  9, -1,          "Users on this computer:" },
    { WC_LISTVIEW, LBS_NOTIFY,      8,  18, 236, 66, IDC_ULIST,   0 },
    { WC_STATIC,   0,               8,  92,  42,  9, -1,          "New user:" },
    { WC_EDIT,     0,              52,  90,  88, 12, IDC_NEWUSER, "" },
    { WC_BUTTON,   BS_AUTOCHECKBOX,148,  91, 48, 10, IDC_UADMIN,  "Admin" },
    { WC_BUTTON,   BS_PUSHBUTTON,  200,  90, 44, 12, IDC_UADD,    "Add" },
    { WC_BUTTON,   BS_PUSHBUTTON,   8, 108,  44, 12, IDC_UREMOVE, "Remove" },
    { WC_STATIC,   0,              58, 109, 186,  9, IDC_UMSG,    "" },
};
static const DLGTEMPLATE_L t_users = { "", 252, 130, p_users, 8, DS_DLU };

static HWND g_sheet;
static struct lg_user g_user[64]; static int g_nuser;

static void users_fill(HWND h) {
    g_nuser = sys_user_list(g_user, 64);
    DlgListReset(h, IDC_ULIST);
    for (int i = 0; i < g_nuser; i++) {
        const char *cells[2] = { g_user[i].name, g_user[i].is_admin ? "Administrator" : "Standard user" };
        DlgLVAddRow(h, IDC_ULIST, cells, 2);
    }
}

static LRESULT CALLBACK Pw_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            SetDlgItemTextW32(h, IDC_PWMSG, sys_has_password(LG_ACCOUNT)
                ? "A password protects this account." : "This account has no password set.");
            char v[8]; int same = 1; if (sys_get_setting("prof_same", v, sizeof v) == 0 && v[0]) same = (v[0] != '0');
            CheckDlgButton(h, IDC_PROF_SAME, same); CheckDlgButton(h, IDC_PROF_CUSTOM, !same);
            static const char *const titles[2] = { "Name", "Type" };
            static const int widths[2] = { 130, 100 };
            DlgLVColumns(h, IDC_ULIST, titles, widths, 2);
            users_fill(h);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_SETPW: {
                    char pw[64]; GetDlgItemTextW32(h, IDC_NEWPW, pw, sizeof pw);
                    if (!pw[0]) { SetDlgItemTextW32(h, IDC_PWMSG, "Enter a new password first."); break; }
                    int rc = sys_set_password(LG_ACCOUNT, pw);
                    SetDlgItemTextW32(h, IDC_PWMSG, rc == 0 ? "Password changed." : "Could not change the password.");
                    SetDlgItemTextW32(h, IDC_NEWPW, "");
                    break;
                }
                case IDC_PROF_SAME:   sys_set_setting("prof_same", "1"); break;
                case IDC_PROF_CUSTOM: sys_set_setting("prof_same", "0"); break;
                case IDC_UADD: {
                    char nm[32]; GetDlgItemTextW32(h, IDC_NEWUSER, nm, sizeof nm);
                    if (!nm[0]) { SetDlgItemTextW32(h, IDC_UMSG, "Enter a user name."); break; }
                    int rc = sys_user_add(nm, "", IsDlgButtonChecked(h, IDC_UADMIN));
                    SetDlgItemTextW32(h, IDC_UMSG, rc == 0 ? "Account created." : "Could not create (needs root).");
                    if (rc == 0) { SetDlgItemTextW32(h, IDC_NEWUSER, ""); CheckDlgButton(h, IDC_UADMIN, 0); users_fill(h); }
                    break;
                }
                case IDC_UREMOVE: {
                    int sel = DlgListGetSel(h, IDC_ULIST);
                    if (sel < 0 || sel >= g_nuser) { SetDlgItemTextW32(h, IDC_UMSG, "Select an account to remove."); break; }
                    if (g_user[sel].uid == 0) { SetDlgItemTextW32(h, IDC_UMSG, "The root account cannot be removed."); break; }
                    int rc = sys_user_del(g_user[sel].name);
                    SetDlgItemTextW32(h, IDC_UMSG, rc == 0 ? "Account removed." : "Could not remove (needs root).");
                    if (rc == 0) users_fill(h);
                    break;
                }
                case IDOK:   /* Apply/OK: profile radio already persisted on click */ break;
            }
            return 0;
        }
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L pw_pages[] = {
    { "Change Passwords", &t_change, Pw_DlgProc },
    { "User Profiles",    &t_prof,   Pw_DlgProc },
    { "User Accounts",    &t_users,  Pw_DlgProc },
};
static const PROPSHEETHEADER_L pw_psh = { "Passwords Properties", 252, 130, pw_pages, 3, DS_DLU };

void passwords_sheet_open(int startpage) {
    if (g_sheet) return;
    g_sheet = PropertySheetL(&pw_psh, startpage);
}
