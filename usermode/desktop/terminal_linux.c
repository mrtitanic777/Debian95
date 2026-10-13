/* Debian95 desktop - a real terminal window. Runs the system shell (/bin/sh) on a
 * PTY and renders its output in a win32 WM window, sending keystrokes back. A
 * small VT subset (newline/CR/backspace/tab, ESC[ CSI stripping, basic erase) is
 * enough for sh, ls, cat, vi-less workflows. This is the Debian95 "shell", now
 * backed by the real Linux userland instead of a hand-written command table. */
#define _GNU_SOURCE
#include <debian95/types.h>
#include <debian95/fb.h>
#include <debian95/wm.h>
#include <debian95/font.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>

#define COLS 64
#define ROWS 20

static Window *g_win;
static int   g_pty = -1;
static pid_t g_child = -1;
static char  g_grid[ROWS][COLS];
static u32   g_fgc[ROWS][COLS];        /* per-cell colour (basic ANSI) */
static int   g_cr, g_cc;               /* cursor row/col */
static u32   g_curfg = 0x00FF00;       /* current fg (green default) */
static int   g_esc;                    /* inside an ESC[ sequence */

#define CELLW 7

static void clear_row(int r) { for (int c = 0; c < COLS; c++) { g_grid[r][c] = 0; g_fgc[r][c] = g_curfg; } }
static void scroll_up(void) {
    for (int r = 1; r < ROWS; r++) { memcpy(g_grid[r-1], g_grid[r], COLS); memcpy(g_fgc[r-1], g_fgc[r], sizeof g_fgc[r]); }
    clear_row(ROWS - 1);
    if (g_cr > 0) g_cr--;
}
static void newline(void) { g_cc = 0; if (++g_cr >= ROWS) { g_cr = ROWS - 1; scroll_up(); } }
static void putch(char ch) {
    if (ch == '\n') { newline(); return; }
    if (ch == '\r') { g_cc = 0; return; }
    if (ch == '\b') { if (g_cc > 0) g_cc--; return; }
    if (ch == '\t') { g_cc = (g_cc + 8) & ~7; if (g_cc >= COLS) newline(); return; }
    if (ch == 7) return;                                   /* bell */
    if ((unsigned char)ch < 32) return;
    if (g_cc >= COLS) newline();
    g_grid[g_cr][g_cc] = ch; g_fgc[g_cr][g_cc] = g_curfg; g_cc++;
}

/* feed one byte through the tiny VT parser */
static void feed(char ch) {
    if (g_esc) {
        /* crude CSI handling: track colour on 'm', clear on 'J', stop at a letter */
        static char seq[32]; static int sl;
        if (g_esc == 1) { if (ch == '[') { g_esc = 2; sl = 0; } else g_esc = 0; return; }
        if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') { if (sl < 31) seq[sl++] = ch; return; }
        seq[sl] = 0;
        if (ch == 'm') {                                   /* SGR colour */
            int code = atoi(seq);
            switch (code) {
                case 0: g_curfg = 0x00FF00; break;         /* reset -> green */
                case 30: g_curfg = 0x000000; break; case 31: g_curfg = 0xFF5555; break;
                case 32: g_curfg = 0x55FF55; break; case 33: g_curfg = 0xFFFF55; break;
                case 34: g_curfg = 0x5555FF; break; case 35: g_curfg = 0xFF55FF; break;
                case 36: g_curfg = 0x55FFFF; break; case 37: g_curfg = 0xFFFFFF; break;
                default: break;
            }
        } else if (ch == 'J' || ch == 'H') {               /* clear / home */
            if (ch == 'J') { for (int r = 0; r < ROWS; r++) clear_row(r); }
            g_cr = g_cc = 0;
        }
        g_esc = 0; return;
    }
    if (ch == 27) { g_esc = 1; return; }
    putch(ch);
}

static void paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, 0x000018);                /* navy-black console */
    int fh = font_height() + 2;
    for (int r = 0; r < ROWS && (r + 1) * fh < ch; r++)
        for (int c = 0; c < COLS; c++) {
            char g = g_grid[r][c]; if (!g) continue;
            char s[2] = { g, 0 };
            fb_text(cx + 4 + c * CELLW, cy + 2 + r * fh, s, g_fgc[r][c], 0x000018);
        }
    /* cursor block */
    int fhh = font_height();
    fb_fill_rect(cx + 4 + g_cc * CELLW, cy + 2 + g_cr * fh, CELLW, fhh, 0x00AA00);
}

static void onkey(char c) {
    if (g_pty >= 0) { if (c == '\b') c = 127; write(g_pty, &c, 1); }
}
static void term_close(void) {
    if (g_child > 0) kill(g_child, SIGKILL);
    if (g_pty >= 0) close(g_pty);
    g_pty = -1; g_child = -1; g_win = 0;
}

static const char *g_pending_cmd;        /* a command to run on open (e.g. the installer) */
void terminal_run(const char *cmd) {     /* open the terminal and run a command in it */
    if (g_win && g_pty >= 0 && cmd && *cmd) { wm_show(g_win); write(g_pty, cmd, strlen(cmd)); write(g_pty, "\n", 1); return; }
    g_pending_cmd = cmd; terminal_open();
}
void terminal_open(void) {
    if (g_win) { wm_show(g_win); return; }
    for (int r = 0; r < ROWS; r++) clear_row(r);
    g_cr = g_cc = 0;
    struct winsize ws = { ROWS, COLS, 0, 0 };
    g_child = forkpty(&g_pty, 0, 0, &ws);
    if (g_child < 0) { const char *m = "forkpty failed\n"; for (const char *p = m; *p; p++) feed(*p); }
    else if (g_child == 0) {
        extern void sandbox_nnp(void); sandbox_nnp();   /* no privilege escalation from the shell */
        setenv("TERM", "ansi", 1); setenv("PS1", "debian95$ ", 1);
        execl("/bin/sh", "sh", "-i", (char *)0);
        execl("/bin/busybox", "sh", "-i", (char *)0);
        _exit(127);
    } else {
        fcntl(g_pty, F_SETFL, O_NONBLOCK);
        const char *cmd = g_pending_cmd ? g_pending_cmd : getenv("DEBIAN95_TERMCMD");
        if (cmd && *cmd) { write(g_pty, cmd, strlen(cmd)); write(g_pty, "\n", 1); }
        g_pending_cmd = 0;
    }
    g_win = wm_add_custom(60, 150, COLS * CELLW + 12, ROWS * (font_height() + 2) + 8,
                          "Debian95 Terminal", paint, onkey, 0);
    wm_set_close(g_win, term_close);
}

/* called once per frame by the platform main loop: drain the PTY. Returns 1 if
 * the window needs a repaint. */
int terminal_tick(void) {
    if (g_pty < 0) return 0;
    char buf[4096]; int dirty = 0, n;
    while ((n = (int)read(g_pty, buf, sizeof buf)) > 0) { for (int i = 0; i < n; i++) feed(buf[i]); dirty = 1; }
    /* reap the shell if it exited */
    if (g_child > 0 && waitpid(g_child, 0, WNOHANG) == g_child) { g_child = -1; }
    return dirty;
}
