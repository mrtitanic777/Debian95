/* Debian95 — Paint: an MS Paint-style editor with the authentic Win95 two-column
 * tool box (16 tools), the 28-colour palette, and a File/Help menu bar. Pencil,
 * Brush, Airbrush, Eraser and Fill draw freehand; Line, Rectangle, Ellipse and
 * Rounded-Rect rubber-band a live preview and commit on release. The canvas is a
 * persistent pixel buffer so it survives desktop recomposites.
 */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/icons.h>
#include <debian95/fb.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include <debian95/nt.h>

#define CW 360
#define CH 240
#define TOOLW 56                 /* 2 columns of 25px tool buttons + margins */

enum { T_SELF, T_SEL, T_ERASER, T_FILL, T_PICK, T_ZOOM, T_PENCIL, T_BRUSH,
       T_AIR, T_TEXT, T_LINE, T_CURVE, T_RECT, T_POLY, T_ELLIPSE, T_RRECT, NTOOL };

static HWND g_win;            /* the single Paint window (Win32 veneer) */
static u32    *g_canvas;                 /* CW*CH pixels */
static u32    *g_snap;                    /* snapshot for shape rubber-banding */
static u32     g_color = RGB(0x00, 0x00, 0x00);
static int     g_tool = T_PENCIL;

/* the Win95 default palette: 28 swatches (two rows of 14) */
static const u32 g_palette[28] = {
    RGB(0x00,0x00,0x00), RGB(0x80,0x80,0x80), RGB(0x80,0x00,0x00), RGB(0x80,0x80,0x00),
    RGB(0x00,0x80,0x00), RGB(0x00,0x80,0x80), RGB(0x00,0x00,0x80), RGB(0x80,0x00,0x80),
    RGB(0x80,0x80,0x40), RGB(0x00,0x40,0x40), RGB(0x00,0x80,0xFF), RGB(0x00,0x40,0x80),
    RGB(0x40,0x00,0xFF), RGB(0x80,0x40,0x00),
    RGB(0xFF,0xFF,0xFF), RGB(0xC0,0xC0,0xC0), RGB(0xFF,0x00,0x00), RGB(0xFF,0xFF,0x00),
    RGB(0x00,0xFF,0x00), RGB(0x00,0xFF,0xFF), RGB(0x00,0x00,0xFF), RGB(0xFF,0x00,0xFF),
    RGB(0xFF,0xFF,0x80), RGB(0x00,0xFF,0x80), RGB(0x80,0xFF,0xFF), RGB(0x80,0x80,0xFF),
    RGB(0xFF,0x00,0x80), RGB(0xFF,0x80,0x40),
};

static void canvas_clear(void) { for (int i = 0; i < CW * CH; i++) g_canvas[i] = RGB(0xFF, 0xFF, 0xFF); }

/* client-relative canvas origin */
static int canvas_ox(void) { return TOOLW + 6; }
static int canvas_oy(void) { return 4; }

/* a compact 16x16-ish glyph for each tool, drawn at (x,y) */
static void tool_glyph(int x, int y, int t) {
    u32 c = RGB(0x20, 0x20, 0x20);
    int cx = x + 12, cy = y + 12;
    switch (t) {
    case T_SELF:  for (int i=0;i<16;i++){ fb_pixel((u32)(x+4+i%2*0),0,c);} /* dashed lasso */
                  for (int i=0;i<14;i+=2){ fb_pixel((u32)(x+5+i),(u32)(y+5),c); fb_pixel((u32)(x+5+i),(u32)(y+18),c); fb_pixel((u32)(x+5),(u32)(y+5+i),c); fb_pixel((u32)(x+18),(u32)(y+5+i),c);} break;
    case T_SEL:   for (int i=0;i<14;i+=2){ fb_pixel((u32)(x+5+i),(u32)(y+5),c); fb_pixel((u32)(x+5+i),(u32)(y+18),c); fb_pixel((u32)(x+5),(u32)(y+5+i),c); fb_pixel((u32)(x+18),(u32)(y+5+i),c);} break;
    case T_ERASER: fb_fill_rect((u32)x+5,(u32)y+8,12,8, RGB(0xF0,0xF0,0xF0)); wm_bevel_raised(x+5,y+8,12,8); break;
    case T_FILL:  fb_fill_rect((u32)x+6,(u32)y+5,9,8,c); fb_fill_rect((u32)x+5,(u32)y+13,12,3, RGB(0x20,0x60,0xF0)); break;
    case T_PICK:  for (int i=0;i<10;i++) fb_pixel((u32)(x+6+i),(u32)(y+16-i),c); fb_fill_rect((u32)x+14,(u32)y+5,3,3,c); break;
    case T_ZOOM:  fb_rect(x+5,y+5,8,8,c); for (int i=0;i<5;i++) fb_pixel((u32)(x+12+i),(u32)(y+12+i),c); break;
    case T_PENCIL: for (int i=0;i<12;i++) fb_pixel((u32)(x+5+i),(u32)(y+17-i),c); fb_fill_rect((u32)x+15,(u32)y+4,3,3,c); break;
    case T_BRUSH: for (int i=0;i<9;i++){ fb_pixel((u32)(x+6+i),(u32)(y+15-i),c); fb_pixel((u32)(x+7+i),(u32)(y+15-i),c);} fb_fill_rect((u32)x+4,(u32)y+13,4,4,c); break;
    case T_AIR:   for (int i=0;i<10;i++){ int a=(i*37)%11, b=(i*53)%11; fb_pixel((u32)(x+7+a),(u32)(y+7+b),c);} fb_fill_rect((u32)x+4,(u32)y+15,3,3,c); break;
    case T_TEXT:  fb_fill_rect((u32)x+5,(u32)y+5,10,2,c); fb_fill_rect((u32)x+9,(u32)y+5,2,12,c); break;
    case T_LINE:  for (int i=0;i<13;i++) fb_pixel((u32)(x+5+i),(u32)(y+17-i),c); break;
    case T_CURVE: for (int i=0;i<13;i++) fb_pixel((u32)(x+5+i),(u32)(y+14-((i*(13-i))/12)),c); break;
    case T_RECT:  fb_rect(x+5,y+7,13,9,c); break;
    case T_POLY:  fb_pixel((u32)cx,(u32)(y+5),c); for(int i=0;i<7;i++){fb_pixel((u32)(x+5+i),(u32)(y+16),c);fb_pixel((u32)(x+18-i),(u32)(y+16),c);} break;
    case T_ELLIPSE: for (int a=0;a<360;a+=20){ int px=cx+(int)(6*((a%180)<90?1:-1)); (void)px; } fb_rect(x+5,y+8,13,7,c); fb_pixel((u32)(x+5),(u32)(y+7),RGB(0xC0,0xC0,0xC0)); fb_pixel((u32)(x+17),(u32)(y+7),RGB(0xC0,0xC0,0xC0)); break;
    case T_RRECT: fb_rect(x+5,y+7,13,9,c); fb_pixel((u32)(x+5),(u32)(y+7),RGB(0xC0,0xC0,0xC0)); fb_pixel((u32)(x+17),(u32)(y+7),RGB(0xC0,0xC0,0xC0)); fb_pixel((u32)(x+5),(u32)(y+15),RGB(0xC0,0xC0,0xC0)); fb_pixel((u32)(x+17),(u32)(y+15),RGB(0xC0,0xC0,0xC0)); break;
    }
}

static int is_shape(int t) { return t == T_LINE || t == T_RECT || t == T_ELLIPSE || t == T_RRECT; }

static void paint_paint(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    fb_fill_rect(cx, cy, cw, ch, RGB(0xC0, 0xC0, 0xC0));
    /* tool box: 2 columns x 8 rows */
    for (int i = 0; i < NTOOL; i++) {
        int col = i % 2, row = i / 2;
        int tx = cx + 3 + col * 25, ty = cy + 4 + row * 25;
        fb_fill_rect(tx, ty, 24, 24, RGB(0xC0, 0xC0, 0xC0));
        if (i == g_tool) wm_bevel_sunken(tx, ty, 24, 24); else wm_bevel_raised(tx, ty, 24, 24);
        tool_glyph(tx, ty, i);
    }
    /* canvas */
    int ox = cx + canvas_ox(), oy = cy + canvas_oy();
    wm_bevel_sunken(ox - 1, oy - 1, CW + 2, CH + 2);
    fb_blit(ox, oy, CW, CH, g_canvas);
    /* palette (2 rows x 14) below the canvas, with the current-colour swatch */
    int py = oy + CH + 8, px = ox + 34;
    fb_fill_rect(ox, py, 30, 30, g_color); wm_bevel_sunken(ox, py, 30, 30);
    for (int i = 0; i < 28; i++) {
        int sx = px + (i % 14) * 15, sy = py + (i / 14) * 15;
        fb_fill_rect(sx, sy, 13, 13, g_palette[i]);
        wm_bevel_sunken(sx, sy, 13, 13);
    }
}

static void plot(int x, int y, int r, u32 c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx >= 0 && yy >= 0 && xx < CW && yy < CH) g_canvas[yy * CW + xx] = c;
        }
}

static void flood_fill(int x, int y, u32 to) {
    if (x < 0 || y < 0 || x >= CW || y >= CH) return;
    u32 from = g_canvas[y * CW + x];
    if (from == to) return;
    int *st = (int *)kmalloc(CW * CH * sizeof(int));
    if (!st) return;
    int sp = 0; st[sp++] = y * CW + x;
    while (sp > 0) {
        int p = st[--sp];
        if (g_canvas[p] != from) continue;
        g_canvas[p] = to;
        int qx = p % CW, qy = p / CW;
        if (qx > 0)      st[sp++] = p - 1;
        if (qx < CW - 1) st[sp++] = p + 1;
        if (qy > 0)      st[sp++] = p - CW;
        if (qy < CH - 1) st[sp++] = p + CW;
    }
    kfree(st);
}

/* -- primitives used by the shape tools (drawn straight into g_canvas) -- */
static void cv_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady; if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) { int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        if (x >= 0 && y >= 0 && x < CW && y < CH) g_canvas[y * CW + x] = c; }
}
static void cv_rect(int x0, int y0, int x1, int y1, u32 c) {
    cv_line(x0, y0, x1, y0, c); cv_line(x0, y1, x1, y1, c);
    cv_line(x0, y0, x0, y1, c); cv_line(x1, y0, x1, y1, c);
}
static void cv_ellipse(int x0, int y0, int x1, int y1, u32 c) {
    int ax = (x0 + x1) / 2, ay = (y0 + y1) / 2;
    int rx = (x1 - x0) / 2, ry = (y1 - y0) / 2; if (rx < 0) rx = -rx; if (ry < 0) ry = -ry;
    if (rx == 0 || ry == 0) { cv_line(x0, y0, x1, y1, c); return; }
    int prevx = ax + rx, prevy = ay;
    for (int a = 6; a <= 360; a += 6) {
        /* cheap sine/cosine via a small table-free approximation */
        int deg = a % 360; int q = deg / 90; int t = deg % 90;
        /* sin(t) approx (t in degrees) using quarter-symmetry, integer 0..1000 */
        int s = (t * (18000 - 100 * t)) / 8100;   /* ~sin scaled to 0..1000 */
        int cN = (( (90 - t) * (18000 - 100 * (90 - t))) / 8100);
        int sinv, cosv;
        if (q == 0) { sinv = s; cosv = cN; }
        else if (q == 1) { sinv = cN; cosv = -s; }
        else if (q == 2) { sinv = -s; cosv = -cN; }
        else { sinv = -cN; cosv = s; }
        int px = ax + rx * cosv / 1000, py = ay + ry * sinv / 1000;
        cv_line(prevx, prevy, px, py, c); prevx = px; prevy = py;
    }
}
static void draw_shape(int x0, int y0, int x1, int y1) {
    if (g_tool == T_LINE) cv_line(x0, y0, x1, y1, g_color);
    else if (g_tool == T_RECT || g_tool == T_RRECT) cv_rect(x0, y0, x1, y1, g_color);
    else if (g_tool == T_ELLIPSE) cv_ellipse(x0, y0, x1, y1, g_color);
}

/* discrete click: tool box, palette, or a fill */
static void paint_click(int rx, int ry) {
    /* tool box (2 columns) */
    if (rx >= 3 && rx < 53) {
        int col = (rx - 3) / 25, row = (ry - 4) / 25, i = row * 2 + col;
        if (col >= 0 && col < 2 && (rx - 3) % 25 < 24 && (ry - 4) % 25 < 24 && i >= 0 && i < NTOOL) { g_tool = i; return; }
    }
    int ox = canvas_ox(), oy = canvas_oy();
    int py = oy + CH + 8, px = ox + 34;
    /* palette */
    if (ry >= py && ry < py + 30) {
        int col = (rx - px) / 15, row = (ry - py) / 15, i = row * 14 + col;
        if (col >= 0 && col < 14 && i >= 0 && i < 28) { g_color = g_palette[i]; return; }
    }
    /* fill / colour picker on the canvas */
    int x = rx - ox, y = ry - oy;
    if (x >= 0 && y >= 0 && x < CW && y < CH) {
        if (g_tool == T_FILL) flood_fill(x, y, g_color);
        else if (g_tool == T_PICK) g_color = g_canvas[y * CW + x];
    }
}

static int g_lx = -1, g_ly = -1;   /* last drawn point (for stroke interpolation) */
static int g_shx = -1, g_shy = -1; /* shape anchor */

/* drag: freehand for pencil/brush/eraser/airbrush; rubber-band for shapes */
static void paint_motion(int rx, int ry, u8 buttons) {
    int ox = canvas_ox(), oy = canvas_oy();
    int x = rx - ox, y = ry - oy;
    if (!(buttons & 1)) {                        /* button released */
        if (g_shx >= 0 && is_shape(g_tool)) {    /* commit the shape */
            int cx = x < 0 ? 0 : x >= CW ? CW - 1 : x, cy = y < 0 ? 0 : y >= CH ? CH - 1 : y;
            for (int i = 0; i < CW * CH; i++) g_canvas[i] = g_snap[i];
            draw_shape(g_shx, g_shy, cx, cy);
        }
        g_lx = g_ly = -1; g_shx = g_shy = -1; return;
    }
    if (g_tool == T_FILL || g_tool == T_PICK) return;
    if (is_shape(g_tool)) {
        if (x < 0) x = 0; if (y < 0) y = 0; if (x >= CW) x = CW - 1; if (y >= CH) y = CH - 1;
        if (g_shx < 0) { g_shx = x; g_shy = y; for (int i = 0; i < CW * CH; i++) g_snap[i] = g_canvas[i]; }
        else { for (int i = 0; i < CW * CH; i++) g_canvas[i] = g_snap[i]; draw_shape(g_shx, g_shy, x, y); }
        return;
    }
    if (x < 0 || y < 0 || x >= CW || y >= CH) { g_lx = g_ly = -1; return; }
    int r = g_tool == T_PENCIL ? 0 : g_tool == T_BRUSH ? 2 : g_tool == T_AIR ? 3 : g_tool == T_ERASER ? 4 : 1;
    u32 c = g_tool == T_ERASER ? RGB(0xFF, 0xFF, 0xFF) : g_color;
    if (g_lx >= 0) { int dx = x - g_lx, dy = y - g_ly, adx = dx<0?-dx:dx, ady = dy<0?-dy:dy, n = adx>ady?adx:ady; if (!n) n = 1;
        for (int i = 0; i <= n; i++) plot(g_lx + dx*i/n, g_ly + dy*i/n, r, c); }
    else plot(x, y, r, c);
    g_lx = x; g_ly = y;
}

/* menu bar */
enum { IDM_NEW = 1, IDM_EXIT = 2, IDM_ABOUT = 3, IDM_SAVE = 4, IDM_PRINT = 5 };
static const MenuItemW32 MP_file[] = { { "New", IDM_NEW }, { "Save", IDM_SAVE }, { "Print", IDM_PRINT }, { "", -1 }, { "Exit", IDM_EXIT } };
static const MenuItemW32 MP_help[] = { { "About Paint", IDM_ABOUT } };
static const MenuW32 PAINT_MENUS[] = { { "File", MP_file, 5 }, { "Help", MP_help, 1 } };
/* save the canvas as a real 24-bpp BMP on the Desktop (unique name) */
static void paint_save(void) {
    int w = CW, h = CH, rowb = (w * 3 + 3) & ~3; u32 sz = 54 + (u32)rowb * h;
    u8 *b = (u8 *)kmalloc(sz); if (!b) return;
    for (u32 i = 0; i < 54; i++) b[i] = 0;
    b[0] = 'B'; b[1] = 'M';
    b[2] = (u8)sz; b[3] = (u8)(sz >> 8); b[4] = (u8)(sz >> 16); b[5] = (u8)(sz >> 24);
    b[10] = 54; b[14] = 40;
    b[18] = (u8)w; b[19] = (u8)(w >> 8); b[22] = (u8)h; b[23] = (u8)(h >> 8);
    b[26] = 1; b[28] = 24;
    for (int y = 0; y < h; y++) { u8 *row = b + 54 + (u32)(h - 1 - y) * rowb;
        for (int x = 0; x < w; x++) { u32 c = g_canvas[y * w + x];
            row[x*3] = (u8)(c & 0xFF); row[x*3+1] = (u8)((c >> 8) & 0xFF); row[x*3+2] = (u8)((c >> 16) & 0xFF); } }
    FsMkdir("Desktop");
    char name[40], path[64]; FsEntry e[24]; int listed = FsListDir("Desktop", e, 24);
    for (int n = 1; n < 100; n++) {                       /* Painting.bmp, Painting (2).bmp, ... */
        int k = 0; const char *base = "Painting";
        for (const char *s = base; *s; s++) name[k++] = *s;
        if (n > 1) { name[k++] = ' '; name[k++] = '('; if (n >= 10) name[k++] = (char)('0'+n/10); name[k++] = (char)('0'+n%10); name[k++] = ')'; }
        const char *ext = ".bmp"; for (const char *s = ext; *s; s++) name[k++] = *s; name[k] = 0;
        int exists = 0; for (int i = 0; i < listed; i++) { int eq = 1; for (int j = 0;; j++) { char a=e[i].name[j],bb=name[j]; if(a>='A'&&a<='Z')a+=32; if(bb>='A'&&bb<='Z')bb+=32; if(a!=bb){eq=0;break;} if(!a)break;} if (eq) { exists = 1; break; } }
        if (!exists) break;
    }
    path[0] = 0; { int k = 0; const char *d = "Desktop/"; for (; d[k]; k++) path[k] = d[k]; for (int i = 0; name[i]; i++) path[k++] = name[i]; path[k] = 0; }
    int ok = FsWriteFile(path, b, sz) == 0; kfree(b);
    if (ok) { char msg[64]; int k = 0; const char *p = "Saved to Desktop\\"; for (; p[k]; k++) msg[k] = p[k]; for (int i = 0; name[i]; i++) msg[k++] = name[i]; msg[k] = 0; wm_messagebox("Paint", msg); }
    else wm_messagebox_ex("Paint", "Could not save the picture.", MB_ERROR);
}
/* ---- Win32 veneer: a single WndProc (drawing via WM_LBUTTONDOWN + WM_MOUSEMOVE,
 *      the latter carrying the button state so a release commits a shape) ---- */
static LRESULT CALLBACK Paint_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_canvas = (u32 *)kmalloc(CW * CH * 4);
            g_snap   = (u32 *)kmalloc(CW * CH * 4);
            canvas_clear();
            SetMenuW32(hwnd, PAINT_MENUS, 2);
            SetWindowIconMod(hwnd, "MSPAINT.EXE", 2);   /* the real MS Paint icon */
            g_win = hwnd;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: paint_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_MOUSEMOVE:   paint_motion(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (u8)wParam); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_NEW:  canvas_clear(); break;
                case IDM_SAVE: paint_save(); break;
                case IDM_PRINT: print_submit_bmp("Painting.bmp", g_canvas, CW, CH); break;
                case IDM_ABOUT: MessageBoxW32(hwnd,
                    "Debian95 Paint\nPencil, Brush, Airbrush, Eraser, Fill,\nLine, Rectangle, Ellipse. Click a colour, drag to draw.",
                    "About Paint", MB_ICONINFO); break;
                case IDM_EXIT: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY:
            if (g_canvas) { kfree(g_canvas); g_canvas = 0; }
            if (g_snap)   { kfree(g_snap);   g_snap = 0; }
            g_win = 0;
            return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void paint_open(void) {
    if (g_win) return;                                    /* single instance */
    int w = TOOLW + CW + 16;
    int h = CH + 8 + 30 + 8 + WM_TITLEH + 16 /*menu bar*/ + 8;
    CreateAppWindow(200, 110, w, h, "untitled - Paint", Paint_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 5, "Paint", 0, "MSPAINT.EXE", paint_open, -1 };
#endif
