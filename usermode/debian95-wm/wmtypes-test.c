/* wmtypes-test — a tiny X client that maps a normal window, a dialog, and a
 * splash (and can request fullscreen) so debian95-wm's window-type handling can
 * be verified. Not shipped in the normal image; built only for WM testing. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static Display *d;
static Window mk(const char *title, int w, int h, const char *wtype, Window transient_for) {
    Window win = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, w, h, 0,
                                     BlackPixel(d, 0), 0xC0C0C0);
    XStoreName(d, win, title);
    if (wtype) {
        Atom a = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
        Atom v = XInternAtom(d, wtype, False);
        XChangeProperty(d, win, a, XA_ATOM, 32, PropModeReplace, (unsigned char *)&v, 1);
    }
    if (transient_for) XSetTransientForHint(d, win, transient_for);
    XSelectInput(d, win, ExposureMask);
    XMapWindow(d, win);
    return win;
}
int main(int argc, char **argv) {
    d = XOpenDisplay(NULL); if (!d) return 1;
    int fs = (argc > 1 && !strcmp(argv[1], "fullscreen"));
    Window norm = mk("Normal App", 320, 200, 0, 0);
    if (fs) {
        /* ask to go fullscreen via _NET_WM_STATE after it is mapped */
        XSync(d, False); sleep(1);
        XEvent e; memset(&e, 0, sizeof e);
        e.type = ClientMessage; e.xclient.window = norm; e.xclient.format = 32;
        e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
        e.xclient.data.l[0] = 1;  /* add */
        e.xclient.data.l[1] = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
        XSendEvent(d, DefaultRootWindow(d), False,
                   SubstructureNotifyMask|SubstructureRedirectMask, &e);
        XSync(d, False);
    } else {
        mk("Settings", 260, 150, "_NET_WM_WINDOW_TYPE_DIALOG", norm);
        mk("Loading...", 220, 110, "_NET_WM_WINDOW_TYPE_SPLASH", 0);
    }
    for (;;) { XEvent e; XNextEvent(d, &e); }
    return 0;
}
