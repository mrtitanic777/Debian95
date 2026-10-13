/* Debian95 — MS-DOS Prompt: a console window backed by the real file tree.
 * Commands: DIR, CD <dir>, CD.., VER, CLS, TYPE <file>, HELP, EXIT. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/nt.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>

#define MAXLN 240
#define LW    100
#define CELLW 8                       /* fixed cell width for the console look */

static HWND g_win;            /* the single MS-DOS Prompt window (Win32 veneer) */
static char g_lines[MAXLN][LW]; static int g_nln;
static char g_input[LW]; static int g_inlen;
static char g_cwd[128] = "";          /* current dir relative to the root */
static int  g_scroll;                 /* not used yet; always tail */

static void put_line(const char *s) {
    if (g_nln >= MAXLN) { for (int i = 1; i < MAXLN; i++) for (int j = 0; j < LW; j++) g_lines[i-1][j] = g_lines[i][j]; g_nln = MAXLN - 1; }
    int i = 0; for (; s[i] && i < LW - 1; i++) g_lines[g_nln][i] = s[i]; g_lines[g_nln][i] = 0; g_nln++;
}
static void cat(char *d, const char *s) { int l = 0; while (d[l]) l++; for (int i = 0; s[i]; i++) d[l++] = s[i]; d[l] = 0; }
static void cwd_disp(char *out) {                    /* "C:\CWD" with backslashes */
    out[0] = 0; cat(out, "C:\\");
    for (int i = 0; g_cwd[i]; i++) { char c = g_cwd[i] == '/' ? '\\' : g_cwd[i]; char s[2] = { c, 0 }; cat(out, s); }
}
static void utoa_(u32 v, char *b) { char t[12]; int i = 0; if (!v) { b[0]='0'; b[1]=0; return; }
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; } int j = 0; while (i) b[j++] = t[--i]; b[j] = 0; }
static int streq_ci(const char *a, const char *b) {
    for (int i = 0;; i++) { char x = a[i], y = b[i]; if (x>='a'&&x<='z') x-=32; if (y>='a'&&y<='z') y-=32; if (x != y) return 0; if (!x) return 1; }
}
static int starts_ci(const char *s, const char *p) {
    for (int i = 0; p[i]; i++) { char x = s[i], y = p[i]; if (x>='a'&&x<='z') x-=32; if (y>='a'&&y<='z') y-=32; if (x != y) return 0; } return 1;
}

static void cmd_dir(void) {
    FsEntry e[64]; int n = FsListDir(g_cwd, e, 64);
    char disp[140]; cwd_disp(disp);
    char h[160]; h[0]=0; cat(h, " Directory of "); cat(h, disp); put_line(h); put_line("");
    int nf = 0, nd = 0;
    for (int i = 0; i < n; i++) {
        char row[LW]; row[0] = 0; cat(row, e[i].name);
        int pad = 16 - (int)strlen(e[i].name); for (int k = 0; k < pad; k++) cat(row, " ");
        if (e[i].is_dir) { cat(row, "<DIR>"); nd++; }
        else { char sz[16]; utoa_(e[i].size, sz); int sp = 10 - (int)strlen(sz); for (int k=0;k<sp;k++) cat(row, " "); cat(row, sz); nf++; }
        put_line(row);
    }
    char f[80]; f[0]=0; char nb[12]; utoa_((u32)nf, nb); cat(f, "     "); cat(f, nb); cat(f, " file(s), "); utoa_((u32)nd, nb); cat(f, nb); cat(f, " dir(s)"); put_line(f);
}
static void cmd_type(const char *arg) {
    char path[160]; path[0] = 0; if (g_cwd[0]) { cat(path, g_cwd); cat(path, "/"); } cat(path, arg);
    u8 *data; u32 sz;
    if (FsReadAlloc(path, &data, &sz) != 0) { put_line("File not found"); return; }
    char line[LW]; int p = 0;
    for (u32 i = 0; i < sz; i++) { char c = (char)data[i];
        if (c == '\r') continue;
        if (c == '\n' || p >= LW - 1) { line[p] = 0; put_line(line); p = 0; if (c != '\n') line[p++] = c; }
        else line[p++] = c; }
    if (p) { line[p] = 0; put_line(line); }
    kfree(data);
}
static void cmd_cd(const char *arg) {
    if (!arg[0]) { char d[140]; cwd_disp(d); put_line(d); return; }
    if (streq_ci(arg, "..")) { int l = 0; while (g_cwd[l]) l++; while (l > 0 && g_cwd[l-1] != '/') l--; if (l>0) l--; g_cwd[l] = 0; return; }
    if (streq_ci(arg, "\\") || streq_ci(arg, "/")) { g_cwd[0] = 0; return; }
    /* verify the subdir exists */
    FsEntry e[64]; int n = FsListDir(g_cwd, e, 64);
    for (int i = 0; i < n; i++) if (e[i].is_dir && streq_ci(e[i].name, arg)) {
        int l = 0; while (g_cwd[l]) l++; if (l && g_cwd[l-1] != '/') g_cwd[l++] = '/';
        for (int k = 0; arg[k] && l < 126; k++) g_cwd[l++] = arg[k]; g_cwd[l] = 0; return;
    }
    put_line("The system cannot find the path specified.");
}
static void run_cmd(const char *c) {
    while (*c == ' ') c++;
    if (!*c) return;
    char verb[16]; int i = 0; for (; c[i] && c[i] != ' ' && i < 15; i++) verb[i] = c[i]; verb[i] = 0;
    const char *arg = c + i; while (*arg == ' ') arg++;
    if (streq_ci(verb, "DIR")) cmd_dir();
    else if (streq_ci(verb, "CD") || streq_ci(verb, "CHDIR")) cmd_cd(arg);
    else if (streq_ci(verb, "CLS")) { g_nln = 0; }
    else if (streq_ci(verb, "VER")) put_line("Debian95 [Version 1.0]");
    else if (streq_ci(verb, "TYPE")) cmd_type(arg);
    else if (streq_ci(verb, "HELP")) { put_line("DIR   CD    CLS   VER   TYPE   HELP   EXIT"); }
    else if (streq_ci(verb, "EXIT")) { DestroyWindow(g_win); }
    else { char b[LW]; b[0]=0; cat(b, "Bad command or file name: "); cat(b, verb); put_line(b); }
    (void)starts_ci;
}

static void md_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, RGB(0x00, 0x00, 0x00));
    int lh = (int)font_height() + 2;
    int rows = (ch - 6) / lh; if (rows < 1) rows = 1;
    /* the prompt line takes the last row */
    int shown = rows - 1;
    int start = g_nln - shown; if (start < 0) start = 0;
    int y = cy + 3;
    for (int i = start; i < g_nln; i++) {
        const char *s = g_lines[i];
        for (int k = 0; s[k]; k++) fb_char_t(cx + 4 + k * CELLW, y, s[k], RGB(0xC0, 0xC0, 0xC0));
        y += lh;
    }
    /* prompt + input + block cursor */
    char pr[140]; cwd_disp(pr); cat(pr, ">");
    int col = 0;
    for (int k = 0; pr[k]; k++, col++) fb_char_t(cx + 4 + col * CELLW, y, pr[k], RGB(0xC0, 0xC0, 0xC0));
    for (int k = 0; g_input[k]; k++, col++) fb_char_t(cx + 4 + col * CELLW, y, g_input[k], RGB(0xC0, 0xC0, 0xC0));
    fb_fill_rect(cx + 4 + col * CELLW, y, CELLW, (int)font_height(), RGB(0xC0, 0xC0, 0xC0));   /* cursor block */
}
static void md_key(char c) {
    if (c == '\r' || c == '\n') {
        char echo[160]; cwd_disp(echo); cat(echo, ">"); cat(echo, g_input); put_line(echo);
        run_cmd(g_input); g_input[0] = 0; g_inlen = 0; return;
    }
    if (c == 8 || c == 0x7F) { if (g_inlen) g_input[--g_inlen] = 0; return; }
    if (c >= 32 && g_inlen < LW - 2) { g_input[g_inlen++] = c; g_input[g_inlen] = 0; }
}
/* ---- Win32 veneer: WndProc (console: WM_PAINT draws, WM_CHAR feeds the line) ---- */
static LRESULT CALLBACK Msdos_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetWindowIconMod(hwnd, "PIFMGR.DLL", 1);   /* the real MS-DOS icon */
            g_win = hwnd;
            if (g_nln == 0) {
                put_line("Debian95 MS-DOS Prompt  [Version 1.0]");
                put_line("(C) Debian95.  Type HELP for a list of commands.");
                put_line("");
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            md_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_CHAR: md_key((char)wParam); return 0;
        case WM_DESTROY: g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void msdos_open(void) {
    (void)g_scroll;
    if (g_win) return;                                    /* single instance */
    CreateAppWindow(180, 110, 520, 330, "MS-DOS Prompt", Msdos_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 15, "MS-DOS Prompt", 0, "PIFMGR.DLL", msdos_open, -1 };
#endif
