/* Debian95 — Reversi (Othello). An authentic board game: you play Black, the
 * computer plays White with a greedy + corner-preferring heuristic. Flips,
 * passing, and end-of-game detection are all real. Ships as an app module. */
#include <debian95/win32.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/icons.h>
#include <stdio.h>

#define N     8
#define CELL  30
#define BX    8          /* board offset inside the client area */
#define BY    28
static HWND g_win;
static int  g_b[64];     /* 0 empty, 1 black (you), 2 white (computer) */
static int  g_over;

static const int DX[8] = { -1,-1,-1, 0, 0, 1, 1, 1 };
static const int DY[8] = { -1, 0, 1,-1, 1,-1, 0, 1 };
static int opp(int c) { return c == 1 ? 2 : 1; }

static int flips_at(const int *b, int r, int col, int c) {   /* # flipped if c plays here; 0 = illegal */
    if (r < 0 || r >= N || col < 0 || col >= N || b[r*N+col]) return 0;
    int tot = 0;
    for (int d = 0; d < 8; d++) {
        int rr = r + DY[d], cc = col + DX[d], cnt = 0;
        while (rr >= 0 && rr < N && cc >= 0 && cc < N && b[rr*N+cc] == opp(c)) { rr += DY[d]; cc += DX[d]; cnt++; }
        if (cnt && rr >= 0 && rr < N && cc >= 0 && cc < N && b[rr*N+cc] == c) tot += cnt;
    }
    return tot;
}
static void play(int *b, int r, int col, int c) {
    b[r*N+col] = c;
    for (int d = 0; d < 8; d++) {
        int rr = r + DY[d], cc = col + DX[d], cnt = 0;
        while (rr >= 0 && rr < N && cc >= 0 && cc < N && b[rr*N+cc] == opp(c)) { rr += DY[d]; cc += DX[d]; cnt++; }
        if (cnt && rr >= 0 && rr < N && cc >= 0 && cc < N && b[rr*N+cc] == c) {
            rr = r + DY[d]; cc = col + DX[d];
            for (int k = 0; k < cnt; k++) { b[rr*N+cc] = c; rr += DY[d]; cc += DX[d]; }
        }
    }
}
static int has_move(const int *b, int c) {
    for (int r = 0; r < N; r++) for (int col = 0; col < N; col++) if (flips_at(b, r, col, c)) return 1;
    return 0;
}
static void counts(int *blk, int *wht) { *blk = *wht = 0; for (int i = 0; i < 64; i++) { if (g_b[i]==1)(*blk)++; else if (g_b[i]==2)(*wht)++; } }

static void ai_move(void) {                                  /* White: most flips, corners weighted */
    static const int corner[4] = { 0, 7, 56, 63 };
    int best = -1, bi = -1;
    for (int r = 0; r < N; r++) for (int col = 0; col < N; col++) {
        int f = flips_at(g_b, r, col, 2); if (!f) continue;
        int idx = r*N+col, sc = f;
        for (int k = 0; k < 4; k++) if (idx == corner[k]) sc += 20;
        if (sc > best) { best = sc; bi = idx; }
    }
    if (bi >= 0) play(g_b, bi/N, bi%N, 2);
}
static void new_game(void) {
    for (int i = 0; i < 64; i++) g_b[i] = 0;
    g_b[3*N+3] = 2; g_b[3*N+4] = 1; g_b[4*N+3] = 1; g_b[4*N+4] = 2;   /* standard opening */
    g_over = 0;
}
static void result_box(void) {
    int bl, wh; counts(&bl, &wh);
    char m[64]; const char *who = bl > wh ? "You win!" : wh > bl ? "The computer wins." : "A tie.";
    snprintf(m, sizeof m, "%s\nBlack %d  -  White %d", who, bl, wh);
    MessageBoxW32(g_win, m, "Reversi", 0);
}

static void draw_disc(int cx, int cy, u32 col) {
    int r = CELL/2 - 4;
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++)
        if (x*x + y*y <= r*r) fb_pixel((u32)(cx+x), (u32)(cy+y), col);
}
static void paint(int ox, int oy, int cw, int ch) {
    fb_fill_rect((u32)ox, (u32)oy, (u32)cw, (u32)ch, RGB(192,192,192));
    int bl, wh; counts(&bl, &wh);
    char s[48]; snprintf(s, sizeof s, "Black (you): %d    White: %d", bl, wh);
    fb_text_t((u32)(ox+8), (u32)(oy+6), s, RGB(0,0,0));
    if (g_over) fb_text_t((u32)(ox+8), (u32)(oy+6), s, RGB(0,0,0));
    int gx = ox + BX, gy = oy + BY;
    fb_fill_rect((u32)gx, (u32)gy, N*CELL, N*CELL, RGB(0,110,0));         /* felt */
    for (int i = 0; i <= N; i++) {
        fb_fill_rect((u32)gx, (u32)(gy+i*CELL), N*CELL+1, 1, RGB(0,60,0));
        fb_fill_rect((u32)(gx+i*CELL), (u32)gy, 1, N*CELL+1, RGB(0,60,0));
    }
    for (int r = 0; r < N; r++) for (int col = 0; col < N; col++) {
        int v = g_b[r*N+col]; if (!v) continue;
        draw_disc(gx + col*CELL + CELL/2, gy + r*CELL + CELL/2, v == 1 ? RGB(0,0,0) : RGB(255,255,255));
    }
    wm_bevel_sunken(gx-1, gy-1, N*CELL+2, N*CELL+2);
}

static void on_click(int rx, int ry) {
    if (g_over) return;
    int col = (rx - BX) / CELL, r = (ry - BY) / CELL;
    if (rx < BX || ry < BY || col < 0 || col >= N || r < 0 || r >= N) return;
    if (!flips_at(g_b, r, col, 1)) return;                   /* illegal: ignore */
    play(g_b, r, col, 1);
    for (;;) {                                               /* resolve White's reply + any passes */
        if (!has_move(g_b, 1) && !has_move(g_b, 2)) { g_over = 1; break; }
        if (has_move(g_b, 2)) ai_move();
        if (has_move(g_b, 1)) break;                         /* your turn again */
        if (!has_move(g_b, 2)) { g_over = 1; break; }        /* you passed, White also stuck */
    }
    InvalidateRect(g_win);
    if (g_over) result_box();
}

#define IDM_NEW  1
#define IDM_EXIT 2
#define IDM_ABOUT 3
static const MenuItemW32 MI_game[] = { { "New Game", IDM_NEW }, { "", -1 }, { "Exit", IDM_EXIT } };
static const MenuItemW32 MI_help[] = { { "About Reversi", IDM_ABOUT } };
static const MenuW32 MENUS[] = { { "Game", MI_game, 3 }, { "Help", MI_help, 1 } };

static LRESULT CALLBACK Reversi_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetMenuW32(hwnd, MENUS, 2); SetWindowIcon(hwnd, IC_APP); g_win = hwnd; return 0;
        case WM_LBUTTONDOWN: on_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch); EndPaint(hwnd, ps.hdc); return 0; }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_NEW:  new_game(); InvalidateRect(hwnd); break;
                case IDM_EXIT: DestroyWindow(hwnd); break;
                case IDM_ABOUT: MessageBoxW32(hwnd, "Reversi\nDebian95 Desktop Edition", "About", 0); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void reversi_open(void) {
    if (g_win) return;
    new_game();
    CreateAppWindow(180, 100, BX*2 + N*CELL + 8, BY + N*CELL + 16 + 24, "Reversi", Reversi_WndProc, 0);
    { char *getenv(const char *); if (getenv("DEBIAN95_REVERSIDEMO")) on_click(BX + 3*CELL + CELL/2, BY + 2*CELL + CELL/2); }  /* test: play d3, AI replies */
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 62, "Reversi", IC_APP, 0, reversi_open, -1, 0 };
#endif
