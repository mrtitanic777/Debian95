/* Debian95 — desktop theme engine. One active LgTheme drives the whole shell's
 * look (colors, metrics, chrome style) so the desktop can be fully restyled
 * between eras: Windows 95 / 98 / XP (Luna) / a flat "newer" look. The shell
 * reads colors/metrics from the active theme instead of hardcoded constants, so
 * a theme switch restyles everything from the ground up. */
#ifndef DEBIAN95_THEME_H
#define DEBIAN95_THEME_H

#include <debian95/types.h>

enum { THEME_95 = 0, THEME_98 = 1, THEME_XP = 2, THEME_FLAT = 3,
       THEME_XP_OLIVE = 4, THEME_XP_SILVER = 5, THEME_VISTA = 6, THEME_WIN7 = 7,
       THEME_DESERT = 8, THEME_ROSE = 9, THEME_EGGPLANT = 10, THEME_SPRUCE = 11,
       THEME_BRICK = 12, THEME_HC_BLACK = 13,
       THEME_COUNT = 14 };

/* control SHAPE/render style (colours are separate, above) — lets a theme change
 * the actual geometry of chrome, not just its palette. */
enum { STYLE_CLASSIC = 0,   /* 95/98: square 3D bevels */
       STYLE_XP      = 1,   /* Luna: rounded corners, glossy gradient buttons, coloured caption buttons */
       STYLE_FLAT    = 2 }; /* modern: flat 1px controls, accent fills */

/* desktop UX ERA — drives real behaviour, not just colour: the Start button shape,
 * the Start-menu layout, taskbar style and window behaviours all key off this. */
enum { ERA_CLASSIC = 0,  /* 95/98/2000 + classic schemes: flag Start button, single-column cascade menu */
       ERA_XP      = 1,  /* XP/Luna: green pill Start, two-pane menu, grouped buttons */
       ERA_AERO    = 2,  /* Vista/7: round orb Start, two-pane + search, icon superbar, snap */
       ERA_FLAT    = 3 };/* modern flat */

typedef struct LgTheme {
    const char *name;
    int  style;              /* STYLE_* — how chrome is drawn */
    int  era;                /* ERA_* — desktop UX behaviour (Start button/menu/taskbar) */
    u32  close_btn;          /* caption close-button colour (XP red / flat accent) */
    u32  minmax_btn;         /* caption min/max button colour (XP blue) */
    u32  accent;             /* default/hover accent (flat) */

    /* ---- base palette ---- */
    u32 face, light, shadow, dkshadow, white, black;
    u32 text, gray_text;
    u32 window_bg;            /* control/interior white area */
    u32 desktop;             /* desktop background colour */
    u32 sel, sel_text;       /* selection highlight + its text */

    /* ---- caption / title bar ---- */
    u32 title_lo, title_hi;      /* active caption gradient (lo==hi => solid) */
    u32 title_ilo, title_ihi;    /* inactive caption gradient */
    u32 title_text, title_itext; /* caption text active / inactive */
    int title_h;                 /* caption height (px) */
    int title_gradient;          /* 1 = horizontal gradient, 0 = solid */

    /* ---- window frame / controls ---- */
    int border;                  /* window border thickness */
    int corner;                  /* corner radius; 0 = square (95/98), >0 = rounded (XP) */
    int flat;                    /* 1 = flat 1px controls (newer), 0 = 3D bevels */

    /* ---- taskbar / Start ---- */
    int taskbar_h;
    u32 taskbar_lo, taskbar_hi;  /* taskbar gradient (solid face if lo==hi) */
    u32 start_lo, start_hi;      /* Start button gradient (XP green; face if equal) */
    u32 start_text;

    /* ---- optional renderer overrides (NULL => the default 3D routines) ---- */
    void (*bevel_raised)(int x, int y, int w, int h);
    void (*bevel_sunken)(int x, int y, int w, int h);
} LgTheme;

/* the active theme (never NULL after theme_init) */
extern const LgTheme *g_theme;

void        theme_init(void);        /* load the persisted theme (default 95) */
void        theme_set(int id);       /* switch + persist; triggers a full repaint */
int         theme_id(void);          /* current theme id */
const char *theme_name(int id);      /* display name for a theme id */

#endif /* DEBIAN95_THEME_H */
