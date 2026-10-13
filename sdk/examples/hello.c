/* Debian95 app module — template. Copy this file to start a new app.
 *
 *   Build:    cc -fPIC -shared -DDEBIAN95_MODULE -I<repo>/sdk/include \
 *                 hello.c -o hello.so
 *   Install:  cp hello.so /usr/lib/debian95/apps/      (then relaunch the desktop)
 *
 * The desktop scans that directory at startup, dlopen()s each .so, and registers
 * the exported `debian95_module` descriptor. A module calls the host API declared
 * by <debian95/appsdk.h> (windows, painting, controls, dialogs) and links none of
 * the shell's object files. See docs/APP-MODULE-SDK.md for the full guide.
 */
#include <debian95/appsdk.h>
#include <string.h>

/* The window procedure: the shell calls this with messages for our window. */
static LRESULT CALLBACK Hello_WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        const char *a = "Hello from a Debian95 app module.";
        const char *b = "Copy sdk/examples/hello.c to build your own.";
        TextOut(hdc, 14, 16, a, (int)strlen(a));
        TextOut(hdc, 14, 36, b, (int)strlen(b));
        EndPaint(h, hdc);
        return 0;
    }
    }
    /* Everything we do not handle (WM_DESTROY, move/size, …) goes to the shell. */
    return DefWindowProc(h, msg, wp, lp);
}

/* Launch: open the app's window. Registered as the descriptor's open(). */
static void hello_open(void) {
    CreateAppWindow(240, 170, 340, 110, "Hello", Hello_WndProc, 0);
}

/* The one symbol the loader looks for. Exactly one per module. */
const Debian95Module debian95_module = {
    DEBIAN95_MODULE_ABI,
    200,          /* command id — unique; use >= 100 for third-party apps        */
    "Hello",      /* display name                                                 */
    IC_APP,       /* icon id from <debian95/icons.h>, or 0                         */
    0,            /* icon module (e.g. "MSPAINT.EXE") for a real PE icon, or 0    */
    hello_open,   /* launch                                                       */
    3             /* Start>Programs group: 0 Internet 1 Multimedia 2 Graphics
                     3 Office  -1 none (reuse a built-in menu id instead)         */
};
