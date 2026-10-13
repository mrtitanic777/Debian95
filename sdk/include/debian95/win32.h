/* Debian95 — a small, clean-room Win32-style API over the native window manager.
 *
 * This is an ADDITIVE veneer: apps written to it use authentic Win32 idioms —
 * a single WndProc(hwnd,msg,wParam,lParam), WM_* messages, and a GDI HDC with
 * BeginPaint/FillRect/Rectangle/TextOut — while the window manager underneath is
 * unchanged. Existing apps that use wm_add_custom directly keep working; new
 * apps can be written the "1995" way. All in-process, so dispatch is a couple of
 * function-pointer calls (no IPC, no dlopen), keeping the instant-launch feel. */
#ifndef DEBIAN95_WIN32_H
#define DEBIAN95_WIN32_H

#include <debian95/types.h>

/* ---- handles (opaque) ---- */
typedef void *HWND;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef void *HBRUSH;
typedef void *HPEN;

typedef unsigned long WPARAM;
typedef unsigned long LPARAM;
typedef long          LRESULT;
typedef unsigned int  UINT;
#ifndef CALLBACK
#define CALLBACK
#endif

/* COLORREF helper (identical to fb.h's, so including both is fine) */
#ifndef RGB
#define RGB(r,g,b)  (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#endif

/* ---- window messages (real Win32 values, for fidelity) ---- */
#define WM_CREATE       0x0001
#define WM_DESTROY      0x0002
#define WM_CLOSE        0x0010
#define WM_PAINT        0x000F
#define WM_COMMAND      0x0111
#define WM_TIMER        0x0113
#define WM_KEYDOWN      0x0100
#define WM_CHAR         0x0102
#define WM_LBUTTONDOWN  0x0201
#define WM_RBUTTONDOWN  0x0204
#define WM_MOUSEMOVE    0x0200
#define MK_LBUTTON      0x0001   /* wParam flag on WM_MOUSEMOVE: left button held */
#define MK_RBUTTON      0x0002

/* ---- WPARAM/LPARAM packing (as in windowsx.h) ---- */
#define LOWORD(l)   ((unsigned)((l) & 0xFFFF))
#define HIWORD(l)   ((unsigned)(((l) >> 16) & 0xFFFF))
#define MAKELPARAM(lo,hi) ((LPARAM)(((unsigned)(lo) & 0xFFFF) | ((unsigned)(hi) << 16)))
#define MAKEWPARAM(lo,hi) ((WPARAM)(((unsigned)(lo) & 0xFFFF) | ((unsigned)(hi) << 16)))
#define GET_X_LPARAM(l)  ((int)(short)LOWORD(l))
#define GET_Y_LPARAM(l)  ((int)(short)HIWORD(l))

typedef LRESULT (CALLBACK *WNDPROC)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef struct { int left, top, right, bottom; } RECT;
typedef struct { HDC hdc; RECT rcPaint; } PAINTSTRUCT;

/* ---- window management ---- */
/* Create + show a top-level app window bound to a WndProc. WM_CREATE fires
 * before the window is shown (lParam = createParam). Returns 0 if none free. */
HWND    CreateAppWindow(int x, int y, int w, int h, const char *title,
                        WNDPROC proc, void *createParam);
void    DestroyWindow(HWND hwnd);            /* fires WM_DESTROY, removes the window */
LRESULT DefWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void   *GetWindowUserData(HWND hwnd);        /* per-window state pointer (GWLP_USERDATA) */
void    SetWindowUserData(HWND hwnd, void *p);
void    InvalidateRect(HWND hwnd);           /* request a repaint */
void    win32_timer_tick(void);              /* WM's frame hook: fires WM_TIMER to all */

/* a menu bar whose item command IDs arrive as WM_COMMAND(LOWORD(wParam)=id). */
/* id<0 = separator. checked -> a check mark; disabled -> grayed + unclickable.
 * For state that changes at runtime, leave these 0 and install a per-window
 * callback via wm_set_menuflags() that returns (bit0=checked | bit1=disabled). */
typedef struct { const char *label; int id; int checked; int disabled; } MenuItemW32;
typedef struct { const char *label; const MenuItemW32 *items; int nitems; } MenuW32;
void    SetMenuW32(HWND hwnd, const MenuW32 *menus, int n);
void    SetMenuFlagsW32(HWND hwnd, int (*cb)(int id));    /* dynamic check/disable (bit0=checked, bit1=disabled) */

/* ---- GDI: device context + objects ---- */
#define TRANSPARENT 1
#define OPAQUE      2
HDC     BeginPaint(HWND hwnd, PAINTSTRUCT *ps);
void    EndPaint(HWND hwnd, HDC hdc);
HBRUSH  CreateSolidBrush(u32 rgb);
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj);      /* returns the previous object */
void    DeleteObject(HGDIOBJ obj);
void    FillRect(HDC hdc, const RECT *rc, HBRUSH br);
void    FrameRect(HDC hdc, const RECT *rc, HBRUSH br);
void    Rectangle(HDC hdc, int l, int t, int r, int b);   /* filled(brush)+outlined(pen) */
void    TextOut(HDC hdc, int x, int y, const char *s, int n);
u32     SetTextColor(HDC hdc, u32 rgb);
int     SetBkMode(HDC hdc, int mode);
void    DrawEdge3D(HDC hdc, const RECT *rc, int raised);  /* Win95 raised/sunken bevel */

/* ---- COMDLG32-lite ---- */
#define MB_OK        0
#define MB_ICONINFO      0x40
#define MB_ICONWARNING   0x30
#define MB_ICONERROR     0x10
int     MessageBoxW32(HWND hwnd, const char *text, const char *caption, UINT type);

/* Common file dialog (a shared Open/Save picker every app reuses). Because the
 * WM event loop is non-blocking, the pick is delivered via ofn->onok(path)
 * rather than a blocking return (the practical form of GetOpenFileName here).
 * The struct's fields are copied on the call, so a stack OPENFILENAME is fine. */
typedef struct {
    HWND        owner;
    const char *title;     /* dialog caption (0 = "Open"/"Save As") */
    const char *filter;    /* e.g. "*.TXT" — extension filter (0 = all files) */
    char        file[64];  /* out: the chosen name (also copied to onok's arg) */
    void      (*onok)(const char *path);   /* fired when the user confirms */
} OPENFILENAME;
int     GetOpenFileName(OPENFILENAME *ofn);   /* 1 if the dialog opened */
int     GetSaveFileName(OPENFILENAME *ofn);

/* ---- declarative dialogs (a lightweight DLGITEMTEMPLATE / .rc model) ----
 * Controls are DATA, not drawing code: an app declares a template and a DlgProc.
 * The engine lays out + paints the controls and routes clicks/keys as messages
 * (WM_INITDIALOG once, then WM_COMMAND(LOWORD(wParam)=control id)). Constants
 * below are the documented Win32 values so templates read like real .rc files. */
#define WM_INITDIALOG   0x0110
/* standard command ids (documented) */
#define IDOK      1
#define IDCANCEL  2
#define IDYES     6
#define IDNO      7
/* predefined control-class atoms (documented dialog-template atoms) */
#define WC_BUTTON    0x0080
#define WC_EDIT      0x0081
#define WC_STATIC    0x0082
#define WC_LISTBOX   0x0083
#define WC_LISTVIEW  0x0084   /* report list with columns (SysListView32-style) */
#define WC_TRACKBAR  0x0085   /* horizontal slider (msctls_trackbar32-style) */
#define WC_COMBO     0x0086   /* drop-down combo box (fill/read with DlgList* by id) */
#define WC_MONTHCAL  0x0087   /* month calendar (SysMonthCal32-style) */
#define WC_OWNERDRAW 0x0088   /* app-painted region: DlgProc gets WM_DRAWITEM(id, &RECT) */
#define WC_TABFRAME  0x0089   /* a tab-control body frame (SysTabControl32 placeholder) */
#define WC_TREEVIEW  0x008A   /* SysTreeView32-style tree (indent + expand/collapse) */
#define TBS_VERT     0x02     /* trackbar style: vertical slider */
#define WM_DRAWITEM  0x002B   /* wParam = control id, lParam = RECT* in absolute fb pixels */
/* button styles (subset; low nibble selects the button kind) */
#define BS_PUSHBUTTON      0x00
#define BS_DEFPUSHBUTTON   0x01
#define BS_CHECKBOX        0x02
#define BS_AUTOCHECKBOX    0x03
#define BS_RADIOBUTTON     0x04
#define BS_GROUPBOX        0x07
#define BS_AUTORADIOBUTTON 0x09
/* static styles */
#define SS_LEFT            0x00
#define SS_ETCHEDHORZ      0x10   /* a horizontal separator line */
/* listbox / listview styles + notifications (HIWORD of WM_COMMAND wParam) */
#define LBS_NOTIFY         0x01
#define ES_PASSWORD        0x20   /* edit shows dots */
#define LBN_SELCHANGE      1
#define LBN_DBLCLK         2
#define TBN_CHANGED        3   /* trackbar position changed (WM_COMMAND notify) */
#define CBN_SELCHANGE      5   /* combo selection changed (WM_COMMAND notify) */
#define MCN_SELCHANGE      6   /* month-calendar date changed (WM_COMMAND notify) */
#define TVN_SELCHANGED     7   /* treeview selection changed (WM_COMMAND notify) */
/* dialog template flags */
#define DS_DLU             0x01   /* item coords are Dialog Units (like a real .rc), not pixels */

typedef struct { u16 wclass; u32 style; int x, y, cx, cy; int id; const char *text; } DLGITEM;
typedef struct { const char *caption; int cx, cy; const DLGITEM *items; int nitems; int flags; } DLGTEMPLATE_L;

HWND CreateDialogL(const DLGTEMPLATE_L *tmpl, WNDPROC dlgProc, void *param);
void EndDialogL(HWND hdlg);
int  GetDlgItemTextW32(HWND hdlg, int id, char *buf, int max);
void SetDlgItemTextW32(HWND hdlg, int id, const char *s);
void CheckDlgButton(HWND hdlg, int id, int check);
int  IsDlgButtonChecked(HWND hdlg, int id);
void EnableDlgItem(HWND hdlg, int id, int enable);         /* grey out / re-enable a control */

/* ---- list controls (LISTBOX single-column, LISTVIEW report with columns) ----
 * Content is data too: fill a list from the DlgProc, read the selection back.
 * The engine paints rows, header, selection and scrollbar and notifies the
 * DlgProc via WM_COMMAND(id, LBN_SELCHANGE|LBN_DBLCLK). */
void DlgListReset(HWND hdlg, int id);
int  DlgListAdd(HWND hdlg, int id, const char *text);       /* returns row index */
int  DlgListCount(HWND hdlg, int id);
int  DlgListGetSel(HWND hdlg, int id);                      /* -1 if none */
void DlgListSetSel(HWND hdlg, int id, int row);
/* LISTVIEW report columns + rows (each row is `ncells` strings) */
void DlgLVColumns(HWND hdlg, int id, const char *const *titles, const int *widths, int n);
int  DlgLVAddRow(HWND hdlg, int id, const char *const *cells, int ncells);
/* TRACKBAR (slider): range + position; DlgProc gets WM_COMMAND(id, TBN_CHANGED) on drag */
void DlgTrackRange(HWND hdlg, int id, int lo, int hi);
void DlgTrackPos(HWND hdlg, int id, int pos);
int  DlgTrackGetPos(HWND hdlg, int id);
/* MONTHCAL: set/read the selected date (DlgProc gets WM_COMMAND(id, MCN_SELCHANGE)) */
void DlgCalSet(HWND hdlg, int id, int y, int mo, int d);
void DlgCalGet(HWND hdlg, int id, int *y, int *mo, int *d);
/* TREEVIEW: build a tree (pre-order, each node has a depth + expandable flag);
 * clicking [+]/[-] expands, clicking a row selects (WM_COMMAND(id, TVN_SELCHANGED)). */
void DlgTreeReset(HWND hdlg, int id);
int  DlgTreeAdd(HWND hdlg, int id, const char *text, int depth, int expandable);   /* -> node index */
int  DlgTreeGetSel(HWND hdlg, int id);
void DlgTreeSetSel(HWND hdlg, int id, int node);
void DlgTreeExpand(HWND hdlg, int id, int node, int on);

/* ---- property sheet (tabbed dialog: a Control-Panel-applet host) ----
 * Each page is a template + a page DlgProc; the host draws the tab strip and the
 * OK / Cancel / Apply buttons and switches pages. A page gets WM_INITDIALOG when
 * it becomes active (so it can load live state) and WM_COMMAND(IDOK) on Apply/OK
 * (to persist). Page control coordinates are relative to the page's own area. */
typedef struct { const char *title; const DLGTEMPLATE_L *page; WNDPROC proc; } PROPSHEETPAGE_L;
typedef struct { const char *caption; int cx, cy; const PROPSHEETPAGE_L *pages; int npages; int flags; } PROPSHEETHEADER_L;
HWND PropertySheetL(const PROPSHEETHEADER_L *psh, int startpage);
HWND PropSheetPageDlg(HWND hsheet, int pageindex);   /* the HWND a page's DlgProc uses for DlgItem calls */

/* absolute client origin/size of a window (for controls that draw to the fb). */
void GetClientOrigin(HWND hwnd, int *x, int *y, int *w, int *h);
void SetWindowSize(HWND hwnd, int w, int h);                /* resize the window (client+chrome) */
void SetWindowSizeGrip(HWND hwnd);                          /* add a bottom-right resize grip */
void SetWindowText(HWND hwnd, const char *title);          /* change the title-bar caption */
int  IsWindowVisible(HWND hwnd);                            /* 1 if shown (not hidden/minimized) */
void SetWindowTextCursor(HWND hwnd);                        /* use the I-beam cursor over the client */
void *GetWindowObject(HWND hwnd);                           /* underlying WM window (for wm_* task APIs) */

/* print spooler (print.c): jobs render to real files under C:\Printed */
void print_submit_text(const char *doc, const char *body, u32 len);
void print_submit_bmp(const char *doc, const u32 *px, int w, int h);
void printman_open(void);                                   /* the Print Manager queue window */

/* set the window's caption/taskbar icon from a real extracted icon */
void SetWindowIcon(HWND hwnd, u16 shell32_icon_id);         /* IC_* from icons.h */
void SetWindowIconMod(HWND hwnd, const char *module, u16 icon_id);  /* e.g. "MMSYS.CPL", 7 */
void SetWindowHelp(HWND hwnd, const char *help);   /* "What's This?" -> a ? caption button + popup */

#endif /* DEBIAN95_WIN32_H */
