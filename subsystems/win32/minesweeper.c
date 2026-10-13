/* Debian95 — Minesweeper (winmine look-alike).
 *
 * A faithful clean-room reimplementation of the classic Windows Minesweeper:
 * a 3D-bevelled field of covered tiles, a red 7-segment mine counter and
 * timer flanking a clickable smiley reset button, left-click to reveal with
 * flood-fill on empty cells, right-click to plant/clear a flag, first click
 * always safe. Beginner board (9x9, 10 mines). Rendered over the shared
 * win32 stack (fb/wm), so it looks identical on x86-64 and ARM64.
 *
 * The RE corpus (build/ghidra/winmine.ghidra.c) confirms the layout and
 * behavior; this code is original.
 */
#include <debian95/win32.h>   /* the Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>       /* wm_bevel_* used by the custom field/LED drawing */
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/icons.h>    /* IC_APP */

#define CELL   16
#define MAXN   (30 * 16)               /* Expert = 30x16 = the largest board */
static int COLS = 9, ROWS = 9, MINES = 10;   /* current difficulty (Beginner default) */
#define N      (COLS * ROWS)

#define C_FACE     RGB(0xC0, 0xC0, 0xC0)
#define C_HILIGHT  RGB(0xFF, 0xFF, 0xFF)
#define C_SHADOW   RGB(0x80, 0x80, 0x80)
#define C_DK       RGB(0x00, 0x00, 0x00)
#define C_LEDBG    RGB(0x00, 0x00, 0x00)
#define C_LEDON    RGB(0xFF, 0x00, 0x00)
#define C_LEDOFF   RGB(0x40, 0x00, 0x00)
#define C_SMILE    RGB(0xFF, 0xFF, 0x00)

extern unsigned sys_uptime_s(void);          /* portable seconds since boot */

/* number colors: 1..8 (classic winmine palette) */
static const u32 numclr[9] = {
    0,
    RGB(0x00, 0x00, 0xFF), RGB(0x00, 0x80, 0x00), RGB(0xFF, 0x00, 0x00),
    RGB(0x00, 0x00, 0x80), RGB(0x80, 0x00, 0x00), RGB(0x00, 0x80, 0x80),
    RGB(0x00, 0x00, 0x00), RGB(0x80, 0x80, 0x80),
};

/* ---- board state ---- */
static u8  s_mine[MAXN];   /* 1 = mine */
static u8  s_state[MAXN];  /* 0 covered, 1 revealed, 2 flagged */
static u8  s_adj[MAXN];    /* neighboring mine count */
static int s_placed;       /* mines laid (deferred until the first click) */
static int s_over;         /* 0 playing, 1 won, 2 lost */
static int s_boom = -1;    /* the mine that was clicked (drawn red) */
static int s_face;         /* 0 happy, 2 win, 3 dead */
static int s_started;      /* timer running */
static unsigned s_t0;      /* start second */
static unsigned s_elapsed; /* frozen elapsed on game over */
static u32 s_rng = 0x1337c0de;

static HWND g_ms;             /* the single Minesweeper window (Win32 veneer) */

static unsigned rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

static void reset_game(void) {
    for (int i = 0; i < N; i++) { s_mine[i] = 0; s_state[i] = 0; s_adj[i] = 0; }
    s_placed = 0; s_over = 0; s_boom = -1; s_face = 0; s_started = 0; s_elapsed = 0;
}

static void place_mines(int safe) {
    int placed = 0;
    s_rng ^= sys_uptime_s() * 2654435761u + 1;
    while (placed < MINES) {
        int i = rnd() % N;
        if (i == safe || s_mine[i]) continue;
        s_mine[i] = 1; placed++;
    }
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int i = r * COLS + c, n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = r + dr, cc = c + dc;
                    if (rr < 0 || rr >= ROWS || cc < 0 || cc >= COLS) continue;
                    if (s_mine[rr * COLS + cc]) n++;
                }
            s_adj[i] = (u8)n;
        }
    s_placed = 1;
}

static int flags_count(void) {
    int f = 0; for (int i = 0; i < N; i++) if (s_state[i] == 2) f++; return f;
}

static void reveal(int i) {
    if (i < 0 || i >= N || s_state[i]) return;   /* already revealed/flagged */
    s_state[i] = 1;
    if (s_mine[i]) { s_over = 2; s_boom = i; s_face = 3; s_elapsed = sys_uptime_s() - s_t0; return; }
    if (s_adj[i] == 0) {
        int r = i / COLS, c = i % COLS;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                int rr = r + dr, cc = c + dc;
                if (rr < 0 || rr >= ROWS || cc < 0 || cc >= COLS) continue;
                reveal(rr * COLS + cc);
            }
    }
}

static void check_win(void) {
    if (s_over) return;
    int hidden = 0;
    for (int i = 0; i < N; i++) if (!s_mine[i] && s_state[i] != 1) hidden++;
    if (hidden == 0) {
        s_over = 1; s_face = 2; s_elapsed = sys_uptime_s() - s_t0;
        for (int i = 0; i < N; i++) if (s_mine[i]) s_state[i] = 2;   /* auto-flag mines */
    }
}

/* ---- rendering ---- */
/* bit0=a top, 1=b tr, 2=c br, 3=d bottom, 4=e bl, 5=f tl, 6=g mid */
static const u8 seg7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static void seg(int x, int y, int w, int h, int on) {
    fb_fill_rect((u32)x, (u32)y, (u32)w, (u32)h, on ? C_LEDON : C_LEDOFF);
}
static void draw_digit(int x, int y, int mask) {
    seg(x + 2, y + 1,  9, 2, mask & 0x01);   /* a */
    seg(x + 10, y + 2, 2, 9, mask & 0x02);   /* b */
    seg(x + 10, y + 12,2, 9, mask & 0x04);   /* c */
    seg(x + 2, y + 21, 9, 2, mask & 0x08);   /* d */
    seg(x + 1, y + 12, 2, 9, mask & 0x10);   /* e */
    seg(x + 1, y + 2,  2, 9, mask & 0x20);   /* f */
    seg(x + 2, y + 11, 9, 2, mask & 0x40);   /* g */
}
static void draw_led3(int x, int y, int val) {
    fb_fill_rect((u32)x - 1, (u32)y - 1, 41, 25, C_LEDBG);
    int neg = val < 0; if (neg) val = -val; if (val > 999) val = 999;
    int d[3] = { (val / 100) % 10, (val / 10) % 10, val % 10 };
    for (int k = 0; k < 3; k++) {
        int dx = x + k * 13;
        int mask = seg7[d[k]];
        if (neg && k == 0) mask = 0x40;      /* leading minus */
        draw_digit(dx, y, mask);
    }
}

static void draw_smiley(int x, int y, int pressed) {
    fb_fill_rect((u32)x, (u32)y, 24, 24, C_FACE);
    if (pressed) { wm_bevel_sunken(x, y, 24, 24); } else { wm_bevel_raised(x, y, 24, 24); }
    int cx = x + 12, cy = y + 12;
    fb_fill_rect((u32)cx - 8, (u32)cy - 8, 16, 16, C_SMILE);   /* face (blocky circle) */
    fb_fill_rect((u32)cx - 9, (u32)cy - 6, 1, 12, C_SMILE);
    fb_fill_rect((u32)cx + 8, (u32)cy - 6, 1, 12, C_SMILE);
    fb_fill_rect((u32)cx - 6, (u32)cy - 9, 12, 1, C_SMILE);
    fb_fill_rect((u32)cx - 6, (u32)cy + 8, 12, 1, C_SMILE);
    fb_rect((u32)cx - 9, (u32)cy - 9, 18, 18, C_DK);
    if (s_face == 2) {                       /* win: sunglasses */
        fb_fill_rect((u32)cx - 6, (u32)cy - 3, 5, 3, C_DK);
        fb_fill_rect((u32)cx + 1, (u32)cy - 3, 5, 3, C_DK);
        fb_fill_rect((u32)cx - 1, (u32)cy - 2, 2, 1, C_DK);
    } else if (s_face == 3) {                /* dead: two X eyes */
        int ex[2] = { cx - 5, cx + 2 };
        for (int e = 0; e < 2; e++)
            for (int i = 0; i < 3; i++) {
                fb_pixel((u32)(ex[e] + i),     (u32)(cy - 4 + i), C_DK);  /* \ */
                fb_pixel((u32)(ex[e] + 2 - i), (u32)(cy - 4 + i), C_DK);  /* / */
            }
    } else {                                 /* happy */
        fb_fill_rect((u32)cx - 5, (u32)cy - 3, 2, 3, C_DK);
        fb_fill_rect((u32)cx + 3, (u32)cy - 3, 2, 3, C_DK);
    }
    /* mouth */
    if (s_face == 3) { fb_fill_rect((u32)cx - 4, (u32)cy + 4, 8, 1, C_DK); }
    else if (s_face == 2) { fb_fill_rect((u32)cx - 4, (u32)cy + 3, 8, 1, C_DK);
                            fb_fill_rect((u32)cx - 4, (u32)cy + 3, 1, 2, C_DK);
                            fb_fill_rect((u32)cx + 3, (u32)cy + 3, 1, 2, C_DK); }
    else { fb_fill_rect((u32)cx - 3, (u32)cy + 4, 6, 1, C_DK);
           fb_fill_rect((u32)cx - 4, (u32)cy + 3, 1, 1, C_DK);
           fb_fill_rect((u32)cx + 3, (u32)cy + 3, 1, 1, C_DK); }
}

static void draw_mine(int x, int y, int red) {
    if (red) fb_fill_rect((u32)x, (u32)y, CELL, CELL, C_LEDON);
    int cx = x + 8, cy = y + 8;
    fb_fill_rect((u32)cx - 4, (u32)cy - 4, 8, 8, C_DK);
    fb_fill_rect((u32)cx - 5, (u32)cy - 2, 10, 4, C_DK);
    fb_fill_rect((u32)cx - 2, (u32)cy - 5, 4, 10, C_DK);
    fb_fill_rect((u32)cx - 6, (u32)cy, 12, 1, C_DK);   /* spikes */
    fb_fill_rect((u32)cx, (u32)cy - 6, 1, 12, C_DK);
    fb_fill_rect((u32)cx - 3, (u32)cy - 3, 2, 2, C_HILIGHT);  /* glint */
}

static void draw_flag(int x, int y) {
    fb_fill_rect((u32)x + 8, (u32)y + 3, 1, 8, C_DK);        /* pole */
    fb_fill_rect((u32)x + 5, (u32)y + 11, 7, 2, C_DK);       /* base */
    fb_fill_rect((u32)x + 6, (u32)y + 10, 5, 1, C_DK);
    /* red pennant to the left of the pole (widest at the pole) */
    static const int wdt[5] = { 1, 3, 5, 3, 1 };
    for (int i = 0; i < 5; i++)
        fb_fill_rect((u32)(x + 8 - wdt[i]), (u32)(y + 2 + i), (u32)wdt[i], 1, C_LEDON);
}

static void paint(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int px = cx + 8, py = cy + 8;
    int fieldw = COLS * CELL, fieldh = ROWS * CELL;

    fb_fill_rect((u32)cx, (u32)cy, (u32)cw, (u32)ch, C_FACE);

    /* status panel */
    int panh = 32;
    wm_bevel_sunken(px, py, fieldw, panh);
    int mines_left = MINES - flags_count();
    draw_led3(px + 6, py + 4, mines_left);
    int t = s_started ? (int)(s_over ? s_elapsed : (sys_uptime_s() - s_t0)) : 0;
    draw_led3(px + fieldw - 46, py + 4, t);
    draw_smiley(px + fieldw / 2 - 12, py + 4, 0);

    /* minefield well */
    int fy = py + panh + 6;
    wm_bevel_sunken(px, fy, fieldw, fieldh);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int i = r * COLS + c;
            int x = px + c * CELL, y = fy + r * CELL;
            int show_mine = s_over == 2 && s_mine[i] && s_state[i] != 2;
            if (s_state[i] == 1 || show_mine) {
                fb_fill_rect((u32)x, (u32)y, CELL, CELL, C_FACE);
                fb_fill_rect((u32)x, (u32)y, CELL, 1, C_SHADOW);   /* recessed grid */
                fb_fill_rect((u32)x, (u32)y, 1, CELL, C_SHADOW);
                if (s_mine[i]) draw_mine(x, y, i == s_boom);
                else if (s_adj[i]) {
                    char s[2] = { (char)('0' + s_adj[i]), 0 };
                    fb_text_t((u32)(x + 5), (u32)(y + 3), s, numclr[s_adj[i]]);
                }
            } else {
                fb_fill_rect((u32)x, (u32)y, CELL, CELL, C_FACE);
                wm_bevel_raised(x, y, CELL, CELL);
                if (s_state[i] == 2) {
                    if (s_over == 2 && !s_mine[i]) {           /* wrong flag: mine + red X */
                        draw_mine(x, y, 0);
                        fb_fill_rect((u32)x + 2, (u32)y + 2, 12, 2, C_LEDON);
                    } else draw_flag(x, y);
                }
            }
        }
}

/* map a client point to smiley / cell; returns cell index or -2 for smiley, -1 none */
static int hit(int rx, int ry, int cx0) {
    int px = 8, py = 8, fieldw = COLS * CELL;
    int sx = px + fieldw / 2 - 12, sy = py + 4;
    (void)cx0;
    if (rx >= sx && rx < sx + 24 && ry >= sy && ry < sy + 24) return -2;
    int fy = py + 32 + 6;
    if (rx < px || rx >= px + fieldw || ry < fy || ry >= fy + ROWS * CELL) return -1;
    int c = (rx - px) / CELL, r = (ry - fy) / CELL;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return -1;
    return r * COLS + c;
}

static void on_left(int rx, int ry) {
    int h = hit(rx, ry, 0);
    if (h == -2) { reset_game(); return; }
    if (h < 0 || s_over) return;
    if (s_state[h] == 2) return;                 /* flagged: protected */
    if (!s_placed) place_mines(h);
    if (!s_started) { s_started = 1; s_t0 = sys_uptime_s(); }
    reveal(h);
    check_win();
}

static void on_right(int rx, int ry) {
    int h = hit(rx, ry, 0);
    if (h < 0 || s_over) return;
    if (s_state[h] == 1) return;                 /* revealed: no flag */
    s_state[h] = (s_state[h] == 2) ? 0 : 2;      /* toggle flag */
}

static void ms_size(int *w, int *h) {
    *w = COLS * CELL + 16 + 6;
    *h = ROWS * CELL + 32 + 6 + 16 + 6 + WM_TITLEH + 3 + 16;   /* +16 for the menu bar */
}
static void set_diff(int c, int r, int m) {
    COLS = c; ROWS = r; MINES = m; reset_game();
    int w, h; ms_size(&w, &h); if (g_ms) SetWindowSize(g_ms, w, h);
}
/* ---- Win32 veneer: a single WndProc, WM_* messages, WM_COMMAND menu ---- */
enum { MI_NEW = 1, MI_BEG = 2, MI_INT = 3, MI_EXP = 4, MI_EXIT = 5, MI_ABOUT = 10 };
static const MenuItemW32 MG_game[] = {
    { "New", MI_NEW }, { "", -1 }, { "Beginner", MI_BEG }, { "Intermediate", MI_INT },
    { "Expert", MI_EXP }, { "", -1 }, { "Exit", MI_EXIT }
};
static const MenuItemW32 MG_help[] = { { "About Minesweeper", MI_ABOUT } };
static const MenuW32 MS_MENUS[] = { { "Game", MG_game, 7 }, { "Help", MG_help, 1 } };

static LRESULT CALLBACK Mine_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            reset_game();
            SetMenuW32(hwnd, MS_MENUS, 2);
            SetWindowIcon(hwnd, IC_APP);
            g_ms = hwnd;
            return 0;
        case WM_PAINT: {                                  /* View: draw the field via fb_* */
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);        /* sets the client clip */
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: on_left(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_RBUTTONDOWN: on_right(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case MI_NEW:  reset_game(); break;
                case MI_BEG:  set_diff(9, 9, 10); break;
                case MI_INT:  set_diff(16, 16, 40); break;
                case MI_EXP:  set_diff(30, 16, 99); break;
                case MI_ABOUT: MessageBoxW32(hwnd,
                    "Debian95 Minesweeper\nLeft-click reveals, right-click flags.\nThe first click is always safe.",
                    "About Minesweeper", MB_ICONINFO); break;
                case MI_EXIT: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY: g_ms = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void minesweeper_open(void) {
    if (g_ms) return;                                     /* single instance */
    int w, h; ms_size(&w, &h);
    CreateAppWindow(150, 90, w, h, "Minesweeper", Mine_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
/* Built as a hot-swappable app module (minesweeper.so). See debian95/appsdk.h. */
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 9, "Minesweeper", IC_APP, 0, minesweeper_open, -1 };
#endif
