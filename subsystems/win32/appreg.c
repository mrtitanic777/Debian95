/* Debian95 — application registry (see sdk/include/debian95/appreg.h). */
#include <debian95/appreg.h>
#include <debian95/wm.h>
#include <debian95/icons.h>

#define APP_MAX 64
static AppEntry g_apps[APP_MAX];
static int      g_napps;

void app_register(int id, const char *name, u16 icon, const char *imod, void (*open)(void)) {
    for (int i = 0; i < g_napps; i++)            /* replace an existing id in place */
        if (g_apps[i].id == id) { g_apps[i].name = name; g_apps[i].icon = icon;
                                  g_apps[i].imod = imod; g_apps[i].open = open; return; }
    if (g_napps >= APP_MAX) return;
    g_apps[g_napps].id = id; g_apps[g_napps].name = name; g_apps[g_napps].icon = icon;
    g_apps[g_napps].imod = imod; g_apps[g_napps].open = open; g_napps++;
}
void app_register_doc(int id, void (*open_doc)(const char *)) {
    for (int i = 0; i < g_napps; i++) if (g_apps[i].id == id) { g_apps[i].open_doc = open_doc; return; }
}
const AppEntry *app_find(int id) {
    for (int i = 0; i < g_napps; i++) if (g_apps[i].id == id) return &g_apps[i];
    return 0;
}
int app_launch(int id) {
    const AppEntry *a = app_find(id);
    if (a && a->open) { a->open(); return 1; }
    return 0;
}
/* open app `id` on a document: its doc-opener if it has one, else a plain launch. */
int app_launch_doc(int id, const char *path) {
    const AppEntry *a = app_find(id);
    if (!a) return 0;
    if (a->open_doc) a->open_doc(path);
    else if (a->open) a->open();
    return 1;
}
const AppEntry *app_table(int *n) { if (n) *n = g_napps; return g_apps; }

/* thin launchers for apps whose open() takes an argument — from a menu they
 * start on a default document (the file-manager passes real names directly). */
void regedit_open(void);             /* subsystems/win32/regedit.c */
void taskmgr_open(void);             /* subsystems/win32/taskmgr.c */
void printman_open(void);            /* subsystems/win32/print.c */
void services_open(void);            /* subsystems/win32/services.c */
void resviewer_open(void);           /* usermode/desktop/rcparse.c (real Windows dialog viewer) */
static void open_media(void)   { mediaplayer_open("TESTVID.MPG"); }

void app_register_builtin(void) {
    app_register(3,  "Windows Explorer", IC_FOLDER, 0,            files_open);
    app_register(7,  "Find",             IC_FIND,   0,            find_open);
    app_register(8,  "Help",             IC_HELP,   0,            help_open);
    /* Hot-swappable app modules (loaded from the plugins directory by
     * module_loader.c; see appsdk.h) — NOT linked into the shell. Their static
     * Start-menu entries still fire these ids, which app_launch resolves to the
     * module. Doc-type apps (ABI 2) also open on a file via app_launch_doc:
     *   Calculator 10, Minesweeper 9, Clock 11, Solitaire 12, Character Map 14,
     *   MS-DOS Prompt 15, Clipboard Viewer 16, FreeCell 24, Paint 5, Web Browser
     *   28, Registry Editor 29, Task Manager 31, Services 33, Volume Control 25,
     *   Sound Recorder 26, Notepad 4, WordPad 27. */
    app_register(13, "Media Player",     IC_APP,    0,            open_media);
    app_register(17, "Recycle Bin",      0,         0,            recycle_open);
    app_register(18, "My Computer",      0,         0,            files_open_mycomputer);
    app_register(22, "System Monitor",   IC_APP,    0,            sysmon_open);
    app_register(32, "Print Manager",    IC_APP,    0,            printman_open);
    app_register(34, "Resource Viewer",  IC_APP,    0,            resviewer_open);
    { extern void vpn_open(void); app_register(78, "Dial-Up Networking", IC_NETWORK, 0, vpn_open); }
}
