/* Debian95 — window manager: managed, draggable, focusable windows composited
 * to a double-buffered framebuffer, plus a taskbar and start menu. */
#ifndef DEBIAN95_WM_H
#define DEBIAN95_WM_H

#include <debian95/types.h>
#include <debian95/cursor.h>

#define WM_TITLEH 18

/* 3D bevels (fill the face yourself first). */
void wm_bevel_raised(int x, int y, int w, int h);
void wm_bevel_sunken(int x, int y, int w, int h);

typedef struct Window Window;

/* Register windows. A text window shows body lines; a custom window paints its
 * client area through the callback (used by the shell console). */
Window *wm_add_text(int x, int y, int w, int h, const char *title,
                    const char **lines, int nlines);
Window *wm_add_custom(int x, int y, int w, int h, const char *title,
                      void (*paint)(int cx, int cy, int cw, int ch),
                      void (*onkey)(char c),
                      void (*onclick)(int rx, int ry));

/* Drop-down menu bar (File / Edit / View ...). An item with id < 0 is a
 * separator. wm_set_menu attaches a bar to a window; clicks fire onmenu(id). */
/* layout-identical to MenuItemW32 (win32.h): checked -> check mark, disabled ->
 * grayed + unclickable. SetMenuW32 casts between the two, so keep them in sync. */
typedef struct { const char *label; int id; int checked; int disabled; } WmMenuItem;
typedef struct { const char *label; const WmMenuItem *items; int nitems; } WmMenu;
void wm_set_menu(Window *win, const WmMenu *menus, int n, void (*onmenu)(int id));
void wm_set_menuflags(Window *win, int (*cb)(int id));   /* dynamic check/disable state */

/* apps show a right-click context menu at the cursor; picks call cb(act) */
void wm_context_menu(const char **items, const int *acts, int n, void (*cb)(int act));
/* a nested fly-out attached to a parent item (e.g. Explorer's "Send To >") */
typedef struct { const char **items; const int *acts; int n; } WmContextSub;
void wm_context_menu_sub(const char **items, const int *acts,
                         const WmContextSub *const *subs, int n, void (*cb)(int act));

void wm_client_rect(Window *win, int *cx, int *cy, int *cw, int *ch);

/* task enumeration for Task Manager (the same window set the taskbar shows) */
typedef struct { const char *title; void *win; int minimized; } WmTaskInfo;
int  wm_enum_tasks(WmTaskInfo *out, int max);   /* fills out[], returns count */
void wm_activate(void *win);                     /* Switch To: raise + unminimize */
void wm_close_task(void *win);                   /* End Task: same as clicking [X] */

/* Foreign X11 apps on the taskbar (Desktop Edition): the platform layer mirrors
 * each window debian95-wm frames into an "external" taskbar button. Never drawn or
 * hit on our framebuffer; a click records an action for the platform to send to X. */
void *wm_ext_find(unsigned long xclient);                          /* by X client id, or 0 */
void *wm_ext_add(unsigned long xframe, unsigned long xclient, const char *title);
void  wm_ext_set_title(void *win, const char *title);
void  wm_ext_set_min(void *win, int minimized);                    /* sync from the frame's map state */
void  wm_ext_del(void *win);                                       /* the X client vanished */
int   wm_ext_enum(unsigned long *out, int max);                    /* current external X client ids */
void  wm_ext_set_active(unsigned long xclient);                    /* the X-focused external (0 = none) */
int   wm_take_ext_action(unsigned long *client);                   /* 0/1 activate/2 minimize/3 close */

/* register a downloaded/native app (VLC, Chromium, ...) so it gets a real desktop
 * icon + Programs entry, launched via the normal action dispatch (app_launch(act)).
 * icon32 = a 32x32 keyed (ICON_KEY-transparent) pixel buffer, or 0 for a fallback. */
void wm_register_program(const char *name, int act, const u32 *icon32, int desktop);
/* cat: 0 Internet, 1 Multimedia, 2 Graphics, 3 Office, -1 = flat at top level */
void wm_register_program_cat(const char *name, int act, const u32 *icon32, int desktop, int cat);
int  wm_ext_prog_count(void);          /* how many external programs are registered */
const char *wm_ext_prog_name(int i);
int  wm_ext_prog_act(int i);

void wm_show(Window *win);          /* make visible + bring to front */
void wm_set_title(Window *win, const char *title);
void wm_set_icon(Window *win, u16 shell32_icon_id);   /* caption/taskbar icon (IC_* from icons.h) */
void wm_set_help(Window *win, const char *help);      /* "What's This?" text (adds a ? caption button) */
void wm_set_icon_mod(Window *win, const char *module, u16 icon_id);   /* icon from any module, e.g. "MSPAINT.EXE" */
void wm_hide(Window *win);          /* hide a window */
void wm_dispatch_key(char c);       /* deliver a key to the focused window */
void wm_set_motion(Window *win, void (*fn)(int rx, int ry, u8 buttons));
void wm_messagebox(const char *title, const char *text);                 /* information */
/* severity: 0 = info, 1 = warning, 2 = error/stop, 3 = question */
#define MB_INFO 0
#define MB_WARN 1
#define MB_ERROR 2
#define MB_QUESTION 3
void wm_messagebox_ex(const char *title, const char *text, int severity);
void wm_confirm(const char *title, const char *text, void (*onyes)(void));   /* Yes/No dialog */

/* Compose the whole desktop into the (back) buffer. Call fb_present() after,
 * then wm_cursor_overlay() to composite the cursor onto the scanout. */
void wm_compose(void);
/* draw the software cursor onto the scanout after a full present */
void wm_cursor_overlay(void);
/* a cursor-only move: erase the old cursor + draw the new one (no full recompose).
 * Use when wm_mouse() returns 2. */
void wm_cursor_move(void);

/* monotonic milliseconds, provided by the platform (double-click timing) */
u32 wm_now_ms(void);
int wm_hover_tick(void);                 /* hover tooltips; returns 1 when a tip shows/hides */
int wm_anim_tick(void);                  /* minimize/maximize zoom; returns 1 while animating */
void wm_set_busy(int on);                /* show the hourglass cursor (auto-clears) */
int  wm_busy_tick(void);                 /* advance the busy timer; 1 when it clears */
void wm_set_removable(int on);           /* show/hide the Safely Remove Hardware tray icon */
void wm_toggle_gadgets(void);            /* show/hide the desktop gadget sidebar */
void wm_set_gadgets(int on);
int  wm_gadgets_tick(void);              /* ~1Hz: refresh gadget stats; 1 if shown */
void wm_logon_switch_user(void);         /* Log Off: account picker + shadow auth -> profile switch */
const char *wm_current_user(void);       /* the logged-in account name */
int  wm_take_profile_switched(void);     /* 1 once after a user switch (platform re-applies the profile) */
void wm_anim_hold(int on);               /* test: freeze the zoom mid-frame for a screenshot */
void wm_test_min_front(void);            /* test: minimize the front window (to exercise the zoom) */
void wm_test_marquee(void);              /* test: assert a marquee box over the left icon column */
void wm_test_open_menu(int idx);         /* test: open the front window's menu-bar dropdown idx */
/* blinking text caret: fields draw their caret only when wm_caret_on(); the
 * platform recomposes while wm_wants_caret() so it visibly blinks. */
int wm_caret_on(void);
int wm_wants_caret(void);
/* screen saver: activates after idle, any input dismisses it */
void wm_set_screensaver_by_name(const char *name);   /* choose the saver renderer */
void wm_screensaver_start(void);
void wm_screensaver_stop(void);
int  wm_screensaver_active(void);
int  wm_screensaver_tick(void);            /* animate one frame; returns 1 while active */

/* Feed one mouse packet: updates the cursor and does focus/drag/close/taskbar
 * interaction. Returns 1 if the screen should be recomposed. */
int  wm_mouse(int dx, int dy, u8 buttons);
/* Feed an ABSOLUTE pointer position (a tablet: virtio-tablet, VMware/VBox mouse
 * integration). Returns the same code as wm_mouse (2 = cursor-only move). */
int  wm_mouse_abs(int x, int y, u8 buttons);

/* theme (Control Panel Appearance/Display applets adjust these live) */
void wm_set_desktop_color(u32 c);
void wm_set_title_colors(u32 l, u32 r);
void wm_set_wallpaper_enabled(int on);
void wm_set_swap_buttons(int on);          /* Mouse applet: swap left/right buttons */
void wm_set_dclick_ms(int ms);             /* Mouse applet: double-click speed (100-900ms) */
void wm_set_time_24h(int on);              /* Regional applet: 24-hour tray clock */
void wm_set_show_clock(int on);            /* Taskbar Properties: show/hide tray clock */
void wm_set_toolbar_address(int on);       /* Toolbars menu: show/hide the taskbar Address deskband */
void wm_set_toolbar_links(int on);         /* Toolbars menu: show/hide the taskbar Links deskband */
int  wm_links_count(void);                 /* Links store (deskband.c editor uses these) */
const char *wm_link_name(int i);
const char *wm_link_url(int i);
void wm_links_add(const char *name, const char *url);
void wm_links_remove(int i);
void wm_taskbar_props(void);               /* Start > Settings > Taskbar & Start Menu */
void wm_set_high_contrast(int on);         /* Accessibility: High Contrast Black scheme */
void wm_set_mods(int ctrl, int shift);     /* platform reports current modifier state */
int  wm_mod_ctrl(void);                    /* Ctrl held? (for Ctrl+click multi-select) */
int  wm_mod_shift(void);                   /* Shift held? (for Shift+click range select) */

void control_open(void);                /* subsystems/win32/control.c — Control Panel */
void applet_open(int applet);           /* subsystems/win32/applets.c — a CP applet (2 = Display) */
void run_open(void);                     /* subsystems/win32/shelldlg.c — Run... dialog */
void shutdown_open(void);                /* subsystems/win32/shelldlg.c — Shut Down dialog */
void welcome_open(void);                 /* subsystems/win32/shelldlg.c — first-run setup wizard */
void welcome_tips_open(void);            /* subsystems/win32/shelldlg.c — "Did you know..." tips */
void wm_post_action(int act);            /* queue an action id for the platform to dispatch */
/* first-run setup (OOBE): hide the desktop/taskbar/icons behind the wizard until done */
void wm_setup_begin(void);
void wm_setup_end(void);
int  wm_setup_active(void);
/* shutdown sequence (Win95 "shutting down" -> "safe to turn off") */
void wm_shutdown_begin(int restart);
int  wm_shutdown_active(void);
int  wm_shutdown_restart(void);
int  wm_shutdown_phase(void);        /* 0 shutting down, 1 safe-to-turn-off, 2 do it now */
void wm_shutdown_draw(void);

void wm_set_wallpaper(const void *lgb, u32 len);   /* .LGB image as the background */
void wm_set_wallpaper_px(const u32 *px, int w, int h);  /* decoded pixels (tiled if small) */
void wm_set_wallpaper_mode(int m);   /* 0 = Tile, 1 = Center, 2 = Stretch */
int  wm_wallpaper_mode(void);
void wm_set_cursor(const Debian95Cursor *c);                /* the arrow pointer (see cursor.h) */
void wm_set_cursor_ibeam(const Debian95Cursor *c);          /* I-beam for text areas */
void wm_set_cursor_size(const Debian95Cursor *c);           /* NW-SE resize cursor */
void wm_set_text_cursor(Window *w);                      /* mark a window's client as a text area */
int  wm_take_action(void);    /* pending start-menu action: 0=none, 1=run, 2=shutdown */
void wm_toggle_start(void);   /* Ctrl+Esc: open/close the Start menu */
void wm_close_active(void);   /* Alt+F4: close the focused window */
void wm_alt_tab(void);        /* Alt+Tab: cycle the task switcher */
void wm_sysmenu(void);        /* Alt+Space: open the focused window's system menu */
int  wm_alt_active(void);     /* is the Alt+Tab switcher showing? */
void wm_alt_commit(void);     /* Alt released: activate the highlighted window */

/* the "Welcome to Windows" logon modal shown before the desktop is usable */
void wm_logon_begin(void);
void wm_logon_lock(void);     /* lock the session: re-auth (verify) while /data stays mounted */
int  wm_logon_active(void);
/* progressive desktop populate after login (icons reveal one-by-one). The
 * platform calls wm_boot_tick() each frame and recomposes while it returns 1. */
void wm_boot_start(void);
int  wm_boot_tick(void);

void desktop_show(void);      /* simple static desktop (wallpaper + taskbar) */
void desktop_run(void) __attribute__((noreturn));

/* apps */
void wm_add_recent(const char *path); /* add a file to the Documents MRU (Start menu) */
int  app_launch_doc(int id, const char *path);   /* open app `id` on a document (appreg.c) */
void editor_open(const char *name);   /* subsystems/win32/editor.c */
void files_open(void);                 /* subsystems/win32/filemgr.c */
void files_open_mycomputer(void);      /* Explorer at the My Computer root */
void paint_open(void);                 /* subsystems/win32/paint.c */
void viewer_open(const char *name);    /* subsystems/win32/viewer.c */
void help_open(void);                   /* subsystems/win32/apps.c */
void sysmon_open(void);                  /* subsystems/win32/apps.c - System Monitor */
int  sysmon_tick(void);                  /* sample CPU/memory ~1/s; returns 1 if it redrew */
void find_open(void);                  /* subsystems/win32/apps.c */
void browser_open(void);                /* subsystems/win32/browser.c */
void minesweeper_open(void);            /* subsystems/win32/minesweeper.c */
void calc_open(void);                    /* subsystems/win32/calc.c */
void charmap_open(void);                  /* subsystems/win32/charmap.c */
void msdos_open(void);                     /* subsystems/win32/msdos.c */
void clipbrd_open(void);                    /* subsystems/win32/clipbrd.c — Clipboard Viewer */
void recycle_open(void);                     /* subsystems/win32/recycle.c — Recycle Bin */
void recycle_add(const char *fullpath);      /* move a file into the Recycle Bin */
/* a shared text clipboard (Character Map Copy, Notepad, etc.) */
void        wm_clipboard_set(const char *text);
const char *wm_clipboard_get(void);
void clock_open(void);                    /* subsystems/win32/clock.c */
void solitaire_open(void);                /* subsystems/win32/solitaire.c */
void wm_set_rclick(Window *win, void (*fn)(int rx, int ry));  /* right-click in client */
void wm_set_close(Window *win, void (*fn)(void));   /* app cleanup on window destroy */
void wm_set_sizegrip(Window *win);                  /* draw the diagonal corner size grip */
void wm_set_size(Window *win, int w, int h);        /* resize a window at runtime */
void wm_destroy(Window *win);                       /* close+terminate: run cleanup, remove window */
int  wm_is_visible(Window *win);
void mediaplayer_open(const char *name);   /* subsystems/win32/mediaplayer.c */
void freecell_open(void);                  /* subsystems/win32/freecell.c (Win32-idiom app) */
void volume_open(void);                    /* subsystems/win32/volume.c (Volume Control) */
void soundrec_open(void);                  /* subsystems/win32/soundrec.c (Sound Recorder) */
void soundrec_open_file(const char *name); /* open Sound Recorder on a specific .WAV */
void wordpad_open(const char *name);       /* subsystems/win32/wordpad.c (WordPad) */
void wm_volume_popup(void);                /* open the tray speaker's mini volume popup */
int  mediaplayer_tick(void);               /* advance animation; returns 1 if a frame changed */

#endif /* DEBIAN95_WM_H */
