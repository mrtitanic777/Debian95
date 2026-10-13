/* Debian95 — shared shell32 icon service. Loads SHELL32.DLL once and extracts
 * RT_ICON resources scaled to a requested size (nearest-neighbour). */
#ifndef DEBIAN95_ICONS_H
#define DEBIAN95_ICONS_H

#include <debian95/types.h>

/* shell32.dll RT_ICON resource IDs we use */
#define IC_GENERIC   6
#define IC_DOC       8
#define IC_APP       10
#define IC_FOLDER    12
#define IC_OPENDIR   14
#define IC_DRIVE     16
#define IC_HELP      52    /* purple "?" help book */
#define IC_SETTINGS  48
#define IC_FIND      50
#define IC_WINLOGO   80    /* the four-colour Windows flag, 32px */
#define IC_SHUTDOWN  60    /* monitor with the Windows logo — the Shut Down item */
#define IC_GRAPHICS  134
/* USER.EXE has the real Start-button flag with motion streaks (native sizes) */
#define UI_FLAG16     5    /* 16x16 flag — the taskbar Start button */
#define UI_FLAG32     1    /* 32x32 flag */
/* desktop shell objects (real RT_ICON ids in SHELL32.DLL) */
#define IC_MYCOMPUTER 36
#define IC_DESKTOP    34   /* the Explorer tree "Desktop" root node */
#define IC_NETWORK    40
#define IC_NETWORK16  41   /* 16px network (two computers) — tray icon */
#define IC_RECYCLE    69   /* empty recycle bin */
#define IC_RECYCLE_FULL 67 /* recycle bin with items */

/* Fill out[size*size] with icon `id` scaled to size; transparent pixels get
 * `bg`. Returns 0 on success (SHELL32.DLL present and the icon exists). */
int icon_get(u16 id, int size, u32 bg, u32 *out);
/* Same, from a named Win95 icon module (upper-case FAT name), e.g. "USER.EXE",
 * "PIFMGR.DLL", "MORICONS.DLL". Modules are loaded once and kept resident. */
int icon_get_from(const char *module, u16 id, int size, u32 bg, u32 *out);
/* A 16x16 image from the real COMCTL32 standard small toolbar strip
 * (IDB_STD_SMALL_COLOR). index: STD_* (Cut=0,Copy=1,Paste=2,...,New=6,Open=7,
 * Save=8,...). Scaled to `size`; the button-face background maps to `bg`. 0=ok. */
int toolbar_std(int index, int size, u32 bg, u32 *out);
/* Any COMCTL32 toolbar strip: res 120=standard, 124=view (Parent folder=8),
 * 130=history (Back=0, Forward=1). */
int toolbar_img(u16 res, int index, int size, u32 bg, u32 *out);
#define TBV_PARENTFOLDER 8      /* "Up one level" in the view strip (res 124) */
/* documented STD_* indices into the standard toolbar strip */
enum { TBI_CUT=0, TBI_COPY=1, TBI_PASTE=2, TBI_UNDO=3, TBI_REDO=4, TBI_DELETE=5,
       TBI_NEW=6, TBI_OPEN=7, TBI_SAVE=8, TBI_PRINTPRE=9, TBI_PROPERTIES=10,
       TBI_HELP=11, TBI_FIND=12, TBI_REPLACE=13, TBI_PRINT=14 };

/* USER.EXE message-box icons (RT_ICON ids) */
#define UI_WARNING   7    /* yellow ! triangle  */
#define UI_QUESTION  9    /* ? speech bubble    */
#define UI_ERROR    11    /* red X circle       */
#define UI_INFO     13    /* i speech bubble    */

#endif /* DEBIAN95_ICONS_H */
