/* debian95-wm - the Debian95 Desktop Edition window manager.
 *
 * A reparenting X11 window manager (Xlib) that wraps every application window in
 * classic Windows-95 chrome: a raised 3D border, a gradient title bar with the
 * window title, and minimize / maximize / close buttons. Drag the title bar to
 * move; the [X] closes via WM_DELETE_WINDOW (ICCCM) or XKillClient. This is what
 * turns downloaded apps (VLC, Chrome, xterm) into Win95-looking windows.
 *
 * Deliberately small and dependency-light (just libX11) so it builds inside the
 * Debian rootfs. Not a compositor - plain reparenting, like the real Win95 USER.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TITLEH 20          /* title bar height */
#define BORDER 4           /* 3D border thickness */
#define BTN    16          /* caption button size */
#define BTNGAP 2

static Display *dpy;
static Window   root;
static int      scr;
static GC       gc;
static XFontStruct *font;
static Atom     A_WM_DELETE, A_WM_PROTOCOLS;
/* EWMH: so the Debian95 shell can list our framed apps and drive them from its taskbar */
static Atom     A_NET_CLIENT_LIST, A_NET_ACTIVE_WINDOW, A_NET_CLOSE_WINDOW, A_WM_CHANGE_STATE;
/* the shell publishes its active theme's caption colours here; we read + repaint */
static Atom     A_DEBIAN95_THEME;
/* EWMH window-type / state so real apps' dialogs, splashes, menus and fullscreen
 * (e.g. VLC video) are handled correctly instead of all being framed the same. */
static Atom     A_NET_WM_WINDOW_TYPE, A_NET_WM_STATE, A_NET_WM_STATE_FULLSCREEN,
                A_NET_WM_STATE_SKIP_TASKBAR, A_NET_SUPPORTED;
static Atom     A_WT_DESKTOP, A_WT_DOCK, A_WT_SPLASH, A_WT_DIALOG, A_WT_UTILITY,
                A_WT_MENU, A_WT_DROPDOWN, A_WT_POPUP, A_WT_TOOLTIP, A_WT_NOTIF,
                A_WT_COMBO, A_WT_DND;
/* system tray (freedesktop System Tray Protocol + XEMBED): dock REAL Linux tray
 * icons (nm-applet, blueman-applet, ...) into the Win95 notification area. The
 * shell reserves the slots and publishes where they go; we own the selection,
 * embed the icon windows and lay them out there. */
static Atom     A_TRAY_SELECTION, A_TRAY_OPCODE, A_TRAY_ORIENTATION,
                A_MANAGER, A_XEMBED, A_XEMBED_INFO,
                A_DEBIAN95_TRAY_RECT, A_DEBIAN95_TRAY_COUNT;

/* palette. Bevel colours are fixed; caption colours are theme-driven and split
 * into ACTIVE (focused window) vs INACTIVE, so the focused frame stands out. The
 * title gradient endpoints are kept as raw 0xRRGGBB (interpolated per column);
 * text/face/desktop are allocated X pixels. */
static unsigned long c_face, c_white, c_light, c_shadow, c_dkshadow, c_black, c_desktop;
static unsigned long at_lo, at_hi, it_lo, it_hi;   /* active/inactive gradient (0xRRGGBB) */
static unsigned long at_text, it_text;             /* active/inactive caption text (pixels) */
static unsigned long c_closebtn, c_minmaxbtn;      /* XP caption-button colours (pixels) */
static int g_style;                                /* 0 classic (95/98), 1 XP (coloured buttons) */

typedef struct Client {
    Window client, frame;
    int cw, ch;            /* client size */
    int max;               /* maximized? */
    int sx, sy, sw, sh;    /* saved frame pos + client size for restore (max/fullscreen) */
    int shaded;            /* window-shaded (rolled up to the title bar)? */
    int dialog;            /* a transient/dialog: close-only chrome, centred, off the taskbar */
    int fullscreen;        /* _NET_WM_STATE_FULLSCREEN (chrome pushed off-screen) */
    struct Client *next;
} Client;
static Client *clients;
static Client *g_focus;    /* the focused window (its caption paints active) */
static KeyCode g_ktab;     /* Tab keycode, for the Alt+Tab grab */

/* system tray */
#define TRAY_ICON  16          /* Win95 notification-area icons are 16x16 */
#define TRAY_STEP  20
#define TRAY_MAXN  16
static Window g_tray_owner;     /* selection owner + XEMBED embedder (holds the icons) */
static Window g_tray_icons[TRAY_MAXN]; static int g_tray_n;
static int g_tray_rx = -1, g_tray_ry, g_tray_rw, g_tray_rh;   /* placement strip from the shell */

/* right-click title-bar system menu */
static Window  g_menu;     /* the popup window (override-redirect), 0 when closed */
static Client *g_menu_c;   /* the window it acts on */
static int     g_menu_hover = -1;
#define SM_W  120
#define SM_IH 18
static const char *SM_LABELS[] = { "Restore", "Minimize", "Maximize", "Close" };
#define SM_N 4

/* drag state */
static Client *drag;
static int drag_dx, drag_dy;

static unsigned long rgb(int r, int g, int b) {
    XColor c; c.red = r*257; c.green = g*257; c.blue = b*257; c.flags = DoRed|DoGreen|DoBlue;
    XAllocColor(dpy, DefaultColormap(dpy, scr), &c);
    return c.pixel;
}

static Client *find_frame(Window w) {
    for (Client *c = clients; c; c = c->next) if (c->frame == w) return c;
    return NULL;
}

/* ---- system tray (XEMBED) ---- */
#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_EMBEDDED_NOTIFY   0
static int  is_tray_icon(Window w) { for (int i = 0; i < g_tray_n; i++) if (g_tray_icons[i] == w) return 1; return 0; }
static void tray_publish_count(void) {
    unsigned long n = g_tray_n;
    XChangeProperty(dpy, root, A_DEBIAN95_TRAY_COUNT, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
}
static void tray_layout(void) {
    if (g_tray_n == 0) { XUnmapWindow(dpy, g_tray_owner); return; }
    int w = g_tray_n * TRAY_STEP, h = TRAY_ICON + 4;
    int x = (g_tray_rx >= 0) ? g_tray_rx : (DisplayWidth(dpy, scr) - w - 80);
    int y = (g_tray_rx >= 0) ? g_tray_ry : (DisplayHeight(dpy, scr) - 22);
    XMoveResizeWindow(dpy, g_tray_owner, x, y - 2, w, h);
    for (int i = 0; i < g_tray_n; i++)
        XMoveResizeWindow(dpy, g_tray_icons[i], i * TRAY_STEP + 2, 2, TRAY_ICON, TRAY_ICON);
    XMapWindow(dpy, g_tray_owner);
    XRaiseWindow(dpy, g_tray_owner);           /* sit on the taskbar, above app frames */
}
static void send_xembed(Window w, long msg, long d1, long d2) {
    XClientMessageEvent e; memset(&e, 0, sizeof e);
    e.type = ClientMessage; e.window = w; e.message_type = A_XEMBED; e.format = 32;
    e.data.l[0] = CurrentTime; e.data.l[1] = msg; e.data.l[2] = d1; e.data.l[3] = d2;
    XSendEvent(dpy, w, False, NoEventMask, (XEvent *)&e);
}
static void tray_dock(Window w) {
    if (g_tray_n >= TRAY_MAXN || is_tray_icon(w)) return;
    XSelectInput(dpy, w, StructureNotifyMask | PropertyChangeMask);
    XReparentWindow(dpy, w, g_tray_owner, 2, 2);
    XResizeWindow(dpy, w, TRAY_ICON, TRAY_ICON);
    send_xembed(w, XEMBED_EMBEDDED_NOTIFY, 0, g_tray_owner);
    XMapWindow(dpy, w);
    g_tray_icons[g_tray_n++] = w;
    tray_publish_count(); tray_layout();
}
static void tray_undock(Window w) {
    int i; for (i = 0; i < g_tray_n; i++) if (g_tray_icons[i] == w) break;
    if (i == g_tray_n) return;
    for (int j = i + 1; j < g_tray_n; j++) g_tray_icons[j - 1] = g_tray_icons[j];
    g_tray_n--;
    tray_publish_count(); tray_layout();
}
static void read_tray_rect(void) {
    Atom t; int f; unsigned long n = 0, after; unsigned char *d = 0;
    if (XGetWindowProperty(dpy, root, A_DEBIAN95_TRAY_RECT, 0, 4, False, XA_CARDINAL,
                           &t, &f, &n, &after, &d) == Success && d && n >= 4) {
        unsigned long *v = (unsigned long *)d;
        g_tray_rx = (int)v[0]; g_tray_ry = (int)v[1]; g_tray_rw = (int)v[2]; g_tray_rh = (int)v[3];
        tray_layout();
    }
    if (d) XFree(d);
}
static void tray_init(void) {
    g_tray_owner = XCreateSimpleWindow(dpy, root, -200, -200, 1, TRAY_ICON + 4, 0, c_face, c_face);
    long orient = 0;                                  /* _NET_SYSTEM_TRAY_ORIENTATION_HORZ */
    XChangeProperty(dpy, g_tray_owner, A_TRAY_ORIENTATION, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&orient, 1);
    XSetSelectionOwner(dpy, A_TRAY_SELECTION, g_tray_owner, CurrentTime);
    if (XGetSelectionOwner(dpy, A_TRAY_SELECTION) == g_tray_owner) {   /* announce the tray to clients */
        XClientMessageEvent m; memset(&m, 0, sizeof m);
        m.type = ClientMessage; m.window = root; m.message_type = A_MANAGER; m.format = 32;
        m.data.l[0] = CurrentTime; m.data.l[1] = A_TRAY_SELECTION; m.data.l[2] = g_tray_owner;
        XSendEvent(dpy, root, False, StructureNotifyMask, (XEvent *)&m);
    }
    tray_publish_count();
}

/* publish _NET_CLIENT_LIST (the framed app windows) on the root, for the taskbar */
static void update_client_list(void) {
    Window buf[64]; int n = 0;
    for (Client *c = clients; c && n < 64; c = c->next) if (!c->dialog) buf[n++] = c->client;  /* dialogs stay off the taskbar */
    XChangeProperty(dpy, root, A_NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)buf, n);
}
static Client *find_client(Window w) {
    for (Client *c = clients; c; c = c->next) if (c->client == w) return c;
    return NULL;
}

/* ---- Win95 3D bevels ---- */
static void hline(Window w, int x, int y, int len, unsigned long col) {
    XSetForeground(dpy, gc, col); XDrawLine(dpy, w, gc, x, y, x+len, y);
}
static void vline(Window w, int x, int y, int len, unsigned long col) {
    XSetForeground(dpy, gc, col); XDrawLine(dpy, w, gc, x, y, x, y+len);
}
static void bevel_raised(Window w, int x, int y, int bw, int bh) {
    hline(w, x, y, bw-1, c_white);        vline(w, x, y, bh-1, c_white);
    hline(w, x+1, y+1, bw-3, c_light);    vline(w, x+1, y+1, bh-3, c_light);
    hline(w, x, y+bh-1, bw-1, c_dkshadow);vline(w, x+bw-1, y, bh-1, c_dkshadow);
    hline(w, x+1, y+bh-2, bw-2, c_shadow);vline(w, x+bw-2, y+1, bh-2, c_shadow);
}
static void bevel_sunken(Window w, int x, int y, int bw, int bh) {
    hline(w, x, y, bw-1, c_shadow);       vline(w, x, y, bh-1, c_shadow);
    hline(w, x, y+bh-1, bw-1, c_white);   vline(w, x+bw-1, y, bh-1, c_white);
}

/* draw one caption button (raised face with a glyph): k = 'x','o','_' */
static void caption_button(Window w, int x, int y, char k) {
    unsigned long glyph = c_black;
    if (g_style != 0) {                              /* XP/Flat: coloured button, white glyph */
        unsigned long bg = (k == 'x') ? c_closebtn : c_minmaxbtn;
        XSetForeground(dpy, gc, bg); XFillRectangle(dpy, w, gc, x, y, BTN, BTN);
        XSetForeground(dpy, gc, c_white);            /* glossy top-left highlight */
        XDrawLine(dpy, w, gc, x+1, y+1, x+BTN-2, y+1);
        XSetForeground(dpy, gc, c_dkshadow);
        XDrawRectangle(dpy, w, gc, x, y, BTN-1, BTN-1);
        glyph = c_white;
    } else {                                         /* classic 95/98: gray raised face */
        XSetForeground(dpy, gc, c_face); XFillRectangle(dpy, w, gc, x, y, BTN, BTN);
        bevel_raised(w, x, y, BTN, BTN);
    }
    XSetForeground(dpy, gc, glyph);
    int cx = x + BTN/2, cy = y + BTN/2;
    if (k == 'x') {
        for (int i = -3; i <= 3; i++) {
            XDrawPoint(dpy, w, gc, cx+i, cy+i);   XDrawPoint(dpy, w, gc, cx+i+1, cy+i);
            XDrawPoint(dpy, w, gc, cx-i, cy+i);   XDrawPoint(dpy, w, gc, cx-i+1, cy+i);
        }
    } else if (k == '_') {
        XFillRectangle(dpy, w, gc, cx-4, y+BTN-6, 8, 2);
    } else { /* 'o' maximize */
        XDrawRectangle(dpy, w, gc, cx-5, cy-5, 10, 9);
        XDrawLine(dpy, w, gc, cx-5, cy-4, cx+5, cy-4);
    }
}

/* frame geometry helpers */
static void frame_size(Client *c, int *fw, int *fh) {
    *fw = c->cw + 2*BORDER;
    *fh = c->ch + TITLEH + 2*BORDER;
}
static void draw_frame(Client *c) {
    int fw, fh; frame_size(c, &fw, &fh);
    if (c->shaded) fh = TITLEH + 2*BORDER;   /* rolled up: only the title bar shows */
    /* border */
    XSetForeground(dpy, gc, c_face); XFillRectangle(dpy, c->frame, gc, 0, 0, fw, fh);
    bevel_raised(c->frame, 0, 0, fw, fh);
    /* title bar with a left->right gradient - active (focused) vs inactive colours */
    int active = (c == g_focus);
    unsigned long lo = active ? at_lo : it_lo, hi = active ? at_hi : it_hi;
    unsigned long tcol = active ? at_text : it_text;
    int tx = BORDER, ty = BORDER, tw = fw - 2*BORDER, th = TITLEH;
    for (int i = 0; i < tw; i++) {
        int t = (i*255)/(tw>1?tw-1:1);
        int r = ((lo>>16&0xff)*(255-t) + (hi>>16&0xff)*t)/255;
        int g = ((lo>>8 &0xff)*(255-t) + (hi>>8 &0xff)*t)/255;
        int b = ((lo    &0xff)*(255-t) + (hi    &0xff)*t)/255;
        XSetForeground(dpy, gc, rgb(r,g,b));
        XDrawLine(dpy, c->frame, gc, tx+i, ty, tx+i, ty+th-1);
    }
    /* title text */
    char *name = NULL;
    if (XFetchName(dpy, c->client, &name) && name) {
        XSetForeground(dpy, gc, tcol);
        if (font) XSetFont(dpy, gc, font->fid);
        int yy = ty + (th + (font?font->ascent-font->descent:10))/2;
        XDrawString(dpy, c->frame, gc, tx+6, yy, name, strlen(name));
        XFree(name);
    }
    /* caption buttons: [_][o][x] at the right */
    int by = ty + (th-BTN)/2, bx = tx + tw - (BTN+BTNGAP);
    caption_button(c->frame, bx, by, 'x');
    if (!c->dialog) {                                 /* dialogs: close only */
        bx -= (BTN+BTNGAP); caption_button(c->frame, bx, by, 'o');
        bx -= (BTN+BTNGAP); caption_button(c->frame, bx, by, '_');
    }
}

/* ---- focus + theme ---- */
static void apply_theme_defaults(void) {   /* Win95: navy->blue active, gray inactive */
    at_lo = 0x000080; at_hi = 0x1084D0; at_text = rgb(0xFF,0xFF,0xFF);
    it_lo = 0x808080; it_hi = 0xA8A8A8; it_text = rgb(0xD8,0xD8,0xD8);
    g_style = 0; c_closebtn = rgb(0xC0,0xC0,0xC0); c_minmaxbtn = rgb(0xC0,0xC0,0xC0);
}
/* the shell publishes _DEBIAN95_THEME = 8 CARDINALs (0xRRGGBB): active lo/hi/text,
 * inactive lo/hi/text, face, desktop. Absent => keep the Win95 defaults. */
static void read_theme(void) {
    Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = 0;
    if (XGetWindowProperty(dpy, root, A_DEBIAN95_THEME, 0, 16, False, XA_CARDINAL,
                           &type, &fmt, &n, &after, &data) == Success && data && n >= 8) {
        unsigned long *v = (unsigned long *)data;
        at_lo = v[0] & 0xFFFFFF; at_hi = v[1] & 0xFFFFFF;
        at_text = rgb((v[2]>>16)&0xFF, (v[2]>>8)&0xFF, v[2]&0xFF);
        it_lo = v[3] & 0xFFFFFF; it_hi = v[4] & 0xFFFFFF;
        it_text = rgb((v[5]>>16)&0xFF, (v[5]>>8)&0xFF, v[5]&0xFF);
        c_face = rgb((v[6]>>16)&0xFF, (v[6]>>8)&0xFF, v[6]&0xFF);
        c_desktop = rgb((v[7]>>16)&0xFF, (v[7]>>8)&0xFF, v[7]&0xFF);
        XSetWindowBackground(dpy, root, c_desktop);
        if (n >= 11) {                               /* XP caption-button colours + style */
            c_closebtn  = rgb((v[8]>>16)&0xFF, (v[8]>>8)&0xFF, v[8]&0xFF);
            c_minmaxbtn = rgb((v[9]>>16)&0xFF, (v[9]>>8)&0xFF, v[9]&0xFF);
            g_style = (int)v[10];
        }
    }
    if (data) XFree(data);
}
/* advertise the focused window on the root, so the shell taskbar can highlight it */
static void publish_active(void) {
    Window w = g_focus ? g_focus->client : None;
    XChangeProperty(dpy, root, A_NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&w, 1);
}
/* the topmost still-mapped client other than `skip` (for focus after close/minimize) */
static Client *topmost_other(Client *skip) {
    for (Client *c = clients; c; c = c->next) {
        if (c == skip) continue;
        XWindowAttributes a;
        if (XGetWindowAttributes(dpy, c->frame, &a) && a.map_state != IsUnmapped) return c;
    }
    return NULL;
}
static void set_focus(Client *c) {
    if (g_focus == c) return;
    Client *old = g_focus; g_focus = c;
    if (old) draw_frame(old);                 /* repaint the one losing focus (-> inactive) */
    if (c) { draw_frame(c); XSetInputFocus(dpy, c->client, RevertToPointerRoot, CurrentTime); }
    publish_active();
}

/* which caption button (if any) is at frame-relative (x,y); returns 'x','o','_' or 0 */
static char hit_button(Client *c, int x, int y) {
    int fw, fh; frame_size(c, &fw, &fh);
    int tx = BORDER, ty = BORDER, tw = fw - 2*BORDER, th = TITLEH;
    int by = ty + (th-BTN)/2;
    if (y < by || y > by+BTN) return 0;
    int bx = tx + tw - (BTN+BTNGAP);
    if (x >= bx && x < bx+BTN) return 'x';
    if (c->dialog) return 0;                          /* dialogs: close only */
    bx -= (BTN+BTNGAP);
    if (x >= bx && x < bx+BTN) return 'o';
    bx -= (BTN+BTNGAP);
    if (x >= bx && x < bx+BTN) return '_';
    return 0;
}
static int in_titlebar(Client *c, int x, int y) {
    int fw, fh; frame_size(c, &fw, &fh);
    return (y >= BORDER && y < BORDER+TITLEH && x >= BORDER && x < fw-BORDER);
}

static void close_client(Client *c) {
    /* ICCCM WM_DELETE_WINDOW if supported, else kill */
    Atom *protos; int n, ok = 0;
    if (XGetWMProtocols(dpy, c->client, &protos, &n)) {
        for (int i = 0; i < n; i++) if (protos[i] == A_WM_DELETE) ok = 1;
        XFree(protos);
    }
    if (ok) {
        XEvent e; memset(&e, 0, sizeof e);
        e.type = ClientMessage; e.xclient.window = c->client;
        e.xclient.message_type = A_WM_PROTOCOLS; e.xclient.format = 32;
        e.xclient.data.l[0] = A_WM_DELETE; e.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c->client, False, NoEventMask, &e);
    } else {
        XKillClient(dpy, c->client);
    }
}

/* maximize <-> restore: toggle the frame between its saved geometry and full screen */
static void toggle_max(Client *c) {
    int SW = DisplayWidth(dpy, scr), SH = DisplayHeight(dpy, scr);
    if (c->shaded) return;                 /* nonsensical while rolled up */
    if (!c->max) {
        XWindowAttributes fa; XGetWindowAttributes(dpy, c->frame, &fa);
        c->sx = fa.x; c->sy = fa.y; c->sw = c->cw; c->sh = c->ch;
        c->cw = SW - 2*BORDER; c->ch = SH - TITLEH - 2*BORDER;
        XMoveResizeWindow(dpy, c->frame, 0, 0, SW, SH);
        XResizeWindow(dpy, c->client, c->cw, c->ch);
        c->max = 1;
    } else {
        c->cw = c->sw; c->ch = c->sh;
        int fw, fh; frame_size(c, &fw, &fh);
        XMoveResizeWindow(dpy, c->frame, c->sx, c->sy, fw, fh);
        XResizeWindow(dpy, c->client, c->cw, c->ch);
        c->max = 0;
    }
    draw_frame(c);
}

/* window-shade: roll the frame up to just its title bar and back (classic behavior;
 * a self-contained stand-in for minimize since the WM has no taskbar of its own) */
static void toggle_shade(Client *c) {
    int fw, fh; frame_size(c, &fw, &fh);
    if (!c->shaded) {
        c->shaded = 1;
        XUnmapWindow(dpy, c->client);
        XResizeWindow(dpy, c->frame, fw, TITLEH + 2*BORDER);
    } else {
        c->shaded = 0;
        XResizeWindow(dpy, c->frame, fw, fh);
        XMapWindow(dpy, c->client);
    }
    draw_frame(c);
}

/* ---- right-click title-bar system menu (Restore/Minimize/Maximize/Close) ---- */
static void draw_menu(void) {
    if (!g_menu) return;
    XSetForeground(dpy, gc, c_face); XFillRectangle(dpy, g_menu, gc, 0, 0, SM_W, SM_IH*SM_N);
    bevel_raised(g_menu, 0, 0, SM_W, SM_IH*SM_N);
    if (font) XSetFont(dpy, gc, font->fid);
    for (int i = 0; i < SM_N; i++) {
        int yy = i * SM_IH;
        if (i == g_menu_hover) {
            XSetForeground(dpy, gc, rgb(0x00,0x00,0x80)); XFillRectangle(dpy, g_menu, gc, 2, yy+1, SM_W-4, SM_IH-1);
            XSetForeground(dpy, gc, c_white);
        } else XSetForeground(dpy, gc, c_black);
        XDrawString(dpy, g_menu, gc, 10, yy+13, SM_LABELS[i], strlen(SM_LABELS[i]));
    }
}
static void close_menu(void) {
    if (!g_menu) return;
    XUngrabPointer(dpy, CurrentTime);
    XDestroyWindow(dpy, g_menu); g_menu = 0; g_menu_c = 0; g_menu_hover = -1;
}
static void toggle_max(Client *c);     /* fwd (defined above) */
static void open_sysmenu(Client *c, int rx, int ry) {
    if (g_menu) close_menu();
    g_menu_c = c; g_menu_hover = -1;
    int SW = DisplayWidth(dpy, scr), SH = DisplayHeight(dpy, scr);
    if (rx + SM_W > SW) rx = SW - SM_W;  if (ry + SM_IH*SM_N > SH) ry = SH - SM_IH*SM_N;
    if (rx < 0) rx = 0; if (ry < 0) ry = 0;
    XSetWindowAttributes wa;
    wa.override_redirect = True; wa.background_pixel = c_face;
    wa.event_mask = ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask;
    g_menu = XCreateWindow(dpy, root, rx, ry, SM_W, SM_IH*SM_N, 0, CopyFromParent,
                           InputOutput, CopyFromParent,
                           CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
    XMapRaised(dpy, g_menu);
    XGrabPointer(dpy, g_menu, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    draw_menu();
}
/* Alt+Tab: cycle focus through the framed apps (Alt+Shift+Tab goes back). Raises
 * and focuses the next mapped client - the shell's own built-in apps have their
 * own Alt-Tab when the desktop is focused; this covers the downloaded apps. */
static void alt_tab(int reverse) {
    Client *arr[64]; int n = 0, cur = -1;
    for (Client *c = clients; c && n < 64; c = c->next) {
        XWindowAttributes a;
        if (XGetWindowAttributes(dpy, c->frame, &a) && a.map_state != IsUnmapped) {
            if (c == g_focus) cur = n;
            arr[n++] = c;
        }
    }
    if (n == 0) return;
    int idx = (cur < 0) ? 0 : (reverse ? (cur - 1 + n) % n : (cur + 1) % n);
    Client *c = arr[idx];
    c->shaded = 0;
    XMapWindow(dpy, c->frame); XRaiseWindow(dpy, c->frame);
    set_focus(c);
}

static void menu_exec(int item) {
    Client *c = g_menu_c; if (!c) return;
    if (item == 0) {                                   /* Restore */
        c->shaded = 0; if (c->max) toggle_max(c);
        XMapWindow(dpy, c->frame); XMapWindow(dpy, c->client);
        XRaiseWindow(dpy, c->frame); set_focus(c);
    } else if (item == 1) {                            /* Minimize */
        XUnmapWindow(dpy, c->frame); if (g_focus == c) set_focus(topmost_other(c));
    } else if (item == 2) {                            /* Maximize */
        toggle_max(c); set_focus(c);
    } else if (item == 3) {                            /* Close */
        close_client(c);
    }
}

/* first _NET_WM_WINDOW_TYPE atom, or None */
static Atom win_type(Window w) {
    Atom type; int fmt; unsigned long n = 0, after; unsigned char *d = 0; Atom t = None;
    if (XGetWindowProperty(dpy, w, A_NET_WM_WINDOW_TYPE, 0, 4, False, XA_ATOM,
                           &type, &fmt, &n, &after, &d) == Success && d && n > 0) t = ((Atom *)d)[0];
    if (d) XFree(d);
    return t;
}
static int wants_fullscreen(Window w) {
    Atom type; int fmt; unsigned long n = 0, after; unsigned char *d = 0; int fs = 0;
    if (XGetWindowProperty(dpy, w, A_NET_WM_STATE, 0, 16, False, XA_ATOM,
                           &type, &fmt, &n, &after, &d) == Success && d)
        for (unsigned long i = 0; i < n; i++) if (((Atom *)d)[i] == A_NET_WM_STATE_FULLSCREEN) fs = 1;
    if (d) XFree(d);
    return fs;
}
/* fullscreen without reparenting: push the frame's chrome off-screen so the client
 * area alone fills the display (no UnmapNotify churn from re-parenting). */
static void go_fullscreen(Client *c) {
    if (c->fullscreen) return;
    int SW = DisplayWidth(dpy, scr), SH = DisplayHeight(dpy, scr);
    XWindowAttributes fa; if (!XGetWindowAttributes(dpy, c->frame, &fa)) return;
    c->sx = fa.x; c->sy = fa.y; c->sw = c->cw; c->sh = c->ch;
    c->fullscreen = 1; c->cw = SW; c->ch = SH;
    XMoveResizeWindow(dpy, c->frame, -BORDER, -(TITLEH + BORDER), SW + 2*BORDER, SH + TITLEH + 2*BORDER);
    XResizeWindow(dpy, c->client, SW, SH);
    XRaiseWindow(dpy, c->frame);
    XSetInputFocus(dpy, c->client, RevertToPointerRoot, CurrentTime);
}
static void exit_fullscreen(Client *c) {
    if (!c->fullscreen) return;
    c->fullscreen = 0; c->cw = c->sw; c->ch = c->sh;
    int fw, fh; frame_size(c, &fw, &fh);
    XMoveResizeWindow(dpy, c->frame, c->sx, c->sy, fw, fh);
    XResizeWindow(dpy, c->client, c->cw, c->ch);
    XRaiseWindow(dpy, c->frame);
    draw_frame(c);
    set_focus(c);
}

static void frame_window(Window w) {
    XWindowAttributes a;
    if (!XGetWindowAttributes(dpy, w, &a)) return;
    char *wn = NULL; XFetchName(dpy, w, &wn);
    fprintf(stderr, "debian95-wm: MapRequest w=%dx%d ovr=%d name=%s\n", a.width, a.height, a.override_redirect, wn ? wn : "(none)");
    if (a.override_redirect) { if (wn) XFree(wn); XMapWindow(dpy, w); return; }
    if (find_client(w)) { if (wn) XFree(wn); return; }
    int SW = DisplayWidth(dpy, scr), SH = DisplayHeight(dpy, scr);
    Atom wt = win_type(w);
    Window tf = None; XGetTransientForHint(dpy, w, &tf);

    /* the Debian95 desktop/shell (titled "Debian95", or a screen-filling type): fill
     * the screen, leave it unframed at the back; chrome goes only on app windows. */
    if (wt == A_WT_DESKTOP || (wn && strcmp(wn, "Debian95") == 0) ||
        (a.width >= SW - 64 && a.height >= SH - 64 && tf == None && wt != A_WT_DIALOG && wt != A_WT_UTILITY)) {
        XMoveResizeWindow(dpy, w, 0, 0, SW, SH);
        XLowerWindow(dpy, w); XMapWindow(dpy, w);
        if (wn) XFree(wn); return;
    }
    /* pass-through types (menus, tooltips, splash screens, docks, DND): never
     * framed - a real app's own popups/menus must appear as-is. */
    if (wt == A_WT_SPLASH || wt == A_WT_MENU || wt == A_WT_DROPDOWN || wt == A_WT_POPUP ||
        wt == A_WT_TOOLTIP || wt == A_WT_NOTIF || wt == A_WT_DOCK || wt == A_WT_COMBO || wt == A_WT_DND) {
        if (wt == A_WT_SPLASH) XMoveWindow(dpy, w, (SW - a.width)/2, (SH - a.height)/2);
        XMapRaised(dpy, w); if (wn) XFree(wn); return;
    }
    if (wn) XFree(wn);

    /* a dialog/utility window, or anything transient-for another: close-only chrome,
     * centred, kept off the taskbar (like a real Win95 dialog). */
    int is_dialog = (wt == A_WT_DIALOG || wt == A_WT_UTILITY || tf != None);
    Client *c = calloc(1, sizeof *c);
    c->client = w; c->cw = a.width; c->ch = a.height; c->dialog = is_dialog;
    int fw = c->cw + 2*BORDER, fh = c->ch + TITLEH + 2*BORDER;
    int fx, fy;
    if (is_dialog) { fx = (SW - fw)/2; if (fx < 0) fx = 0; fy = (SH - fh)/2; if (fy < 40) fy = 40; }
    else           { fx = a.x ? a.x : 60; fy = a.y ? a.y : 40; }

    c->frame = XCreateSimpleWindow(dpy, root, fx, fy, fw, fh, 0, c_black, c_face);
    XSelectInput(dpy, c->frame, SubstructureRedirectMask|SubstructureNotifyMask|
                 ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask);
    XAddToSaveSet(dpy, w);
    XReparentWindow(dpy, w, c->frame, BORDER, BORDER + TITLEH);
    XMapWindow(dpy, c->frame);
    XMapWindow(dpy, w);
    c->next = clients; clients = c;
    draw_frame(c);
    update_client_list();
    set_focus(c);                        /* a newly-mapped app takes focus */
    if (wants_fullscreen(w)) go_fullscreen(c);   /* mapped already-fullscreen */
}

static void unframe(Client *c) {
    XUnmapWindow(dpy, c->frame);
    XReparentWindow(dpy, c->client, root, 0, 0);
    XRemoveFromSaveSet(dpy, c->client);
    XDestroyWindow(dpy, c->frame);
    /* unlink */
    int wasfocus = (g_focus == c);
    if (wasfocus) g_focus = NULL;
    Client **pp = &clients;
    while (*pp && *pp != c) pp = &(*pp)->next;
    if (*pp) *pp = c->next;
    free(c);
    update_client_list();
    if (wasfocus) set_focus(topmost_other(NULL));   /* focus the next window down */
    else          publish_active();
}

/* a WM must not die on the benign X errors that races produce (a client destroys
 * a window just before we touch it): swallow them instead of the default exit(). */
static int ignore_xerror(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "debian95-wm: cannot open display\n"); return 1; }
    XSetErrorHandler(ignore_xerror);
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);

    c_face     = rgb(0xC0,0xC0,0xC0); c_white  = rgb(0xFF,0xFF,0xFF);
    c_light    = rgb(0xDF,0xDF,0xDF); c_shadow = rgb(0x80,0x80,0x80);
    c_dkshadow = rgb(0x00,0x00,0x00); c_black  = rgb(0x00,0x00,0x00);
    c_desktop  = rgb(0x00,0x80,0x80);
    apply_theme_defaults();

    gc = XCreateGC(dpy, root, 0, NULL);
    font = XLoadQueryFont(dpy, "-*-helvetica-bold-r-*-*-12-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy, "fixed");

    A_WM_DELETE    = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    A_WM_PROTOCOLS = XInternAtom(dpy, "WM_PROTOCOLS", False);
    A_NET_CLIENT_LIST   = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    A_NET_ACTIVE_WINDOW = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    A_NET_CLOSE_WINDOW  = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
    A_WM_CHANGE_STATE   = XInternAtom(dpy, "WM_CHANGE_STATE", False);
    A_DEBIAN95_THEME     = XInternAtom(dpy, "_DEBIAN95_THEME", False);
    A_NET_WM_WINDOW_TYPE = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    A_NET_WM_STATE       = XInternAtom(dpy, "_NET_WM_STATE", False);
    A_NET_WM_STATE_FULLSCREEN   = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    A_NET_WM_STATE_SKIP_TASKBAR = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    A_WT_DESKTOP  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    A_WT_DOCK     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    A_WT_SPLASH   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    A_WT_DIALOG   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    A_WT_UTILITY  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    A_WT_MENU     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
    A_WT_DROPDOWN = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
    A_WT_POPUP    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    A_WT_TOOLTIP  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    A_WT_NOTIF    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    A_WT_COMBO    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_COMBO", False);
    A_WT_DND      = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DND", False);
    A_NET_SUPPORTED = XInternAtom(dpy, "_NET_SUPPORTED", False);
    { Atom sup[] = { A_NET_WM_WINDOW_TYPE, A_NET_WM_STATE, A_NET_WM_STATE_FULLSCREEN,
                     A_NET_WM_STATE_SKIP_TASKBAR, A_NET_CLIENT_LIST, A_NET_ACTIVE_WINDOW,
                     A_NET_CLOSE_WINDOW, A_WT_DIALOG, A_WT_UTILITY, A_WT_SPLASH, A_WT_DESKTOP };
      XChangeProperty(dpy, root, A_NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char *)sup, 11); }
    { char sel[32]; snprintf(sel, sizeof sel, "_NET_SYSTEM_TRAY_S%d", scr);
      A_TRAY_SELECTION = XInternAtom(dpy, sel, False); }
    A_TRAY_OPCODE      = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    A_TRAY_ORIENTATION = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    A_MANAGER          = XInternAtom(dpy, "MANAGER", False);
    A_XEMBED           = XInternAtom(dpy, "_XEMBED", False);
    A_XEMBED_INFO      = XInternAtom(dpy, "_XEMBED_INFO", False);
    A_DEBIAN95_TRAY_RECT  = XInternAtom(dpy, "_DEBIAN95_TRAY_RECT", False);
    A_DEBIAN95_TRAY_COUNT = XInternAtom(dpy, "_DEBIAN95_TRAY_COUNT", False);
    read_theme();                     /* pick up the shell's theme if already published */

    /* teal desktop background */
    XSetWindowBackground(dpy, root, c_desktop);
    XClearWindow(dpy, root);

    /* become THE window manager: redirect substructure + watch the theme property */
    XSelectInput(dpy, root, SubstructureRedirectMask|SubstructureNotifyMask|PropertyChangeMask);
    /* grab Alt+Tab / Alt+Shift+Tab globally to cycle the framed apps */
    g_ktab = XKeysymToKeycode(dpy, XK_Tab);
    XGrabKey(dpy, g_ktab, Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, g_ktab, Mod1Mask|ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    tray_init();                      /* become the system-tray manager (XEMBED) */
    read_tray_rect();                 /* where the shell wants the icons (if already set) */
    XSync(dpy, False);

    /* adopt already-mapped windows */
    Window r2, p2, *ch; unsigned int nch;
    if (XQueryTree(dpy, root, &r2, &p2, &ch, &nch)) {
        for (unsigned i = 0; i < nch; i++) {
            XWindowAttributes a;
            if (XGetWindowAttributes(dpy, ch[i], &a) && a.map_state == IsViewable && !a.override_redirect)
                frame_window(ch[i]);
        }
        if (ch) XFree(ch);
    }
    update_client_list();   /* publish the initial (possibly empty) list */

    for (;;) {
        XEvent e; XNextEvent(dpy, &e);
        switch (e.type) {
        case MapRequest:
            if (is_tray_icon(e.xmaprequest.window)) { XMapWindow(dpy, e.xmaprequest.window); break; }  /* tray icon, not an app */
            frame_window(e.xmaprequest.window);
            break;
        case ConfigureRequest: {
            XConfigureRequestEvent *r = &e.xconfigurerequest;
            XWindowChanges wc;
            wc.x = r->x; wc.y = r->y; wc.width = r->width; wc.height = r->height;
            wc.border_width = r->border_width; wc.sibling = r->above; wc.stack_mode = r->detail;
            Client *c = find_client(r->window);
            if (c) {
                c->cw = r->width; c->ch = r->height;
                int fw, fh; frame_size(c, &fw, &fh);
                XResizeWindow(dpy, c->frame, fw, fh);
                XConfigureWindow(dpy, r->window, r->value_mask & (CWWidth|CWHeight), &wc);
                draw_frame(c);
            } else {
                XConfigureWindow(dpy, r->window, r->value_mask, &wc);
            }
            break;
        }
        case UnmapNotify: {
            if (is_tray_icon(e.xunmap.window)) { tray_undock(e.xunmap.window); break; }
            Client *c = find_client(e.xunmap.window);
            if (c && e.xunmap.event != root) unframe(c);
            break;
        }
        case DestroyNotify: {
            if (is_tray_icon(e.xdestroywindow.window)) { tray_undock(e.xdestroywindow.window); break; }
            Client *c = find_client(e.xdestroywindow.window);
            if (c) unframe(c);
            break;
        }
        case Expose: {
            if (g_menu && e.xexpose.window == g_menu) { draw_menu(); break; }
            Client *c = find_frame(e.xexpose.window);
            if (c && e.xexpose.count == 0) draw_frame(c);
            break;
        }
        case PropertyNotify:
            if (e.xproperty.window == root && e.xproperty.atom == A_DEBIAN95_THEME) {
                read_theme();                                   /* theme switched in the shell */
                for (Client *c = clients; c; c = c->next) draw_frame(c);
            } else if (e.xproperty.window == root && e.xproperty.atom == A_DEBIAN95_TRAY_RECT) {
                read_tray_rect();                               /* shell moved the tray slot */
            }
            break;
        case ClientMessage: {
            /* system tray: a client asks to dock its icon (freedesktop protocol) */
            if (e.xclient.message_type == A_TRAY_OPCODE && e.xclient.window == g_tray_owner) {
                if (e.xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) tray_dock((Window)e.xclient.data.l[2]);
                break;
            }
            /* the Debian95 taskbar drives a framed app: activate / minimize / close */
            Client *c = find_client(e.xclient.window);
            if (!c) break;
            if (e.xclient.message_type == A_NET_ACTIVE_WINDOW) {          /* restore + raise + focus */
                c->shaded = 0;
                XMapWindow(dpy, c->frame); XMapWindow(dpy, c->client);
                XRaiseWindow(dpy, c->frame);
                set_focus(c);
            } else if (e.xclient.message_type == A_WM_CHANGE_STATE &&
                       e.xclient.data.l[0] == IconicState) {              /* minimize -> hide the frame */
                XUnmapWindow(dpy, c->frame);
                if (g_focus == c) set_focus(topmost_other(c));
            } else if (e.xclient.message_type == A_NET_CLOSE_WINDOW) {    /* close */
                close_client(c);
            } else if (e.xclient.message_type == A_NET_WM_STATE) {        /* fullscreen toggle (VLC, etc.) */
                long act = e.xclient.data.l[0];                          /* 0 remove, 1 add, 2 toggle */
                if (e.xclient.data.l[1] == (long)A_NET_WM_STATE_FULLSCREEN ||
                    e.xclient.data.l[2] == (long)A_NET_WM_STATE_FULLSCREEN) {
                    int want = (act == 1) || (act == 2 && !c->fullscreen);
                    if (want) go_fullscreen(c); else exit_fullscreen(c);
                }
            }
            break;
        }
        case ButtonPress: {
            if (g_menu) {                              /* a click while the system menu is up */
                if (e.xbutton.window == g_menu &&
                    e.xbutton.x >= 0 && e.xbutton.x < SM_W &&
                    e.xbutton.y >= 0 && e.xbutton.y < SM_IH*SM_N)
                    menu_exec(e.xbutton.y / SM_IH);    /* pick an item */
                close_menu();                          /* click anywhere else = dismiss */
                break;
            }
            Client *c = find_frame(e.xbutton.window);
            if (!c) break;
            if (e.xbutton.button == Button3) {         /* right-click title bar -> system menu */
                if (in_titlebar(c, e.xbutton.x, e.xbutton.y))
                    open_sysmenu(c, e.xbutton.x_root, e.xbutton.y_root);
                break;
            }
            XRaiseWindow(dpy, c->frame);
            set_focus(c);
            char b = hit_button(c, e.xbutton.x, e.xbutton.y);
            if (b == 'x') close_client(c);
            else if (b == 'o') toggle_max(c);
            else if (b == '_') toggle_shade(c);
            else if (in_titlebar(c, e.xbutton.x, e.xbutton.y)) {
                drag = c; drag_dx = e.xbutton.x; drag_dy = e.xbutton.y;
            }
            break;
        }
        case MotionNotify:
            if (g_menu) {                              /* highlight the item under the pointer */
                int hov = -1;
                if (e.xmotion.window == g_menu && e.xmotion.x >= 0 && e.xmotion.x < SM_W &&
                    e.xmotion.y >= 0 && e.xmotion.y < SM_IH*SM_N) hov = e.xmotion.y / SM_IH;
                if (hov != g_menu_hover) { g_menu_hover = hov; draw_menu(); }
                break;
            }
            if (drag) {
                /* coalesce */
                while (XCheckTypedEvent(dpy, MotionNotify, &e)) {}
                XMoveWindow(dpy, drag->frame, e.xmotion.x_root - drag_dx, e.xmotion.y_root - drag_dy);
            }
            break;
        case ButtonRelease:
            drag = NULL;
            break;
        case KeyPress:
            if (e.xkey.keycode == g_ktab && (e.xkey.state & Mod1Mask))
                alt_tab(e.xkey.state & ShiftMask);
            break;
        }
    }
    return 0;
}
