/* Debian95 — Application Module SDK.
 *
 * An app module is a shared object (.so) dropped into the plugins directory
 * (default /usr/lib/debian95/apps, override with $DEBIAN95_PLUGINS). At startup
 * the shell scans that directory, dlopen()s each module, reads its exported
 * `debian95_module` descriptor, and registers it like any built-in app. Adding
 * or replacing an app is then a matter of dropping in a .so — the shell binary
 * is never recompiled and never grows.
 *
 * Because modules load into the shell's own process, a module calls back into
 * the host directly: it uses the functions declared by the headers included
 * below (windows + painting + controls + dialogs from win32.h; wm_* helpers;
 * the framebuffer; text metrics; icon ids). THAT SET IS THE STABLE ABI. The
 * host exports those symbols (it is linked -rdynamic); a module links against
 * none of the shell's object files. Keep additions to the surface backwards
 * compatible, and bump DEBIAN95_MODULE_ABI on any incompatible change.
 *
 * A module is NOT isolated from the shell (same address space): the SDK gives
 * build- and distribution-level modularity (hot-swap, no monolith growth), not
 * fault isolation. Author modules to the contract and they behave like a
 * first-class app; the curated, real Linux apps (VLC, …) remain separate
 * processes framed by debian95-wm.
 *
 *   A module .c defines exactly one descriptor, exported by name:
 *
 *       #include <debian95/appsdk.h>
 *       static void myapp_open(void) { CreateAppWindow(...); }
 *       const Debian95Module debian95_module = {
 *           DEBIAN95_MODULE_ABI, 120, "My App", IC_APP, 0, myapp_open, -1
 *       };
 *
 *   build:  cc -fPIC -shared -I<sdk>/include -DDEBIAN95_MODULE \
 *               myapp.c -o myapp.so            (no shell objects linked)
 */
#ifndef DEBIAN95_APPSDK_H
#define DEBIAN95_APPSDK_H

#include <debian95/types.h>
#include <debian95/win32.h>   /* CreateAppWindow, Begin/EndPaint, controls, dialogs */
#include <debian95/wm.h>      /* wm_* helpers: clipboard, bevels, program registration */
#include <debian95/fb.h>      /* framebuffer drawing primitives */
#include <debian95/font.h>    /* font_height / font_measure text metrics */
#include <debian95/icons.h>   /* IC_* icon ids */

/* Bump on any source-incompatible change to the descriptor or the stable ABI
 * surface above. The loader refuses a module whose abi field does not match. */
#define DEBIAN95_MODULE_ABI 2

typedef struct {
    int             abi;        /* must equal DEBIAN95_MODULE_ABI */
    int             id;         /* command id the Start menu / desktop fire */
    const char     *name;       /* display name */
    unsigned short  icon;       /* IC_* icon id (0 if none / supplied by imod) */
    const char     *imod;       /* icon module e.g. "MSPAINT.EXE" (0 = use icon) */
    void          (*open)(void);/* launch: opens the app window in-process */
    int             category;   /* Start>Programs category, or -1 for none
                                   (apps already present in a static menu use -1) */
    void          (*open_doc)(const char *path);  /* ABI 2: open ON a document (file
                                   associations). 0 = none; the shell falls back to open(). */
} Debian95Module;

/* The symbol every module exports (see the example above). */
/* extern const Debian95Module debian95_module; */

/* Host-side loader: dlopen every *.so in `dir`, register the valid ones.
 * Returns the count loaded. Implemented in usermode/desktop/module_loader.c. */
int modules_load(const char *dir);

#endif /* DEBIAN95_APPSDK_H */
