/* Debian95 — a small from-scratch web browser (managed WM window). It fetches a
 * real page over the HTTP stack (www_get), strips HTML to text while keeping
 * block breaks + links, word-wraps it into the window with a live address bar,
 * and lets you click links to navigate. Arch-neutral: same source on x86-64 and
 * ARM64. Not Chrome - a Dillo-class text browser, but it browses the real web. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/krt.h>

extern int www_get(const char *hostport, const char *path, char *out, int max);

#define RAWMAX  48000
#define TEXTMAX 48000
#define MAXLINK 128
#define MAXRECT 600

static char g_url[220] = "example.com/";
static int  g_urllen;
static char g_raw[RAWMAX];
static char g_text[TEXTMAX];
static int  g_textlen;
static char g_status[64] = "type a URL and press Enter";

static struct { int start, end; char href[180]; } g_link[MAXLINK];
static int g_nlink;
static struct { int x, y, w, h, idx; } g_rect[MAXRECT];
static int g_nrect;

static HWND g_win;            /* the single Browser window (Win32 veneer) */

/* ---- small string helpers ---- */
static int  lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int  taghas(const char *tag, const char *name) {
    for (int i = 0; name[i]; i++) if (lower(tag[i]) != name[i]) return 0;
    char e = tag[(int)strlen(name)];
    return e == 0 || e == ' ' || e == '>' || e == '/' || e == '\t' || e == '\n';
}
static int  isblock(const char *t) {
    static const char *b[] = { "br", "/p", "p", "/div", "div", "/h1", "h1", "/h2", "h2",
        "/h3", "h3", "li", "/tr", "tr", "hr", "/li", "/ul", "/table", 0 };
    for (int i = 0; b[i]; i++) if (taghas(t, b[i])) return 1;
    return 0;
}

/* decode a small set of HTML entities starting at &... ; advance *pp past it. */
static char entity(const char **pp) {
    const char *p = *pp + 1; char name[10]; int n = 0;
    while (*p && *p != ';' && n < 9 && *p != ' ') name[n++] = (char)lower(*p++);
    name[n] = 0;
    char out = 0;
    if      (taghas(name, "amp"))  out = '&';
    else if (taghas(name, "lt"))   out = '<';
    else if (taghas(name, "gt"))   out = '>';
    else if (taghas(name, "quot")) out = '"';
    else if (taghas(name, "nbsp")) out = ' ';
    else if (taghas(name, "#39") || taghas(name, "apos")) out = '\'';
    else return 0;                          /* unknown: leave '&' as-is */
    if (*p == ';') p++;
    *pp = p - 1;
    return out;
}

static void emit(char c) { if (g_textlen < TEXTMAX - 1) g_text[g_textlen++] = c; }

/* strip the HTTP headers, parse the HTML body into g_text (+ g_link ranges). */
static void parse(void) {
    g_textlen = 0; g_nlink = 0;
    const char *body = g_raw;
    for (int i = 0; g_raw[i] && i < RAWMAX - 4; i++)
        if (g_raw[i] == '\r' && g_raw[i+1] == '\n' && g_raw[i+2] == '\r' && g_raw[i+3] == '\n') { body = g_raw + i + 4; break; }

    int space = 1, skip = 0, linkopen = 0;
    for (const char *p = body; *p; p++) {
        if (*p == '<') {
            const char *t = p + 1;
            if (taghas(t, "script") || taghas(t, "style")) skip = 1;
            if (t[0] == '/' && (taghas(t + 1, "script") || taghas(t + 1, "style"))) skip = 0;
            if (!skip) {
                if (taghas(t, "a") && !taghas(t, "abbr") && !taghas(t, "address") && !taghas(t, "area")) {
                    const char *h = t;                    /* find href="..." */
                    while (*h && *h != '>') {
                        if ((h[0]=='h'||h[0]=='H') && lower(h[1])=='r' && lower(h[2])=='e' && lower(h[3])=='f') {
                            const char *v = h + 4; while (*v && *v != '"' && *v != '\'' && *v != '>') v++;
                            char q = *v; if (q == '"' || q == '\'') {
                                v++; int n = 0; if (g_nlink < MAXLINK) {
                                    while (*v && *v != q && n < 179) g_link[g_nlink].href[n++] = *v++;
                                    g_link[g_nlink].href[n] = 0;
                                    g_link[g_nlink].start = g_textlen; linkopen = 1;
                                }
                            }
                            break;
                        }
                        h++;
                    }
                } else if (t[0] == '/' && lower(t[1]) == 'a' && (t[2] == '>' || t[2] == ' ')) {
                    if (linkopen && g_nlink < MAXLINK) { g_link[g_nlink].end = g_textlen; g_nlink++; linkopen = 0; }
                } else if (isblock(t)) { if (g_textlen && g_text[g_textlen-1] != '\n') emit('\n'); space = 1; }
            }
            while (*p && *p != '>') p++;                   /* skip to tag end */
            continue;
        }
        if (skip) continue;
        char c = *p;
        if (c == '&') { char d = entity(&p); if (d) c = d; else continue; }
        if (c == '\r') continue;
        if (c == ' ' || c == '\t' || c == '\n') { if (!space) { emit(' '); space = 1; } continue; }
        emit(c); space = 0;
    }
    g_text[g_textlen] = 0;
}

static int in_link(int idx, int *which) {
    for (int i = 0; i < g_nlink; i++) if (idx >= g_link[i].start && idx < g_link[i].end) { *which = i; return 1; }
    return 0;
}

static void go(void) {
    char host[180], path[180]; int i = 0, j = 0;
    while (g_url[i] && g_url[i] != '/' && j < 179) host[j++] = g_url[i++]; host[j] = 0;
    if (!g_url[i]) { path[0] = '/'; path[1] = 0; } else { j = 0; while (g_url[i] && j < 179) path[j++] = g_url[i++]; path[j] = 0; }
    for (int k = 0; k < 24; k++) g_status[k] = "fetching..."[k];
    int n = www_get(host, path, g_raw, RAWMAX - 1);
    if (n <= 0) { const char *e = "fetch failed"; int k = 0; while (e[k]) { g_status[k] = e[k]; k++; } g_status[k] = 0; g_raw[0] = 0; g_textlen = 0; return; }
    g_raw[n] = 0;
    parse();
    char *s = g_status; const char *pre = "loaded "; int k = 0; while (pre[k]) { s[k] = pre[k]; k++; }
    /* append byte count */
    char num[12]; int m = 0, v = n; if (!v) num[m++] = '0'; char t2[12]; int tt = 0; while (v) { t2[tt++] = (char)('0'+v%10); v/=10; } while (tt) num[m++] = t2[--tt]; num[m] = 0;
    for (int q = 0; num[q]; q++) s[k++] = num[q]; s[k] = 0;
}

/* ---- rendering ---- */
static void con_rect(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, RGB(0xFF, 0xFF, 0xFF));
    int fh = font_height() + 3;
    int x = cx + 6, y = cy + 6, right = cx + cw - 8;
    g_nrect = 0;
    int i = 0;
    while (i < g_textlen && y < cy + ch - fh) {
        if (g_text[i] == '\n') { x = cx + 6; y += fh; i++; continue; }
        if (g_text[i] == ' ')  { x += 4; i++; continue; }
        int j = i; while (j < g_textlen && g_text[j] != ' ' && g_text[j] != '\n') j++;   /* one word */
        char w[128]; int wl = 0; for (int k = i; k < j && wl < 127; k++) w[wl++] = g_text[k]; w[wl] = 0;
        int px = (int)font_measure(w);
        if (x + px > right) { x = cx + 6; y += fh; }
        int link, is = in_link(i, &link);
        u32 col = is ? RGB(0x00, 0x00, 0xE0) : RGB(0x00, 0x00, 0x00);
        fb_text_t(x, y, w, col);
        if (is) { fb_fill_rect(x, y + fh - 3, px, 1, col);       /* underline */
                  if (g_nrect < MAXRECT) { g_rect[g_nrect].x = x; g_rect[g_nrect].y = y; g_rect[g_nrect].w = px; g_rect[g_nrect].h = fh; g_rect[g_nrect].idx = link; g_nrect++; } }
        x += px; i = j;
    }
}

static void paint(int cx, int cy, int cw, int ch) {
    /* address bar */
    fb_fill_rect(cx, cy, cw, 22, RGB(0xC0, 0xC0, 0xC0));
    fb_fill_rect(cx + 4, cy + 3, cw - 60, 16, RGB(0xFF, 0xFF, 0xFF));
    wm_bevel_sunken(cx + 4, cy + 3, cw - 60, 16);
    fb_text_t(cx + 8, cy + 5, g_url, RGB(0, 0, 0));
    fb_fill_rect(cx + cw - 52, cy + 3, 46, 16, RGB(0xC0, 0xC0, 0xC0));
    wm_bevel_raised(cx + cw - 52, cy + 3, 46, 16);
    fb_text_t(cx + cw - 44, cy + 5, "Go", RGB(0, 0, 0));
    /* content */
    con_rect(cx, cy + 24, cw, ch - 44);
    /* status bar */
    fb_fill_rect(cx, cy + ch - 18, cw, 18, RGB(0xC0, 0xC0, 0xC0));
    fb_text_t(cx + 6, cy + ch - 15, g_status, RGB(0x20, 0x20, 0x20));
}

static void onkey(char c) {
    if (c == '\n' || c == '\r') { g_url[g_urllen] = 0; go(); }
    else if (c == 8 || c == 0x7F) { if (g_urllen) g_url[--g_urllen] = 0; }
    else if (c >= 32 && c < 127 && g_urllen < (int)sizeof(g_url) - 1) { g_url[g_urllen++] = c; g_url[g_urllen] = 0; }
}

static void onclick(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch);
    if (ry >= 3 && ry < 19 && rx >= cw - 52 && rx < cw - 6) { go(); return; }   /* Go button */
    int ax = cx + rx, ay = cy + ry;                        /* link rects are absolute */
    for (int i = 0; i < g_nrect; i++) {
        if (ax >= g_rect[i].x && ax < g_rect[i].x + g_rect[i].w &&
            ay >= g_rect[i].y && ay < g_rect[i].y + g_rect[i].h) {
            const char *h = g_link[g_rect[i].idx].href;
            if (h[0] == 'h' && h[1] == 't') {          /* absolute http://host/path */
                const char *s = h; while (*s && *s != '/') s++; if (s[0]=='/'&&s[1]=='/') s += 2;
                g_urllen = 0; while (*s && g_urllen < (int)sizeof(g_url)-1) g_url[g_urllen++] = *s++; g_url[g_urllen] = 0;
            } else {                                    /* relative: keep host, replace path */
                int k = 0; while (g_url[k] && g_url[k] != '/') k++;   /* end of host */
                const char *r = h; int hs = 0;
                if (r[0] != '/') { while (g_url[k]) k++; while (k && g_url[k-1] != '/') k--; }  /* dir */
                while (r[hs] && k < (int)sizeof(g_url)-1) g_url[k++] = r[hs++]; g_url[k] = 0; g_urllen = k;
            }
            go(); return;
        }
    }
}

/* ---- Win32 veneer: WndProc ---- */
static LRESULT CALLBACK Browser_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetWindowTextCursor(hwnd);       /* I-beam over the address bar / page */
            g_win = hwnd;
            g_urllen = (int)strlen(g_url);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_CHAR:        onkey((char)wParam); return 0;
        case WM_LBUTTONDOWN: onclick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_DESTROY:     g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void browser_open(void) {
    if (g_win) return;                                    /* single instance */
    CreateAppWindow(120, 80, 620, 460, "Debian95 Browser", Browser_WndProc, 0);
}

/* open the browser ON a URL (taskbar Address bar, file associations). Strips an
 * http(s):// scheme the way the address field stores it, then navigates. */
void browser_open_url(const char *url) {
    if (url && url[0]) {
        const char *s = url;
        if ((s[0]=='h'||s[0]=='H') && (s[1]=='t'||s[1]=='T')) { while (*s && *s != '/') s++; if (s[0]=='/' && s[1]=='/') s += 2; }
        g_urllen = 0; while (*s && g_urllen < (int)sizeof(g_url) - 1) g_url[g_urllen++] = *s++; g_url[g_urllen] = 0;
    }
    if (!g_win) browser_open();
    if (g_win) go();
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 28, "Web Browser", IC_APP, 0, browser_open, -1, browser_open_url };
#endif
