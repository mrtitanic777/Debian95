/* Debian95 — application registry: the single source of truth for launching apps.
 *
 * Instead of a hardcoded switch mapping menu command IDs to xxx_open() calls,
 * every app registers itself as a { id, name, icon, open } entry. The Start menu
 * and desktop fire a command ID; app_launch(id) finds the entry and calls its
 * open() function. Adding a new app is one app_register() call — no central
 * switch to edit. Apps still run in-process (open() just adds a window), so
 * there is zero launch latency; this is modularity, not multiprocessing. */
#ifndef DEBIAN95_APPREG_H
#define DEBIAN95_APPREG_H

#include <debian95/types.h>

typedef struct {
    int          id;      /* menu command ID (the same ids the Start menu fires) */
    const char  *name;    /* display name */
    u16          icon;    /* IC_* icon id (0 if supplied via module instead) */
    const char  *imod;    /* icon module e.g. "MSPAINT.EXE" (0 = use `icon`) */
    void       (*open)(void);               /* launch: creates/show the app window */
    void       (*open_doc)(const char *);   /* launch on a document (file assoc); 0 = none */
} AppEntry;

/* Register (or replace, by id) one app. Safe to call at startup. */
void            app_register(int id, const char *name, u16 icon,
                             const char *imod, void (*open)(void));
void            app_register_doc(int id, void (*open_doc)(const char *));  /* set the doc-opener */
const AppEntry *app_find(int id);            /* the entry for id, or 0 */
int             app_launch(int id);          /* run app id; 1 if found, else 0 */
int             app_launch_doc(int id, const char *path);  /* open id on a document; 1 if found */
const AppEntry *app_table(int *n);           /* the whole table (for menu building) */

/* Register every app that lives in the shared subsystems/win32 tree. The
 * platform layer calls this, then app_register()s its own platform-specific
 * apps (e.g. the /bin/sh terminal). */
void            app_register_builtin(void);

#endif /* DEBIAN95_APPREG_H */
