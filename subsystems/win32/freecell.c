/* Debian95 — FreeCell. The GOLD-STANDARD reference app: written entirely in the
 * authentic Win32 idiom (a single WndProc, WM_* messages, a GDI HDC via
 * BeginPaint/FillRect/Rectangle/TextOut, WM_COMMAND menu handling) on top of the
 * Debian95 Win32 veneer (sdk/include/debian95/win32.h). It also keeps a clean
 * Model (the FC game state) separate from the View (WM_PAINT). Use this file as
 * the template when adding new apps. */
#include <debian95/win32.h>
#include <debian95/mm.h>
#include <debian95/nt.h>
#include <debian95/icons.h>    /* IC_APP */

extern unsigned sys_uptime_ms(void);

/* ---- Model ---- */
typedef struct { u8 rank, suit; } Card;         /* rank 1..13; suit 0=♣ 1=♠ (black) 2=♥ 3=♦ (red) */
typedef struct {
    Card free[4]; int freeUsed[4];
    int  foundN[4];                              /* foundation top rank per suit (0 = empty) */
    Card col[8][24]; int colN[8];
    int  selType, selIdx;                        /* selection: -1 none; 0=free,1=col */
    int  won;
    /* one-level undo snapshot (everything above except sel) */
    Card ufree[4]; int ufreeUsed[4]; int ufoundN[4]; Card ucol[8][24]; int ucolN[8]; int hasUndo;
} FC;

/* menu command ids */
enum { IDM_NEW = 1, IDM_UNDO = 2, IDM_EXIT = 3, IDM_OPEN = 4, IDM_SAVE = 5, IDM_ABOUT = 10 };

/* ---- layout ---- */
#define CW 46
#define CH 62
#define GX 6
#define MX 10
#define TOPY 8
#define COLY (TOPY + CH + 16)
#define OVER 18

static int is_red(int suit) { return suit >= 2; }
static int alt_color(int a, int b) { return is_red(a) != is_red(b); }

static u32 g_seed;
static int rnd(int n) { g_seed = g_seed * 1103515245u + 12345u; return (int)((g_seed >> 16) % (unsigned)n); }

static void deal(FC *fc) {
    Card deck[52]; int k = 0;
    for (int s = 0; s < 4; s++) for (int r = 1; r <= 13; r++) { deck[k].rank = (u8)r; deck[k].suit = (u8)s; k++; }
    for (int i = 51; i > 0; i--) { int j = rnd(i + 1); Card t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
    for (int i = 0; i < 4; i++) { fc->freeUsed[i] = 0; fc->foundN[i] = 0; }
    for (int c = 0; c < 8; c++) fc->colN[c] = 0;
    for (int i = 0; i < 52; i++) { int c = i % 8; fc->col[c][fc->colN[c]++] = deck[i]; }
    fc->selType = -1; fc->selIdx = -1; fc->won = 0; fc->hasUndo = 0;
}
static void snapshot(FC *fc) {
    for (int i = 0; i < 4; i++) { fc->ufree[i] = fc->free[i]; fc->ufreeUsed[i] = fc->freeUsed[i]; fc->ufoundN[i] = fc->foundN[i]; }
    for (int c = 0; c < 8; c++) { fc->ucolN[c] = fc->colN[c]; for (int i = 0; i < fc->colN[c]; i++) fc->ucol[c][i] = fc->col[c][i]; }
    fc->hasUndo = 1;
}
static void undo(FC *fc) {
    if (!fc->hasUndo) return;
    for (int i = 0; i < 4; i++) { fc->free[i] = fc->ufree[i]; fc->freeUsed[i] = fc->ufreeUsed[i]; fc->foundN[i] = fc->ufoundN[i]; }
    for (int c = 0; c < 8; c++) { fc->colN[c] = fc->ucolN[c]; for (int i = 0; i < fc->ucolN[c]; i++) fc->col[c][i] = fc->ucol[c][i]; }
    fc->hasUndo = 0; fc->selType = -1; fc->won = 0;
}

/* fetch the currently selected card (does not remove it) */
static int sel_card(FC *fc, Card *out) {
    if (fc->selType == 0) { if (!fc->freeUsed[fc->selIdx]) return 0; *out = fc->free[fc->selIdx]; return 1; }
    if (fc->selType == 1) { int c = fc->selIdx; if (!fc->colN[c]) return 0; *out = fc->col[c][fc->colN[c] - 1]; return 1; }
    return 0;
}
static void sel_remove(FC *fc) {
    if (fc->selType == 0) fc->freeUsed[fc->selIdx] = 0;
    else if (fc->selType == 1) fc->colN[fc->selIdx]--;
}
static void check_win(FC *fc) { int t = 0; for (int i = 0; i < 4; i++) t += fc->foundN[i]; if (t == 52) fc->won = 1; }

/* try to move the selected card to a destination; returns 1 if it moved */
static int try_move(FC *fc, int dstType, int dstIdx) {
    Card c; if (!sel_card(fc, &c)) return 0;
    if (dstType == 0) {                                   /* -> free cell */
        if (fc->freeUsed[dstIdx]) return 0;
        snapshot(fc); sel_remove(fc); fc->free[dstIdx] = c; fc->freeUsed[dstIdx] = 1; return 1;
    }
    if (dstType == 2) {                                   /* -> foundation (by suit) */
        if (c.rank != fc->foundN[c.suit] + 1) return 0;
        snapshot(fc); sel_remove(fc); fc->foundN[c.suit] = c.rank; check_win(fc); return 1;
    }
    if (dstType == 1) {                                   /* -> cascade */
        int d = dstIdx;
        if (fc->colN[d] == 0) { snapshot(fc); sel_remove(fc); fc->col[d][fc->colN[d]++] = c; return 1; }
        Card top = fc->col[d][fc->colN[d] - 1];
        if (alt_color(c.suit, top.suit) && c.rank + 1 == top.rank) {
            if (fc->selType == 1 && fc->selIdx == d) return 0;   /* same column no-op */
            snapshot(fc); sel_remove(fc); fc->col[d][fc->colN[d]++] = c; return 1;
        }
        return 0;
    }
    return 0;
}

/* ---- View ---- */
static const char *RANK[] = { "", "A","2","3","4","5","6","7","8","9","10","J","Q","K" };
static const char *SUIT[] = { "C","S","H","D" };

static void draw_card(HDC hdc, int x, int y, Card c, int sel) {
    RECT r = { x, y, x + CW, y + CH };
    HBRUSH wh = CreateSolidBrush(RGB(0xFF,0xFF,0xFF));
    SelectObject(hdc, wh); Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    DeleteObject(wh);
    if (sel) { RECT s = { x-1, y-1, x+CW+1, y+CH+1 }; HBRUSH b = CreateSolidBrush(RGB(0,0,0xC0)); FrameRect(hdc, &s, b); DeleteObject(b); }
    u32 col = is_red(c.suit) ? RGB(0xC0,0,0) : RGB(0,0,0);
    SetTextColor(hdc, col);
    TextOut(hdc, x + 4, y + 3, RANK[c.rank], 3);
    TextOut(hdc, x + 4, y + 16, SUIT[c.suit], 1);
    TextOut(hdc, x + CW - 16, y + CH - 18, SUIT[c.suit], 1);
}
static void draw_slot(HDC hdc, int x, int y, const char *tag) {
    RECT r = { x, y, x + CW, y + CH };
    HBRUSH g = CreateSolidBrush(RGB(0,0x60,0)); FillRect(hdc, &r, g); DeleteObject(g);
    HBRUSH out = CreateSolidBrush(RGB(0,0x40,0)); FrameRect(hdc, &r, out); DeleteObject(out);
    if (tag) { SetTextColor(hdc, RGB(0,0x50,0)); TextOut(hdc, x + CW/2 - 4, y + CH/2 - 6, tag, 2); }
}

static void paint(HWND hwnd, FC *fc) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT full = ps.rcPaint;
    HBRUSH table = CreateSolidBrush(RGB(0,0x80,0)); FillRect(hdc, &full, table); DeleteObject(table);
    /* top row: 4 free cells then 4 foundations */
    for (int i = 0; i < 4; i++) {
        int x = MX + i * (CW + GX);
        if (fc->freeUsed[i]) draw_card(hdc, x, TOPY, fc->free[i], fc->selType == 0 && fc->selIdx == i);
        else draw_slot(hdc, x, TOPY, 0);
    }
    for (int i = 0; i < 4; i++) {
        int x = MX + (4 + i) * (CW + GX);
        if (fc->foundN[i]) { Card c = { (u8)fc->foundN[i], (u8)i }; draw_card(hdc, x, TOPY, c, 0); }
        else draw_slot(hdc, x, TOPY, SUIT[i]);
    }
    /* cascades */
    for (int c = 0; c < 8; c++) {
        int x = MX + c * (CW + GX);
        if (fc->colN[c] == 0) draw_slot(hdc, x, COLY, 0);
        for (int i = 0; i < fc->colN[c]; i++) {
            int y = COLY + i * OVER;
            int sel = (fc->selType == 1 && fc->selIdx == c && i == fc->colN[c] - 1);
            draw_card(hdc, x, y, fc->col[c][i], sel);
        }
    }
    if (fc->won) { SetTextColor(hdc, RGB(0xFF,0xFF,0)); TextOut(hdc, MX + 120, TOPY + 20, "You won!  Game > New Game", 26); }
    EndPaint(hwnd, hdc);
}

/* hit test a click; sets *type (0 free, 1 cascade, 2 foundation) and *idx, or returns 0 */
static int hit(int x, int y, int *type, int *idx) {
    if (y >= TOPY && y < TOPY + CH) {
        int j = (x - MX) / (CW + GX); if (j < 0 || j > 7 || x < MX) return 0;
        if (j < 4) { *type = 0; *idx = j; } else { *type = 2; *idx = j - 4; }
        return 1;
    }
    if (y >= COLY) {
        int c = (x - MX) / (CW + GX); if (c < 0 || c > 7 || x < MX) return 0;
        *type = 1; *idx = c; return 1;
    }
    return 0;
}

static void on_click(HWND hwnd, FC *fc, int x, int y) {
    int type, idx; if (!hit(x, y, &type, &idx)) { fc->selType = -1; return; }
    if (fc->selType < 0) {                                /* pick a source */
        if (type == 0 && fc->freeUsed[idx]) { fc->selType = 0; fc->selIdx = idx; }
        else if (type == 1 && fc->colN[idx] > 0) { fc->selType = 1; fc->selIdx = idx; }
        return;
    }
    if (type == fc->selType && idx == fc->selIdx) { fc->selType = -1; return; }  /* click source again = cancel */
    if (!try_move(fc, type, idx)) {                       /* illegal: try re-selecting the new pile */
        fc->selType = -1;
        if (type == 0 && fc->freeUsed[idx]) { fc->selType = 0; fc->selIdx = idx; }
        else if (type == 1 && fc->colN[idx] > 0) { fc->selType = 1; fc->selIdx = idx; }
    } else fc->selType = -1;
    (void)hwnd;
}

static const MenuItemW32 M_game[] = { { "New Game", IDM_NEW }, { "Undo", IDM_UNDO }, { "", -1 },
                                      { "Open...", IDM_OPEN }, { "Save...", IDM_SAVE }, { "", -1 }, { "Exit", IDM_EXIT } };
static const MenuItemW32 M_help[] = { { "About FreeCell", IDM_ABOUT } };
static const MenuW32 FC_MENU[] = { { "Game", M_game, 7 }, { "Help", M_help, 1 } };

/* Open/Save via the shared common file dialog (COMDLG32). The dialog delivers
 * the pick through a callback, so the active game is reached via a static HWND. */
static HWND g_fc_hwnd;
static void fc_save_cb(const char *name) {
    FC *fc = (FC *)GetWindowUserData(g_fc_hwnd); if (fc) FsWriteFile(name, (const u8 *)fc, sizeof(FC));
}
static void fc_open_cb(const char *name) {
    FC *fc = (FC *)GetWindowUserData(g_fc_hwnd); u8 *d; u32 n;
    if (fc && FsReadAlloc(name, &d, &n) == 0) {
        if (n == sizeof(FC)) { u8 *dst = (u8 *)fc; for (u32 i = 0; i < n; i++) dst[i] = d[i]; fc->selType = -1; }
        kfree(d);
    }
}

static LRESULT CALLBACK FreeCell_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FC *fc = (FC *)GetWindowUserData(hwnd);
    switch (msg) {
        case WM_CREATE:
            fc = (FC *)kmalloc(sizeof(FC));
            g_seed = sys_uptime_ms() ^ 0x9E3779B9u;
            deal(fc); SetWindowUserData(hwnd, fc);
            SetMenuW32(hwnd, FC_MENU, 2);
            SetWindowIcon(hwnd, IC_APP);
            g_fc_hwnd = hwnd;
            return 0;
        case WM_PAINT:   paint(hwnd, fc); return 0;
        case WM_LBUTTONDOWN: on_click(hwnd, fc, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_NEW:  deal(fc); break;
                case IDM_UNDO: undo(fc); break;
                case IDM_OPEN: { OPENFILENAME o = {0}; o.title="Open Game"; o.filter="*.FCL"; o.onok=fc_open_cb; GetOpenFileName(&o); } break;
                case IDM_SAVE: { OPENFILENAME o = {0}; o.title="Save Game"; o.filter="*.FCL"; o.file[0]='G';o.file[1]='A';o.file[2]='M';o.file[3]='E';o.file[4]='.';o.file[5]='F';o.file[6]='C';o.file[7]='L';o.file[8]=0; o.onok=fc_save_cb; GetSaveFileName(&o); } break;
                case IDM_ABOUT: MessageBoxW32(hwnd, "Debian95 FreeCell\nA Win32-idiom reference app\n(WndProc + GDI).", "About FreeCell", MB_ICONINFO); break;
                case IDM_EXIT: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY: if (fc) kfree(fc); return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void freecell_open(void) {
    CreateAppWindow(150, 70, 8 * (CW + GX) + 2 * MX + 8, COLY + 8 * OVER + CH + 24,
                    "FreeCell", FreeCell_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 24, "FreeCell", IC_APP, 0, freecell_open, -1 };
#endif
