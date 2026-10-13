/* Debian95 — Solitaire (sol.exe, Klondike).
 *
 * A clean-room Klondike Solitaire on the shared win32 stack: green baize,
 * stock/waste, four suit foundations, seven tableau columns with the classic
 * overlap, red/black cards with rank + suit pips and a blue card back.
 * Interaction is click-to-move (click a face-up card to pick it up with the
 * run beneath it, click a destination pile to drop) so it needs no drag-drop.
 * Deal is a real shuffle; foundation and tableau move rules are enforced.
 */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>       /* wm_bevel_* used by the card/pile drawing */
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/icons.h>

extern unsigned sys_uptime_s(void);

#define CW 44
#define CH 60
#define GAP 8
#define COLGAP 8
#define OVER_UP 16      /* face-up overlap step */
#define OVER_DN 5       /* face-down overlap step */

#define C_BAIZE  RGB(0x00, 0x80, 0x00)
#define C_CARD   RGB(0xFF, 0xFF, 0xFF)
#define C_BACK   RGB(0x30, 0x40, 0xC0)
#define C_RED    RGB(0xC0, 0x00, 0x00)
#define C_BLK    RGB(0x00, 0x00, 0x00)
#define C_SLOT   RGB(0x00, 0x60, 0x00)

typedef unsigned char u8;

/* card 0..51: suit = card/13 (0 spade,1 heart,2 diamond,3 club), rank = card%13 (0=A..12=K) */
typedef struct { u8 card[24]; u8 up[24]; int n; } Pile;
static Pile stock, waste, found[4], tab[7];

static int sel_kind = -1;   /* -1 none, 0 waste, 2 tableau */
static int sel_pile;        /* tableau column when sel_kind==2 */
static int sel_idx;         /* index within that pile (start of the moved run) */

static HWND g_win;            /* the single Solitaire window (Win32 veneer) */
static u32 s_rng = 0x2468ace0;
static unsigned rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

static int suit_of(int c) { return c / 13; }
static int rank_of(int c) { return c % 13; }
static int is_red(int c)  { int s = suit_of(c); return s == 1 || s == 2; }

static void deal(void) {
    for (int i = 0; i < 4; i++) found[i].n = 0;
    waste.n = 0; stock.n = 0; sel_kind = -1;
    u8 deck[52]; for (int i = 0; i < 52; i++) deck[i] = (u8)i;
    s_rng ^= sys_uptime_s() * 2654435761u + 1;
    for (int i = 51; i > 0; i--) { int j = (int)(rnd() % (unsigned)(i + 1)); u8 t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
    int d = 0;
    for (int col = 0; col < 7; col++) {
        tab[col].n = 0;
        for (int k = 0; k <= col; k++) {
            tab[col].card[tab[col].n] = deck[d++];
            tab[col].up[tab[col].n] = (k == col);   /* only the top card face-up */
            tab[col].n++;
        }
    }
    while (d < 52) { stock.card[stock.n] = deck[d++]; stock.up[stock.n] = 0; stock.n++; }
}

/* ---- geometry ---- */
static int ox, oy;   /* client origin, captured in paint */
static int col_x(int c) { return ox + GAP + c * (CW + COLGAP); }
static int top_y(void)  { return oy + GAP; }
static int tab_y(void)  { return oy + GAP + CH + 14; }
static int found_x(int i) { return col_x(3 + i); }

/* ---- card drawing ---- */
static void pip(int x, int y, int suit) {
    u32 col = (suit == 1 || suit == 2) ? C_RED : C_BLK;
    if (suit == 2) {   /* diamond */
        for (int r = 0; r < 5; r++) { int wd = r < 3 ? r + 1 : 5 - r; fb_fill_rect((u32)(x - wd), (u32)(y - 2 + r), (u32)(2 * wd + 1), 1, col); }
    } else if (suit == 1) {   /* heart */
        fb_fill_rect((u32)x - 3, (u32)y - 2, 3, 3, col); fb_fill_rect((u32)x + 1, (u32)y - 2, 3, 3, col);
        for (int r = 0; r < 4; r++) { int wd = 3 - r; fb_fill_rect((u32)(x - wd), (u32)(y + r), (u32)(2 * wd + 1), 1, col); }
    } else if (suit == 0) {   /* spade */
        for (int r = 0; r < 4; r++) { int wd = r; fb_fill_rect((u32)(x - wd), (u32)(y - 2 + r), (u32)(2 * wd + 1), 1, col); }
        fb_fill_rect((u32)x - 3, (u32)y + 1, 3, 2, col); fb_fill_rect((u32)x + 1, (u32)y + 1, 3, 2, col);
        fb_fill_rect((u32)x - 1, (u32)y + 2, 3, 3, col);   /* stem */
    } else {   /* club */
        fb_fill_rect((u32)x - 1, (u32)y - 3, 3, 3, col);
        fb_fill_rect((u32)x - 4, (u32)y, 3, 3, col); fb_fill_rect((u32)x + 2, (u32)y, 3, 3, col);
        fb_fill_rect((u32)x - 1, (u32)y + 2, 3, 3, col);   /* stem */
    }
}
static const char *rank_str(int r) {
    static const char *R[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
    return R[r];
}
static void draw_card(int x, int y, int c, int up) {
    if (!up) {
        fb_fill_rect((u32)x, (u32)y, CW, CH, C_BACK);
        wm_bevel_raised(x, y, CW, CH);
        for (int gy = 6; gy < CH - 6; gy += 6)
            for (int gx = 6; gx < CW - 6; gx += 6) fb_pixel((u32)(x + gx), (u32)(y + gy), RGB(0x80, 0x90, 0xF0));
        return;
    }
    fb_fill_rect((u32)x, (u32)y, CW, CH, C_CARD);
    fb_rect((u32)x, (u32)y, CW, CH, RGB(0x60, 0x60, 0x60));
    u32 col = is_red(c) ? C_RED : C_BLK;
    fb_text_t((u32)x + 3, (u32)y + 2, rank_str(rank_of(c)), col);
    pip(x + 8, y + 20, suit_of(c));         /* corner pip */
    pip(x + CW / 2, y + CH / 2 + 4, suit_of(c));  /* centre pip */
}
static void draw_slot(int x, int y) {
    fb_fill_rect((u32)x, (u32)y, CW, CH, C_SLOT);
    fb_rect((u32)x, (u32)y, CW, CH, RGB(0x00, 0x40, 0x00));
}

static void paint(int cx, int cy, int cw, int ch) {
    ox = cx; oy = cy;
    fb_fill_rect(cx, cy, cw, ch, C_BAIZE);
    /* stock */
    int sx = col_x(0), sy = top_y();
    if (stock.n > 0) draw_card(sx, sy, stock.card[stock.n - 1], 0); else draw_slot(sx, sy);
    /* waste */
    int wx = col_x(1);
    if (waste.n > 0) draw_card(wx, sy, waste.card[waste.n - 1], 1); else draw_slot(wx, sy);
    /* foundations */
    for (int i = 0; i < 4; i++) {
        int fx = found_x(i);
        if (found[i].n > 0) draw_card(fx, sy, found[i].card[found[i].n - 1], 1); else draw_slot(fx, sy);
    }
    /* tableau */
    for (int c = 0; c < 7; c++) {
        int x = col_x(c), y = tab_y();
        if (tab[c].n == 0) { draw_slot(x, y); continue; }
        for (int i = 0; i < tab[c].n; i++) {
            int sel = (sel_kind == 2 && sel_pile == c && i >= sel_idx);
            if (sel) fb_fill_rect((u32)x - 2, (u32)y - 2, CW + 4, CH + 4, RGB(0xF0, 0xF0, 0x00));
            draw_card(x, y, tab[c].card[i], tab[c].up[i]);
            y += tab[c].up[i] ? OVER_UP : OVER_DN;
        }
    }
    /* selected waste highlight */
    if (sel_kind == 0 && waste.n > 0) fb_rect((u32)wx - 2, (u32)sy - 2, CW + 4, CH + 4, RGB(0xF0, 0xF0, 0x00));
}

/* ---- rules ---- */
static int can_to_found(int c, int f) {
    if (found[f].n == 0) return rank_of(c) == 0 && suit_of(c) == f;
    int top = found[f].card[found[f].n - 1];
    return suit_of(c) == f && rank_of(c) == rank_of(top) + 1;
}
static int can_to_tab(int c, int col) {
    if (tab[col].n == 0) return rank_of(c) == 12;   /* only a King on an empty column */
    int top = tab[col].card[tab[col].n - 1];
    if (!tab[col].up[tab[col].n - 1]) return 0;
    return (is_red(c) != is_red(top)) && rank_of(c) == rank_of(top) - 1;
}
static void flip_expose(int col) {   /* turn the new top card of a tableau face-up */
    if (tab[col].n > 0 && !tab[col].up[tab[col].n - 1]) tab[col].up[tab[col].n - 1] = 1;
}

/* move the selected card/run onto tableau column `col` (or foundation f>=0) */
static void move_selected_to_tab(int col) {
    if (sel_kind == 0) {                       /* waste -> tableau (single) */
        int c = waste.card[waste.n - 1];
        if (can_to_tab(c, col)) { tab[col].card[tab[col].n] = (u8)c; tab[col].up[tab[col].n] = 1; tab[col].n++; waste.n--; sel_kind = -1; }
    } else if (sel_kind == 2) {                /* tableau run -> tableau */
        int from = sel_pile, start = sel_idx;
        int c = tab[from].card[start];
        if (from != col && can_to_tab(c, col)) {
            for (int i = start; i < tab[from].n; i++) {
                tab[col].card[tab[col].n] = tab[from].card[i];
                tab[col].up[tab[col].n] = 1; tab[col].n++;
            }
            tab[from].n = start; flip_expose(from); sel_kind = -1;
        }
    }
}
static void move_selected_to_found(int f) {
    int c = -1;
    if (sel_kind == 0 && waste.n > 0) c = waste.card[waste.n - 1];
    else if (sel_kind == 2 && sel_idx == tab[sel_pile].n - 1) c = tab[sel_pile].card[sel_idx];  /* only a single top card */
    else return;
    if (c < 0 || !can_to_found(c, f)) return;
    found[f].card[found[f].n] = (u8)c; found[f].n++;
    if (sel_kind == 0) waste.n--; else { tab[sel_pile].n--; flip_expose(sel_pile); }
    sel_kind = -1;
}

static void on_click(int rx, int ry) {
    int sx = ox + rx, sy = oy + ry;   /* absolute */
    int trow = top_y();
    /* stock */
    if (sx >= col_x(0) && sx < col_x(0) + CW && sy >= trow && sy < trow + CH) {
        sel_kind = -1;
        if (stock.n > 0) { waste.card[waste.n] = stock.card[stock.n - 1]; waste.up[waste.n] = 1; waste.n++; stock.n--; }
        else { while (waste.n > 0) { stock.card[stock.n] = waste.card[--waste.n]; stock.up[stock.n] = 0; stock.n++; } }
        return;
    }
    /* waste (select) */
    if (sx >= col_x(1) && sx < col_x(1) + CW && sy >= trow && sy < trow + CH && waste.n > 0) {
        sel_kind = (sel_kind == 0) ? -1 : 0; return;
    }
    /* foundations (destination) */
    for (int i = 0; i < 4; i++)
        if (sx >= found_x(i) && sx < found_x(i) + CW && sy >= trow && sy < trow + CH) {
            if (sel_kind >= 0) move_selected_to_found(i);
            return;
        }
    /* tableau */
    for (int c = 0; c < 7; c++) {
        int x = col_x(c);
        if (sx < x || sx >= x + CW) continue;
        int y = tab_y();
        if (tab[c].n == 0) { if (sy >= y && sy < y + CH && sel_kind >= 0) move_selected_to_tab(c); return; }
        /* find the card index under the cursor (account for overlap; last card is tallest) */
        int hitidx = -1, yy = y;
        for (int i = 0; i < tab[c].n; i++) {
            int step = (i == tab[c].n - 1) ? CH : (tab[c].up[i] ? OVER_UP : OVER_DN);
            if (sy >= yy && sy < yy + step) { hitidx = i; break; }
            yy += step;
        }
        if (hitidx < 0) return;
        if (sel_kind >= 0) {                   /* a selection exists: treat as destination */
            move_selected_to_tab(c);
            return;
        }
        if (tab[c].up[hitidx]) { sel_kind = 2; sel_pile = c; sel_idx = hitidx; }  /* pick up a run */
        return;
    }
    sel_kind = -1;   /* clicked empty baize */
}

/* ---- Win32 veneer: a single WndProc + WM_COMMAND menu ---- */
enum { IDM_DEAL = 1, IDM_EXIT = 2, IDM_ABOUT = 10 };
static const MenuItemW32 MG_game[] = { { "Deal", IDM_DEAL }, { "", -1 }, { "Exit", IDM_EXIT } };
static const MenuItemW32 MG_help[] = { { "About Solitaire", IDM_ABOUT } };
static const MenuW32 SOL_MENUS[] = { { "Game", MG_game, 3 }, { "Help", MG_help, 1 } };

static LRESULT CALLBACK Sol_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, SOL_MENUS, 2);
            SetWindowIcon(hwnd, IC_APP);
            g_win = hwnd; deal();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: on_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_DEAL: deal(); break;
                case IDM_ABOUT: MessageBoxW32(hwnd,
                    "Debian95 Solitaire\nKlondike - click to move cards and build the\nfoundations up by suit.",
                    "About Solitaire", MB_ICONINFO); break;
                case IDM_EXIT: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void solitaire_open(void) {
    if (g_win) return;                                    /* single instance */
    int w = GAP * 2 + 7 * (CW + COLGAP) - COLGAP + 6;
    CreateAppWindow(120, 80, w, 420, "Solitaire", Sol_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 12, "Solitaire", IC_APP, 0, solitaire_open, -1 };
#endif
