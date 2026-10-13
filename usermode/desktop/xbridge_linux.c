/* Debian95 desktop - foreign-app taskbar bridge (X11), SDL/Desktop-Edition only.
 *
 * debian95-wm frames downloaded apps (VLC, Chromium) and publishes them in the
 * root's _NET_CLIENT_LIST. This file (a separate translation unit from the shell,
 * because Xlib and debian95/wm.h both typedef `Window`) mirrors each framed app
 * into an "external" taskbar window via the wm_ext_* API, and turns taskbar clicks
 * into EWMH messages (activate / iconify / close) back to debian95-wm.
 *
 * Compiled ONLY into debian95-desktop-sdl (see the Makefile). The fbdev/Appliance
 * build has no X server; platform_linux.c provides no-op stubs there. */
#ifdef USE_SDL
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>            /* IconicState */
#include <stdio.h>
#include <string.h>
#include <debian95/theme.h>       /* g_theme - published to debian95-wm for framed chrome */

/* wm.c API (declared here, not via wm.h, to avoid its `Window` typedef clashing
 * with Xlib's). These use only unsigned long / void* / char, so no X types leak. */
void        *wm_ext_find(unsigned long xclient);
void        *wm_ext_add(unsigned long xframe, unsigned long xclient, const char *title);
void         wm_ext_set_title(void *win, const char *title);
void         wm_ext_set_min(void *win, int minimized);
void         wm_ext_set_active(unsigned long xclient);
void         wm_ext_del(void *win);
int          wm_ext_enum(unsigned long *out, int max);
int          wm_take_ext_action(unsigned long *client);
unsigned int wm_now_ms(void);
void         wm_set_tray_slots(int n);                     /* reserve N tray slots in the taskbar */
int          wm_get_tray_strip(int *x, int *y, int *w, int *h);   /* screen rect of those slots */

static Display *g_xdpy;
static Atom g_ax_list, g_ax_active, g_ax_close, g_ax_changestate, g_ax_netname, g_ax_utf8, g_ax_theme;
static Atom g_ax_traycount, g_ax_trayrect;

void ext_x11_init(void) {
    g_xdpy = XOpenDisplay(NULL);
    if (!g_xdpy) { fprintf(stderr, "taskbar-bridge: no X display (foreign apps won't appear)\n"); return; }
    g_ax_list        = XInternAtom(g_xdpy, "_NET_CLIENT_LIST", False);
    g_ax_active      = XInternAtom(g_xdpy, "_NET_ACTIVE_WINDOW", False);
    g_ax_close       = XInternAtom(g_xdpy, "_NET_CLOSE_WINDOW", False);
    g_ax_changestate = XInternAtom(g_xdpy, "WM_CHANGE_STATE", False);
    g_ax_netname     = XInternAtom(g_xdpy, "_NET_WM_NAME", False);
    g_ax_utf8        = XInternAtom(g_xdpy, "UTF8_STRING", False);
    g_ax_theme       = XInternAtom(g_xdpy, "_DEBIAN95_THEME", False);
    g_ax_traycount   = XInternAtom(g_xdpy, "_DEBIAN95_TRAY_COUNT", False);
    g_ax_trayrect    = XInternAtom(g_xdpy, "_DEBIAN95_TRAY_RECT", False);
}

/* publish the active theme's caption colours so debian95-wm styles framed apps to
 * match (active lo/hi/text, inactive lo/hi/text, face, desktop; all 0xRRGGBB). */
void ext_x11_publish_theme(void) {
    if (!g_xdpy || !g_theme) return;
    unsigned long v[11] = {
        g_theme->title_lo,  g_theme->title_hi,  g_theme->title_text,
        g_theme->title_ilo, g_theme->title_ihi, g_theme->title_itext,
        g_theme->face,      g_theme->desktop,
        g_theme->close_btn, g_theme->minmax_btn, (unsigned long)g_theme->style,
    };
    XChangeProperty(g_xdpy, DefaultRootWindow(g_xdpy), g_ax_theme, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)v, 11);
    XFlush(g_xdpy);
}

static void x_title(Window c, char *out, int n) {
    out[0] = 0;
    Atom type; int fmt; unsigned long nit, after; unsigned char *data = 0;
    if (XGetWindowProperty(g_xdpy, c, g_ax_netname, 0, 64, False, g_ax_utf8,
                           &type, &fmt, &nit, &after, &data) == Success && data) {
        int i = 0; for (; data[i] && i < n - 1; i++) out[i] = (char)data[i]; out[i] = 0;
        XFree(data); if (out[0]) return;
    }
    if (data) XFree(data);
    char *wn = 0; if (XFetchName(g_xdpy, c, &wn) && wn) {
        int i = 0; for (; wn[i] && i < n - 1; i++) out[i] = wn[i]; out[i] = 0; XFree(wn);
    }
}
/* the app's frame is its parent under a reparenting WM (debian95-wm) */
static Window x_frame(Window c) {
    Window r, parent, *kids = 0; unsigned nk = 0;
    if (!XQueryTree(g_xdpy, c, &r, &parent, &kids, &nk)) return c;
    if (kids) XFree(kids);
    return (parent && parent != DefaultRootWindow(g_xdpy)) ? parent : c;
}

/* reconcile our external taskbar buttons with debian95-wm's _NET_CLIENT_LIST */
int ext_x11_sync(void) {
    if (!g_xdpy) return 0;
    static unsigned int last; unsigned int now = wm_now_ms();
    if (now - last < 500) return 0;                      /* poll ~2 Hz */
    last = now;
    Atom type; int fmt; unsigned long nit = 0, after; unsigned char *data = 0;
    Window root = DefaultRootWindow(g_xdpy);
    if (XGetWindowProperty(g_xdpy, root, g_ax_list, 0, 64, False, XA_WINDOW,
                           &type, &fmt, &nit, &after, &data) != Success) return 0;
    if (type != XA_WINDOW) { if (data) XFree(data); return 0; }   /* WM hasn't published yet */
    Window *list = (Window *)data; int changed = 0;
    for (unsigned i = 0; i < nit; i++) {
        Window c = list[i];
        char title[128]; x_title(c, title, sizeof title);
        const char *t = title[0] ? title : "Application";
        void *w = wm_ext_find((unsigned long)c);
        if (!w) { w = wm_ext_add((unsigned long)x_frame(c), (unsigned long)c, t); changed = 1; }
        else    { wm_ext_set_title(w, t); }
        Window f = x_frame(c); XWindowAttributes fa;
        if (f && XGetWindowAttributes(g_xdpy, f, &fa)) wm_ext_set_min(w, fa.map_state == IsUnmapped);
    }
    /* sweep: drop externals no longer in the list (app closed) */
    unsigned long mine[16]; int nm = wm_ext_enum(mine, 16);
    for (int i = 0; i < nm; i++) {
        int found = 0;
        for (unsigned j = 0; j < nit; j++) if (list[j] == (Window)mine[i]) { found = 1; break; }
        if (!found) { void *w = wm_ext_find(mine[i]); if (w) { wm_ext_del(w); changed = 1; } }
    }
    if (data) XFree(data);
    /* which framed app is focused (debian95-wm publishes _NET_ACTIVE_WINDOW) -> the
     * taskbar sinks that app's button. Recompose when it changes. */
    { static unsigned long last_active; unsigned long act = 0;
      Atom t; int f; unsigned long an = 0, aa; unsigned char *ad = 0;
      if (XGetWindowProperty(g_xdpy, root, g_ax_active, 0, 1, False, XA_WINDOW,
                             &t, &f, &an, &aa, &ad) == Success && ad && an >= 1)
          act = (unsigned long)*(Window *)ad;
      if (ad) XFree(ad);
      wm_ext_set_active(act);
      if (act != last_active) { last_active = act; changed = 1; } }
    /* system tray: mirror debian95-wm's docked-icon count into reserved taskbar
     * slots, and publish where those slots sit on screen so the WM places the
     * XEMBED icon windows there. */
    { Atom t; int f; unsigned long cn = 0, ca; unsigned char *cd = 0; int slots = 0;
      if (XGetWindowProperty(g_xdpy, root, g_ax_traycount, 0, 1, False, XA_CARDINAL,
                             &t, &f, &cn, &ca, &cd) == Success && cd && cn >= 1)
          slots = (int)*(unsigned long *)cd;
      if (cd) XFree(cd);
      static int last_slots = -1;
      if (slots != last_slots) { last_slots = slots; wm_set_tray_slots(slots); changed = 1; }
      else wm_set_tray_slots(slots);
      int rx, ry, rw, rh;
      if (slots > 0 && wm_get_tray_strip(&rx, &ry, &rw, &rh)) {
          unsigned long rect[4] = { (unsigned long)rx, (unsigned long)ry, (unsigned long)rw, (unsigned long)rh };
          XChangeProperty(g_xdpy, root, g_ax_trayrect, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)rect, 4);
          XFlush(g_xdpy);
      } }
    return changed;
}

static void x_send_root(Atom msg, Window client, long l0) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage; e.xclient.window = client;
    e.xclient.message_type = msg; e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = CurrentTime;
    XSendEvent(g_xdpy, DefaultRootWindow(g_xdpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(g_xdpy);
}
/* turn a queued taskbar click into the matching EWMH request to debian95-wm */
void ext_x11_drain(void) {
    if (!g_xdpy) return;
    unsigned long client; int a;
    while ((a = wm_take_ext_action(&client))) {
        if      (a == 1) x_send_root(g_ax_active, (Window)client, 2);                  /* activate/restore */
        else if (a == 2) x_send_root(g_ax_changestate, (Window)client, IconicState);   /* minimize */
        else if (a == 3) x_send_root(g_ax_close, (Window)client, 0);                   /* close */
    }
}
#endif /* USE_SDL */
