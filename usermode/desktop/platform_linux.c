/* Debian95 desktop - Linux platform layer (SDL2). The shared WM, chrome, font, apps
 * and codecs draw into a software framebuffer; this back-end creates the window,
 * pumps input into the WM, and pushes the composed frame to the screen. It runs as
 * the X session on the Debian image (under debian95-wm) and as a portable app on any
 * Linux desktop for development. */
#include <debian95/types.h>
#include <debian95/fb.h>
#include <debian95/wm.h>
#include <debian95/font.h>
#include <debian95/img.h>
#include <debian95/appreg.h>
#include <debian95/win32.h>
#include <debian95/theme.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

int  FsReadAlloc(const char *name, u8 **out, u32 *size);   /* shims_linux.c */

/* app launchers (subsystems/win32) */
void files_open(void); void paint_open(void); void editor_open(const char *);
void clock_open(void); void solitaire_open(void);   /* calc / minesweeper are plugins now */
void control_open(void); void applet_open(int);
int  modules_load(const char *dir);                 /* module_loader.c: load app plugins */
void run_open(void); void shutdown_open(void); void find_open(void); void help_open(void);
void terminal_open(void); int terminal_tick(void);   /* terminal_linux.c */
void terminal_run(const char *cmd);                   /* terminal_linux.c (run a command) */
void clippy_open(void); int clippy_tick(void);        /* subsystems/win32/clippy.c (AI assistant) */
void scandisk_open(void); int scandisk_tick(void);   /* systools.c */
void defrag_open(void);   int defrag_tick(void);     /* systools.c */
void snd_play(const char *name);                      /* shims_linux.c */
void snd_event(const char *ev);                       /* shims_linux.c */
void sys_session_login(const char *user);             /* shims_linux.c -> /dev/debian95 */
void sys_shutdown(int restart);                        /* shims_linux.c -> systemctl poweroff/reboot */
int  wm_mouse_abs(int x, int y, unsigned char buttons);/* wm.c (test hover) */
int  sys_get_setting(const char *key, char *out, int n);   /* shims_linux.c */
int  sys_has_password(const char *user);                   /* shims_linux.c */
int  sys_vnc_available(void);                              /* shims_linux.c (Remote Access) */
int  sys_vnc_running(void);
int  sys_vnc_has_password(void);
int  sys_vnc_set_password(const char *pw);
int  sys_vnc_start(void);
void sys_vnc_info(char *ip, int iplen, int *port);

#define SCRW 1024
#define SCRH 768

/* monotonic millisecond clock for the WM (double-click timing) */
u32 wm_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* text-caret blink half-period (ms); the Keyboard applet's "Cursor blink rate"
 * slider changes it live and persists it. Win95 default is ~530ms. */
int g_caret_ms = 530;
void sys_set_caret_ms(int ms) { if (ms >= 100 && ms <= 2000) g_caret_ms = ms; }

void ext_x11_publish_theme(void);   /* defined below; publishes theme colours to debian95-wm */
int  sys_launch_app(const char *cmd);   /* shims_linux.c */
/* installed apps discovered from .desktop files get action ids DYN_APP_BASE+i
 * (200+, clear of the theme range 90..90+THEME_COUNT which now reaches ~103) */
#define DYN_APP_BASE 200
static char g_dynexec[48][192]; static int g_ndyn;
/* pending Start-menu / desktop action -> open the app (mirrors arch/arm64 gui.c) */
static void dispatch_action(int act) {
    if (act >= DYN_APP_BASE && act < DYN_APP_BASE + g_ndyn) {   /* a discovered installed app */
        sys_launch_app(g_dynexec[act - DYN_APP_BASE]); return;
    }
    if (act >= 90 && act < 90 + THEME_COUNT) {                 /* Start > Settings > Theme > <era/scheme> */
        theme_set(act - 90); ext_x11_publish_theme(); return;
    }
    if (act != 2 && act != 30) snd_event("open");    /* the "Open program" event sound */
    if (app_launch(act)) return;                     /* registered apps (see appreg.c) */
    switch (act) {                                   /* non-app shell actions */
        case 2:  shutdown_open(); break;
        case 30: run_open(); break;
        case 6:  control_open(); break;
        case 20: applet_open(2); break;              /* Display properties */
        case 21: wm_taskbar_props(); break;
        case 19: applet_open(9); break;              /* Network Neighborhood -> Network applet */
        case 40: scandisk_open(); break;             /* System Tools > ScanDisk */
        case 41: defrag_open(); break;               /* System Tools > Disk Defragmenter */
        case 50: terminal_run("debian95-install"); break;   /* Install Debian95 to an internal disk */
        case 51: wm_logon_lock(); break;                    /* Lock Computer */
        case 52: wm_logon_switch_user(); break;             /* Log Off (switch user) */
        case 53: wm_toggle_gadgets(); break;                /* Desktop Gadgets on/off */
        default: break;   /* theme acts 90.. handled above the switch */
    }
}

/* re-apply the current user's profile (wallpaper + theme + a few display prefs)
 * after a Log Off / switch-user. Settings now resolve to the new user's store. */
static void plat_apply_profile(void) {
    u8 *b; u32 l; u32 *wpx; int ww, wh; char v[40];
    const char *wall = 0;
    if (sys_get_setting("wallpaper", v, sizeof v) == 0 && v[0]) wall = v[0] == '(' ? 0 : v;
    if (wall && *wall && FsReadAlloc(wall, &b, &l) == 0 && bmp_decode(b, l, &wpx, &ww, &wh) == 0) {
        wm_set_wallpaper_px(wpx, ww, wh); wm_set_wallpaper_enabled(1);
    } else { static u32 wp[4]; for (int i = 0; i < 4; i++) wp[i] = RGB(0x00,0x80,0x80); wm_set_wallpaper_px(wp, 2, 2); }
    { char m[8]; if (sys_get_setting("wallpaper_mode", m, sizeof m) == 0 && m[0]) wm_set_wallpaper_mode(atoi(m)); }
    theme_init(); ext_x11_publish_theme();                 /* restyle the shell + framed apps */
    { char hc[8]; wm_set_high_contrast(sys_get_setting("acc_hc", hc, sizeof hc) == 0 && atoi(hc)); }
    { char t[8]; if (sys_get_setting("time_24h", t, sizeof t) == 0 && t[0]) wm_set_time_24h(atoi(t)); }
}

/* ---- launching downloaded apps (VLC, Chromium) from a desktop icon / Programs ----
 * sys_launch_app (shims_linux.c) forks, drops root -> the 'debian95' user, and execs
 * the command with the X display. Registered as normal apps so a click dispatches
 * here via app_launch(). NEVER auto-started - only on the user's click. */
int sys_launch_app(const char *cmd);                       /* shims_linux.c */
static void open_vlc(void) {
    sys_launch_app("vlc --no-qt-privacy-ask --intf qt --vout=x11");
}
static void open_chrome(void) {
    sys_launch_app("chromium --no-sandbox --no-first-run --no-default-browser-check "
                   "--disable-gpu --disable-dev-shm-usage --user-data-dir=/tmp/debian95-home/cr");
}
static void open_pcmanfm(void)  { sys_launch_app("pcmanfm /usr/share"); }
static void open_gpicview(void) { sys_launch_app("gpicview /usr/share/icons/hicolor/256x256/apps/vlc.png"); }
static void open_mousepad(void)  { sys_launch_app("mousepad"); }
static void open_xarchiver(void) { sys_launch_app("xarchiver"); }
static void open_dosbox(void)    { sys_launch_app("dosbox"); }                       /* real DOS environment */
static void open_remmina(void)   { sys_launch_app("remmina"); }                      /* RDP / VNC remote desktop */
static void open_retroarch(void) { sys_launch_app("retroarch"); }                    /* retro game arcade */
/* decode a staged icon file (a keyed BMP) into a 32x32 buffer; 1 on success */
static int load_icon32(const char *file, u32 *out) {
    u8 *d; u32 n; if (FsReadAlloc(file, &d, &n) != 0) return 0;
    u32 *px; int w, h; int r = bmp_decode(d, n, &px, &w, &h); free(d);
    if (r != 0 || w <= 0 || h <= 0) return 0;
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { int sx = x * w / 32, sy = y * h / 32; out[y * 32 + x] = px[sy * w + sx]; }
    free(px); return 1;
}
/* register the downloaded apps present in this image (desktop icon + Programs) */
static void register_downloaded_apps(void) {
    static u32 vlc_ic[32 * 32], cr_ic[32 * 32], pm_ic[32 * 32], iv_ic[32 * 32];
    if (access("/usr/bin/vlc", X_OK) == 0) {
        app_register(42, "VLC media player", 0, 0, open_vlc);
        wm_register_program_cat("VLC media player", 42, load_icon32("vlc.bmp", vlc_ic) ? vlc_ic : 0, 1, 1);  /* Multimedia */
    }
    if (access("/usr/bin/chromium", X_OK) == 0 || access("/usr/bin/google-chrome", X_OK) == 0) {
        app_register(43, "Chromium", 0, 0, open_chrome);
        wm_register_program_cat("Chromium", 43, load_icon32("chrome.bmp", cr_ic) ? cr_ic : 0, 1, 0);  /* Internet */
    }
    if (access("/usr/bin/pcmanfm", X_OK) == 0) {
        app_register(44, "File Manager", 0, 0, open_pcmanfm);
        wm_register_program("File Manager", 44, load_icon32("pcmanfm.bmp", pm_ic) ? pm_ic : 0, 1);
    }
    if (access("/usr/bin/gpicview", X_OK) == 0) {
        app_register(45, "Image Viewer", 0, 0, open_gpicview);
        wm_register_program_cat("Image Viewer", 45, load_icon32("gpicview.bmp", iv_ic) ? iv_ic : 0, 1, 2);  /* Graphics */
    }
    static u32 me_ic[32 * 32], xa_ic[32 * 32];
    if (access("/usr/bin/mousepad", X_OK) == 0) {
        app_register(46, "Text Editor", 0, 0, open_mousepad);
        wm_register_program("Text Editor", 46, load_icon32("mousepad.bmp", me_ic) ? me_ic : 0, 1);
    }
    if (access("/usr/bin/xarchiver", X_OK) == 0) {
        app_register(47, "Archive Manager", 0, 0, open_xarchiver);
        wm_register_program("Archive Manager", 47, load_icon32("xarchiver.bmp", xa_ic) ? xa_ic : 0, 1);
    }
    if (access("/usr/bin/dosbox", X_OK) == 0) {
        app_register(48, "DOS Box", 0, 0, open_dosbox);               /* run real DOS software */
        wm_register_program("DOS Box", 48, 0, 0);
    }
    if (access("/usr/bin/remmina", X_OK) == 0) {
        app_register(49, "Remote Desktop", 0, 0, open_remmina);       /* RDP / VNC client */
        wm_register_program_cat("Remote Desktop", 49, 0, 0, 0);       /* Internet */
    }
    if (access("/usr/bin/retroarch", X_OK) == 0) {
        app_register(71, "Arcade", 0, 0, open_retroarch);             /* RetroArch: NES/SNES/Genesis/... */
        wm_register_program_cat("Arcade", 71, 0, 0, 1);               /* Multimedia */
    }
}

/* Any OTHER installed GUI app (from its .desktop file) shows up in Start > Programs
 * automatically, so `apt install <app>` (or Add/Remove Programs) makes it launchable
 * without code changes. The curated apps above (tuned flags + icons) are skipped. */
struct lg_app { char name[64]; char exec[192]; char icon[64]; int cat; };
int sys_list_apps(struct lg_app *out, int max);
static char g_dynname[48][64];
static int dyn_is_curated(const char *exec) {
    char t[128]; int k = 0;
    for (const char *s = exec; *s && *s != ' ' && k < 127; s++) t[k++] = *s; t[k] = 0;
    char *b = t; for (char *p = t; *p; p++) if (*p == '/') b = p + 1;
    static const char *cur[] = { "vlc","chromium","chromium-browser","pcmanfm","gpicview","mousepad","xarchiver", 0 };
    for (int i = 0; cur[i]; i++) if (!strcmp(b, cur[i])) return 1;
    return 0;
}
/* exec binary basename already registered as a dynamic app? (keeps re-scan idempotent) */
static void exec_base(const char *exec, char *out, int n) {
    char t[192]; int k = 0; for (const char *s = exec; *s && *s != ' ' && k < 191; s++) t[k++] = *s; t[k] = 0;
    char *b = t; for (char *p = t; *p; p++) if (*p == '/') b = p + 1;
    snprintf(out, n, "%s", b);
}
static int dyn_already(const char *exec) {
    char b[192]; exec_base(exec, b, sizeof b);
    for (int i = 0; i < g_ndyn; i++) { char u[192]; exec_base(g_dynexec[i], u, sizeof u); if (!strcmp(b, u)) return 1; }
    return 0;
}
/* Scan /usr/share/applications and register any NOT-yet-registered GUI app into
 * Start > Programs. Idempotent, so it doubles as a live re-scan: apps installed via
 * Add/Remove Programs appear without a restart. Returns how many were newly added. */
static int register_installed_apps(void) {
    struct lg_app apps[64]; int n = sys_list_apps(apps, 64); int added = 0;
    for (int i = 0; i < n && g_ndyn < 48; i++) {
        if (dyn_is_curated(apps[i].exec) || dyn_already(apps[i].exec)) continue;
        snprintf(g_dynexec[g_ndyn], sizeof g_dynexec[g_ndyn], "%s", apps[i].exec);
        snprintf(g_dynname[g_ndyn], sizeof g_dynname[g_ndyn], "%s", apps[i].name);
        wm_register_program_cat(g_dynname[g_ndyn], DYN_APP_BASE + g_ndyn, 0, 0, apps[i].cat);   /* grouped in Programs */
        g_ndyn++; added++;
    }
    return added;
}

/* ============================ back-end interface ============================ */
static int  plat_init(void);      /* set up fb_init_direct + input; 0 on success */
static int  plat_input(void);     /* pump input into the WM; return 1 if dirty   */
static void plat_present(void);   /* push the composed frame to the screen        */

/* ================================ SDL2 back-end ============================= */
#ifdef USE_SDL
#include <SDL2/SDL.h>
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static u32          *g_px;        /* the frame buffer fb.c draws into */
static int g_lx, g_ly, g_have_last;
static int g_scrw = SCRW, g_scrh = SCRH;   /* current SDL surface size */

static int plat_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1; }
    /* Render at the ACTUAL display size so debian95-wm's "fill the screen" is a 1:1
     * map, not an ugly stretch of a fixed 1024x768 surface. Priority: DEBIAN95_SCREEN=WxH
     * (the session can pin it), else the current display mode, else 1024x768. The Win95
     * shell is resolution-independent (taskbar/wallpaper/icons use fb_width/height). */
    int w = SCRW, h = SCRH;
    const char *env = getenv("DEBIAN95_SCREEN");
    SDL_DisplayMode dm;
    if (env && *env) { int ew, eh; if (sscanf(env, "%dx%d", &ew, &eh) == 2 && ew >= 640 && eh >= 480) { w = ew; h = eh; } }
    else if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w >= 640 && dm.h >= 480) { w = dm.w; h = dm.h; }
    g_scrw = w; g_scrh = h;
    g_win = SDL_CreateWindow("Debian95", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!g_win || !g_ren || !g_tex) { fprintf(stderr, "SDL setup: %s\n", SDL_GetError()); return -1; }
    g_px = (u32 *)malloc((size_t)w * h * 4);
    fb_init_direct((u64)(uintptr_t)g_px, w, h, w * 4);
    SDL_ShowCursor(SDL_DISABLE);                 /* the WM draws its own cursor */
    printf("sdl: %dx%d window (runs on any Linux desktop)\n", w, h);
    return 0;
}
static int plat_input(void) {
    int full = 0, curs = 0; static u8 btn;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: exit(0);
            case SDL_MOUSEMOTION: {
                int dx, dy;
                if (!g_have_last) { g_lx = e.motion.x; g_ly = e.motion.y; g_have_last = 1; }
                dx = e.motion.x - g_lx; dy = e.motion.y - g_ly;
                g_lx = e.motion.x; g_ly = e.motion.y;
                if (dx || dy) { int rc = wm_mouse(dx, -dy, btn); if (rc == 2) curs = 1; else if (rc) full = 1; }
                break;
            }
            case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    SDL_Keymod m = SDL_GetModState(); wm_set_mods(m & KMOD_CTRL, m & KMOD_SHIFT);
                    btn = (e.type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
                    if (wm_mouse(0, 0, btn)) full = 1;
                }
                break;
            case SDL_TEXTINPUT:                                   /* printable chars */
                for (const char *p = e.text.text; *p; p++) { wm_dispatch_key(*p); full = 1; }
                break;
            case SDL_KEYDOWN: {
                char c = 0;
                if ((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_ESCAPE) { wm_toggle_start(); full = 1; break; }
                if ((e.key.keysym.mod & KMOD_ALT) && e.key.keysym.sym == SDLK_F4) { wm_close_active(); full = 1; break; }
                if ((e.key.keysym.mod & KMOD_ALT) && e.key.keysym.sym == SDLK_TAB) { wm_alt_tab(); full = 1; break; }
                if ((e.key.keysym.mod & KMOD_ALT) && e.key.keysym.sym == SDLK_SPACE) { wm_sysmenu(); full = 1; break; }
                switch (e.key.keysym.sym) {
                    case SDLK_RETURN: case SDLK_KP_ENTER: c = '\n'; break;
                    case SDLK_BACKSPACE: c = '\b'; break;
                    case SDLK_TAB: c = '\t'; break;
                    case SDLK_ESCAPE: c = 27; break;
                    case SDLK_F5: c = 0x85; break;             /* Notepad: insert time/date */
                }
                if (c) { wm_dispatch_key(c); full = 1; }
                break;
            }
            case SDL_KEYUP:
                if ((e.key.keysym.sym == SDLK_LALT || e.key.keysym.sym == SDLK_RALT) && wm_alt_active()) { wm_alt_commit(); full = 1; }
                break;
        }
    }
    return full ? 1 : (curs ? 2 : 0);
}
static void plat_present(void) {
    /* Upload only the damaged region. A streaming texture keeps its prior
     * contents outside the updated rect, so RenderCopy of the whole texture
     * still shows a correct frame. A plain mouse move thus uploads a few
     * hundred pixels instead of the entire ~4MB buffer. */
    int dx, dy, dw, dh;
    if (fb_dirty_get(&dx, &dy, &dw, &dh)) {
        SDL_Rect r = { dx, dy, dw, dh };
        SDL_UpdateTexture(g_tex, &r, g_px + (size_t)dy * g_scrw + dx, g_scrw * 4);
    } else {
        SDL_UpdateTexture(g_tex, 0, g_px, g_scrw * 4);   /* nothing tracked: full upload */
    }
    fb_dirty_reset();
    SDL_RenderCopy(g_ren, g_tex, 0, 0);
    /* one-shot screenshot for headless verification (DEBIAN95_SHOT=path.ppm).
     * Read back from the RENDERER (not g_px) so it captures what the GPU texture
     * actually holds after the damage-limited upload — this validates the
     * dirty-rect path, which a g_px dump would silently paper over. */
    static int shot; const char *sp = getenv("DEBIAN95_SHOT");
    if (sp && shot++ == 90) {                    /* ~1.3s in: after startup output settles */
        u32 *rb = (u32 *)malloc((size_t)g_scrw * g_scrh * 4);
        if (rb && SDL_RenderReadPixels(g_ren, 0, SDL_PIXELFORMAT_RGB888, rb, g_scrw * 4) == 0) {
            FILE *f = fopen(sp, "wb");
            if (f) { fprintf(f, "P6\n%d %d\n255\n", g_scrw, g_scrh);
                for (int i = 0; i < g_scrw * g_scrh; i++) { u32 p = rb[i];
                    fputc((p >> 16) & 0xFF, f); fputc((p >> 8) & 0xFF, f); fputc(p & 0xFF, f); }
                fclose(f); }
        }
        free(rb);
    }
    SDL_RenderPresent(g_ren);
}
/* SDL: offer the common desktop resolutions for the Display applet */
int sys_list_resolutions(int *ws, int *hs, int max) {
    static const int R[][2] = { {640,480},{800,600},{1024,768},{1152,864},{1280,1024} };
    int n = (int)(sizeof R / sizeof R[0]); if (n > max) n = max;
    for (int i = 0; i < n; i++) { ws[i] = R[i][0]; hs[i] = R[i][1]; }
    return n;
}
/* SDL: the OS/toolkit handles key auto-repeat, so this is a no-op success */
int sys_set_key_repeat(int delay_ms, int rate_cps) { (void)delay_ms; (void)rate_cps; return 0; }
/* SDL resolution change (testing): resize the window/texture + backbuffer */
int sys_set_resolution(int w, int h) {
    SDL_SetWindowSize(g_win, w, h);
    if (g_tex) SDL_DestroyTexture(g_tex);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
    free(g_px); g_px = (u32 *)malloc((size_t)w * h * 4);
    g_scrw = w; g_scrh = h;
    fb_free_backbuffer();
    fb_init_direct((u64)(uintptr_t)g_px, w, h, w * 4);
    fb_enable_backbuffer();
    return 0;
}
static void plat_wait(void) { SDL_Delay(15); }

#endif   /* USE_SDL */

/* foreign-app taskbar bridge (X11): implemented in xbridge_linux.c, a separate TU
 * because Xlib and debian95/wm.h both typedef `Window`. */
void ext_x11_init(void);
int  ext_x11_sync(void);
void ext_x11_drain(void);
void ext_x11_publish_theme(void);

/* ================================ shared main ============================== */
static const char *g_welcome[] = {
    "Welcome to Debian95.",
    "",
    "This desktop, window manager, MS Sans",
    "Serif font, 3D chrome, taskbar and Start",
    "menu are the SAME source that ran on the",
    "from-scratch kernel - now portable to any",
    "Linux (framebuffer, or an SDL window).",
    "",
    "Drag this window by its title bar.",
};

/* the Win95 boot splash: stretch BOOT.BMP over the framebuffer AND animate the
 * scrolling blue progress band along the bottom for the hold (DEBIAN95_SPLASHMS
 * overrides the ~3.5s hold; 0 skips) - the real Win95 palette-animated bar. */
static void boot_splash(void) {
    const char *ms = getenv("DEBIAN95_SPLASHMS");
    int hold = ms ? atoi(ms) : 3500; if (hold <= 0) return;
    u8 *bp; u32 bl; u32 *px; int bw, bh;
    if (FsReadAlloc("BOOT.BMP", &bp, &bl) != 0) return;
    if (bmp_decode(bp, bl, &px, &bw, &bh) == 0) {
        int sw = (int)fb_width(), sh = (int)fb_height();
        for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++)          /* static logo */
            fb_pixel(x, y, px[(y * bh / sh) * bw + (x * bw / sw)]);
        int barx = sw * 20 / 100, barw = sw * 60 / 100, bary = sh * 90 / 100, barh = 10;
        int frames = hold / 40, phase = 0;                                 /* ~25 fps */
        for (int f = 0; f < frames; f++) {
            for (int i = 0; i < barw; i++) {                               /* scrolling gradient band */
                int v = (i + phase) % 48; int t = v < 24 ? v : 48 - v;     /* 0..24..0 triangle */
                u32 col = RGB(0x08, 0x10 + t * 5, 0x60 + t * 6);           /* navy -> bright blue */
                fb_fill_rect(barx + i, bary, 1, barh, col);
            }
            fb_rect(barx - 1, bary - 1, barw + 2, barh + 2, RGB(0x00, 0x00, 0x40));
            fb_present(); plat_present();
            struct timespec ts = { 0, 40 * 1000000L }; nanosleep(&ts, 0);
            phase += 3;
        }
        free(px);
    }
    free(bp);
}

int main(void) {
    /* Writing a passphrase to a helper (cryptsetup) whose read end has closed
     * must not kill the desktop: turn a broken pipe into an EPIPE error return
     * so run_tool reports failure instead of the whole session dying. */
    signal(SIGPIPE, SIG_IGN);
    if (plat_init() != 0) return 1;
    fb_enable_backbuffer();
    /* apply the persisted resolution (Display Properties) before anything draws */
    { char rs[16]; if (sys_get_setting("resolution", rs, sizeof rs) == 0 && rs[0]) {
        int w = 0, h = 0; char *p = rs; while (*p >= '0' && *p <= '9') w = w*10 + (*p++ - '0');
        if (*p == 'x') { p++; while (*p >= '0' && *p <= '9') h = h*10 + (*p++ - '0'); }
        if (w && h && (w != (int)fb_width() || h != (int)fb_height())) sys_set_resolution(w, h); } }
    boot_splash();                               /* Microsoft Windows 95-style boot logo */

    u8 *buf; u32 len;
    if (FsReadAlloc("FONT.LGF", &buf, &len) == 0 && font_load_lgf(buf, len) == 0)
        printf("gui: loaded MS Sans Serif (FONT.LGF)\n");
    else printf("gui: FONT.LGF not found; using the built-in 8x8 font\n");

    /* The wallpaper is real, persisted state. Authentic Win95: a fresh install
     * has NO wallpaper - just the solid teal desktop; the SETUP.BMP cloud is a
     * setup-time image, not the default. So first run (no saved setting) and an
     * explicit "(None)" both fall through to solid teal. */
    u32 *wpx; int ww, wh;
    char setw[40];
    const char *wall = getenv("DEBIAN95_WALL");                 /* test override only */
    if (sys_get_setting("wallpaper", setw, sizeof setw) == 0 && setw[0]) wall = setw[0] == '(' ? 0 : setw;  /* saved choice; "(None)" -> teal */
    /* NO saved choice -> authentic Win95 teal (the real default). The user's own
     * wallpaper choice is expected to persist in SETTINGS.INI. */
    if (wall && *wall && FsReadAlloc(wall, &buf, &len) == 0 && bmp_decode(buf, len, &wpx, &ww, &wh) == 0)
        wm_set_wallpaper_px(wpx, ww, wh);
    else { static u32 wp[4]; for (int i = 0; i < 4; i++) wp[i] = RGB(0x00, 0x80, 0x80); wm_set_wallpaper_px(wp, 2, 2); }
    { char setm[8]; const char *wm = getenv("DEBIAN95_WALLMODE");
      if (sys_get_setting("wallpaper_mode", setm, sizeof setm) == 0 && setm[0]) wm = setm;
      if (wm) wm_set_wallpaper_mode(atoi(wm)); }
    { char sw[8]; if (sys_get_setting("mouse_swap", sw, sizeof sw) == 0) wm_set_swap_buttons(atoi(sw)); }
    { char dc[8]; if (sys_get_setting("dclick", dc, sizeof dc) == 0 && dc[0]) wm_set_dclick_ms(atoi(dc)); }
    /* keyboard: apply the saved repeat + caret-blink rates (Keyboard applet) */
    { char kd[8], kr[8]; int d = 0, r = 0;
      if (sys_get_setting("kbd_delay", kd, sizeof kd) == 0 && kd[0]) d = atoi(kd);
      if (sys_get_setting("kbd_rate",  kr, sizeof kr) == 0 && kr[0]) r = atoi(kr);
      if (d || r) sys_set_key_repeat(d ? d : 250, r ? r : 30); }
    { char cm[8]; if (sys_get_setting("caret_ms", cm, sizeof cm) == 0 && cm[0]) sys_set_caret_ms(atoi(cm)); }
    { char t24[8]; if (sys_get_setting("time_24h", t24, sizeof t24) == 0 && t24[0]) wm_set_time_24h(atoi(t24)); }
    { char sc[8]; if (sys_get_setting("show_clock", sc, sizeof sc) == 0 && sc[0]) wm_set_show_clock(atoi(sc)); }
    { char ta[8]; if (sys_get_setting("tb_address", ta, sizeof ta) == 0 && ta[0]) wm_set_toolbar_address(atoi(ta)); }
    { char tl[8]; if (sys_get_setting("tb_links", tl, sizeof tl) == 0 && tl[0]) wm_set_toolbar_links(atoi(tl)); }
    { char hc[8]; if (sys_get_setting("acc_hc", hc, sizeof hc) == 0 && atoi(hc)) wm_set_high_contrast(1); }
    { char g[8]; if ((sys_get_setting("gadgets", g, sizeof g) == 0 && g[0] == '1') || getenv("DEBIAN95_GADGETS")) wm_set_gadgets(1); }  /* desktop gadget sidebar */

    app_register_builtin();                             /* shared apps (subsystems/win32) */
    theme_init();                                       /* load the persisted desktop theme (default Win95) */
    app_register(23, "Debian95 Terminal", 0, "PIFMGR.DLL", terminal_open);  /* platform app */
    app_register(72, "Clippy", 0, 0, clippy_open);      /* AI assistant (local LLM) */
    register_downloaded_apps();                         /* VLC / Chromium desktop icons + Programs */
    register_installed_apps();                          /* any other apt-installed GUI app -> Programs */
    /* hot-swappable app modules: dlopen every .so in the plugins dir and register
     * it. Default /usr/lib/debian95/apps; $DEBIAN95_PLUGINS overrides (for dev). */
    { const char *pd = getenv("DEBIAN95_PLUGINS"); modules_load(pd && *pd ? pd : "/usr/lib/debian95/apps"); }
    /* Remote Access: if the user turned on remote connections before, bring the
     * VNC host back up at boot (the password is already stored in the data dir). */
    { char ve[8]; if (sys_vnc_available() &&
          sys_get_setting("vnc_enabled", ve, sizeof ve) == 0 && ve[0] == '1' &&
          sys_vnc_has_password()) sys_vnc_start(); }
    /* test (QEMU): DEBIAN95_VNC=<password> sets a password, starts the host, and
     * reports whether x11vnc is listening on :0 to the serial log. */
    { const char *vv = getenv("DEBIAN95_VNC");
      if (vv && *vv) { sys_vnc_set_password(vv); int r = sys_vnc_start();
          char ip[96]; int port = 0; sys_vnc_info(ip, sizeof ip, &port);
          fprintf(stderr, "DEBIAN95_VNC: start=%d running=%d addr=%s:%d\n", r, sys_vnc_running(), ip, port); } }
    /* test (QEMU): DEBIAN95_SNAPTEST=1 creates a System Restore point and reports the
     * btrfs snapshots to the serial log, proving the whole-system snapshot path. */
    { if (getenv("DEBIAN95_SNAPTEST")) { extern int sys_restore_create(const char *);
          int r = sys_restore_create("verify");
          fprintf(stderr, "DEBIAN95_SNAPTEST: create=%d\n", r);
          int rc = system("echo '--- btrfs subvols ---' >/dev/ttyS0 2>&1; btrfs subvolume list / >/dev/ttyS0 2>&1; ls -la /.snapshots >/dev/ttyS0 2>&1"); (void)rc; } }
    /* test (QEMU): DEBIAN95_RESCANTEST=1 drops a new .desktop then re-scans, proving
     * a freshly-installed app registers live (no restart). */
    { if (getenv("DEBIAN95_RESCANTEST")) {
          int rc = system("cat >/usr/share/applications/zz-rescan-test.desktop <<'D'\n"
                           "[Desktop Entry]\nType=Application\nName=Rescan Test App\nExec=/usr/bin/zz-rescan-probe\nD\n"); (void)rc;
          int added = register_installed_apps();
          fprintf(stderr, "DEBIAN95_RESCANTEST: newly_added=%d total_dyn=%d\n", added, g_ndyn); } }
    { const char *da = getenv("DEBIAN95_DUMPAPPS"); if (da && *da) {   /* test: list registered programs */
        int c = wm_ext_prog_count();
        for (int i = 0; i < c; i++) fprintf(stderr, "PROG %d: %s (act %d)\n", i, wm_ext_prog_name(i), wm_ext_prog_act(i)); } }
    { const char *ld = getenv("DEBIAN95_LAUNCHDESK");   /* test: launch a discovered app via dispatch */
      if (ld && *ld) for (int i = 0; i < g_ndyn; i++) if (strstr(g_dynexec[i], ld)) { dispatch_action(DYN_APP_BASE + i); break; } }
    if (!getenv("DEBIAN95_NOLOGON")) wm_logon_begin();   /* the Welcome-to-Windows logon */
    /* first-run setup wizard: computer name + look, shown once (persisted setup_done) */
    { char sd[8]; if (!getenv("DEBIAN95_NOSETUP") &&
          !(sys_get_setting("setup_done", sd, sizeof sd) == 0 && sd[0] == '1')) {
          wm_setup_begin(); welcome_open(); } }   /* OOBE: hide the desktop behind the wizard */
    /* Boot to a clean, empty desktop like real Win95 - nothing auto-opens. The
     * Welcome window + shell terminal are launchable from the Start menu instead.
     * (DEBIAN95_WELCOME=1 restores the old intro window for demos.) */
    if (getenv("DEBIAN95_WELCOME"))
        wm_add_text(150, 90, 420, 210, "Welcome - Debian95", g_welcome,
                    (int)(sizeof g_welcome / sizeof g_welcome[0]));
    /* auto-open apps at startup: DEBIAN95_OPEN="10,12" (Start-menu action ids) */
    const char *op = getenv("DEBIAN95_OPEN");
    if (op) { const char *p = op; while (*p) { int a = 0; while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
        if (a) dispatch_action(a); while (*p == ',' || *p == ' ') p++; } }
    /* doc-open test: DEBIAN95_OPENDOC="4:README.TXT" opens app id on a document */
    { const char *od = getenv("DEBIAN95_OPENDOC");
      if (od && *od) { int id = 0; const char *p = od; while (*p >= '0' && *p <= '9') id = id*10 + (*p++ - '0');
          if (*p == ':') app_launch_doc(id, p + 1); } }
    /* open a specific Control Panel applet by id (Regional=3, Keyboard=5, Passwords=10) */
    { const char *ap = getenv("DEBIAN95_APPLET"); if (ap && *ap) {
        const char *tb = getenv("DEBIAN95_APPLET_TAB");
        if (tb && *tb) { extern void applet_open_tab(int, int); applet_open_tab(atoi(ap), atoi(tb)); }
        else applet_open(atoi(ap)); } }
    /* open the Media Player on a specific file (testing): DEBIAN95_MEDIA="TESTVID.MPG" */
    { const char *mf = getenv("DEBIAN95_MEDIA"); if (mf && *mf) mediaplayer_open(mf); }
    /* open the Image Viewer on a specific file (testing): DEBIAN95_IMAGE="BAMBOO.BMP" */
    { const char *im = getenv("DEBIAN95_IMAGE"); if (im && *im) { extern void viewer_open(const char *); viewer_open(im); } }
    /* open Explorer at a real filesystem path (testing): DEBIAN95_EXPLORE="/etc" */
    { const char *ep = getenv("DEBIAN95_EXPLORE"); if (ep && *ep) { extern void files_open_at(const char *); files_open_at(ep); } }
    /* file-association test: open a file in its downloaded app (DEBIAN95_OPENFILE=/path) */
    { const char *ofp = getenv("DEBIAN95_OPENFILE"); if (ofp && *ofp) { extern int sys_open_file(const char *); sys_open_file(ofp); } }
    /* WM test: launch an X command under debian95-wm (DEBIAN95_LAUNCH="/usr/local/bin/wmtypes-test") */
    { const char *lc = getenv("DEBIAN95_LAUNCH"); if (lc && *lc) sys_launch_app(lc); }
    /* load + render a REAL Win32 dialog resource from a PE file (fun/testing):
     * DEBIAN95_RCFILE="/mnt/c/Windows/System32/timedate.cpl"  DEBIAN95_RCINDEX=0 */
    { const char *rf = getenv("DEBIAN95_RCFILE"); if (rf && *rf) { extern void rc_open_file(const char *, int);
        const char *ix = getenv("DEBIAN95_RCINDEX"); rc_open_file(rf, ix ? atoi(ix) : 0); } }
    /* browse EVERY dialog across a folder of real Windows binaries:
     * DEBIAN95_RCDIR="/mnt/c/.../win98/files" */
    { const char *rd = getenv("DEBIAN95_RCDIR"); if (rd && *rd) { extern void resbrowse_open(const char *); resbrowse_open(rd); } }
    /* exercise the print spooler (testing): DEBIAN95_TESTPRINT=1 prints a doc then opens the queue */
    { if (getenv("DEBIAN95_TESTPRINT")) {
        extern void print_submit_text(const char *, const char *, unsigned);
        extern void printman_open(void);
        const char *doc = "REPORT.TXT\nDebian95 print spooler test.\nThis really wrote C:\\Printed\\REPORT.TXT.\n";
        print_submit_text("REPORT.TXT", doc, (unsigned)strlen(doc));
        printman_open();
    } }
    { const char *vp = getenv("DEBIAN95_VOLPOP"); if (vp && *vp) wm_volume_popup(); }   /* test: show tray volume popup */
    /* pop the common file dialog for a screenshot (testing): DEBIAN95_DLG=open|save */
    { const char *dl = getenv("DEBIAN95_DLG"); if (dl && *dl) {
        static OPENFILENAME o; o.title = dl[0]=='s' ? "Save As" : "Open"; o.filter = "*.BMP"; o.onok = 0;
        if (dl[0]=='f') { extern void finddlg_open(void); finddlg_open(); }
        else if (dl[0]=='s') GetSaveFileName(&o); else GetOpenFileName(&o); } }
    wm_compose(); fb_present(); wm_cursor_overlay(); plat_present();
    printf("gui: Debian95 desktop up\n");

    ext_x11_init();                              /* foreign-app taskbar bridge (SDL/X11 build) */
    ext_x11_publish_theme();                      /* tell debian95-wm the current theme's colours */
    int shotframes = getenv("DEBIAN95_SHOT") ? 0 : -1;
    int prev_logon = wm_logon_active();          /* 1 while the logon modal is up */
    u32 last_clock = wm_now_ms(), last_input = wm_now_ms();
    /* screen saver config (Display Properties): enabled unless "(None)", wait mins */
    int ss_wait_ms = 60000;
    { char v[24]; if (sys_get_setting("ss_wait", v, sizeof v) == 0 && v[0]) { int m = atoi(v); if (m > 0) ss_wait_ms = m * 60000; } }
    static int sd_fired = 0; static u32 loop_t0 = 0;
    for (;;) {
        if (!loop_t0) loop_t0 = wm_now_ms();
        /* test: DEBIAN95_SHUTDOWN=1/2 auto-triggers the sequence 3s after the desktop
         * is up (and suppresses the real poweroff), for headless screenshotting. */
        { const char *se = getenv("DEBIAN95_SHUTDOWN"); static int armed = 0;
          if (se && !armed && wm_now_ms() - loop_t0 > 3000) { armed = 1; wm_shutdown_begin(atoi(se) == 2); } }
        /* test: DEBIAN95_TOOLTIP="x,y" parks the cursor there so the hover tip shows */
        { const char *hv = getenv("DEBIAN95_TOOLTIP"); static int hset = 0;
          if (hv && !hset && wm_now_ms() - loop_t0 > 2000) { hset = 1;
              int hx = atoi(hv); const char *c = hv; while (*c && *c != ',') c++;
              wm_mouse_abs(hx, *c ? atoi(c + 1) : 0, 0); } }
        /* test: DEBIAN95_SS="<name>" force-starts that screen saver */
        { const char *ss = getenv("DEBIAN95_SS"); static int ssset = 0;
          if (ss && !ssset && wm_now_ms() - loop_t0 > 2000) { ssset = 1; wm_set_screensaver_by_name(ss); wm_screensaver_start(); } }
        /* Shutdown sequence: full-screen "shutting down" -> "safe to turn off",
         * then the real poweroff/reboot (once). Overrides the normal desktop. */
        if (wm_shutdown_active()) {
            wm_shutdown_draw(); fb_present(); plat_present();
            if (!sd_fired && wm_shutdown_phase() == 2) { sd_fired = 1;
                if (!getenv("DEBIAN95_SHUTDOWN")) sys_shutdown(wm_shutdown_restart()); }
            plat_wait();
            continue;
        }
        plat_wait();
        int dirty = plat_input();                /* 0 = none, 1 = full, 2 = cursor-only */
        /* Welcome "Did you know..." tips: once per boot, after the desktop is
         * interactive, unless the user cleared "Show tips at startup". Force a
         * repaint so the dialog is composed even without user input. */
        { static int tips_done = 0; char tv[8];
          if (!tips_done && wm_now_ms() - loop_t0 > 1200 && !wm_setup_active() && !wm_logon_active()
              && !getenv("DEBIAN95_NOTIPS")
              && !(sys_get_setting("show_tips", tv, sizeof tv) == 0 && tv[0] == '0')) {
              tips_done = 1; welcome_tips_open(); dirty = 1; } }
        /* test: DEBIAN95_STARTMENU=1 opens Start > Programs for a screenshot */
        { void wm_debug_open_programs(void); static int smset = 0;
          if (getenv("DEBIAN95_STARTMENU") && !smset && wm_now_ms() - loop_t0 > 2500) { smset = 1; wm_debug_open_programs(); dirty = 1; } }
        /* test: DEBIAN95_SHELLDEMO=sysmenu|anim|marquee exercises the new shell
         * features. Re-asserted every frame (after the desktop settles) so the
         * state survives to the screenshot regardless of stray input events. */
        { const char *sm = getenv("DEBIAN95_SHELLDEMO");
          if (sm && *sm && wm_now_ms() - loop_t0 > 3000) {
              if (!strcmp(sm, "sysmenu")) wm_sysmenu();
              else if (!strcmp(sm, "anim")) { static int once = 0; if (!once) { once = 1; wm_anim_hold(1); wm_test_min_front(); } }
              else if (!strcmp(sm, "marquee")) wm_test_marquee();
              dirty = 1; } }
        /* test: DEBIAN95_MENU=<idx> opens that menu-bar dropdown; DEBIAN95_BUSY=1 shows the hourglass */
        { const char *mn = getenv("DEBIAN95_MENU");
          if (mn && *mn && wm_now_ms() - loop_t0 > 2800) { wm_test_open_menu(atoi(mn)); dirty = 1; } }
        { if (getenv("DEBIAN95_BUSY") && wm_now_ms() - loop_t0 > 2800) { wm_set_busy(1); wm_mouse_abs(700, 250, 0); dirty = 1; } }
        /* test: DEBIAN95_LOCKTEST=1 locks the session after a moment (renders the Computer Locked dialog) */
        { static int lk = 0; if (getenv("DEBIAN95_LOCKTEST") && !lk && wm_now_ms() - loop_t0 > 2800) { lk = 1; wm_logon_lock(); dirty = 1; } }
        /* test: DEBIAN95_SWITCHUSER=1 opens the Log Off account picker */
        { static int su = 0; if (getenv("DEBIAN95_SWITCHUSER") && !su && wm_now_ms() - loop_t0 > 2800) { su = 1; wm_logon_switch_user(); dirty = 1; } }
        /* test: DEBIAN95_OPENWITH=1 pops the Open With dialog for a sample file */
        { static int ow = 0; if (getenv("DEBIAN95_OPENWITH") && !ow && wm_now_ms() - loop_t0 > 2800) { ow = 1;
            extern void fm_openwith_for(const char *, const char *); fm_openwith_for("README.TXT", "README.TXT"); dirty = 1; } }
        /* test: DEBIAN95_CLIPPY="question" opens Clippy and asks it (end-to-end AI test) */
        { static int cq = 0; const char *q = getenv("DEBIAN95_CLIPPY");
          if (q && *q && !cq && wm_now_ms() - loop_t0 > 2800) { cq = 1; extern void clippy_ask_test(const char *); clippy_ask_test(q); dirty = 1; } }
        /* test: DEBIAN95_DISKDIAG=1 dumps the block-device state to the serial */
        { static int dd = 0; if (getenv("DEBIAN95_DISKDIAG") && !dd && wm_now_ms() - loop_t0 > 8000) { dd = 1;
            system("{ echo DISKDIAG-START; lsblk -rno NAME,RM,TYPE,SIZE,MOUNTPOINT; echo udisksctl=$(command -v udisksctl || echo none); echo media:; ls -la /media 2>&1; echo DISKDIAG-END; } >/dev/ttyS0 2>&1"); } }
        /* test: DEBIAN95_OPENLATE="6:18" opens app id after N seconds (for USB automount) */
        { const char *ol = getenv("DEBIAN95_OPENLATE"); static int oldone = 0;
          if (ol && *ol && !oldone) { int secs = atoi(ol); const char *cc = ol; while (*cc && *cc != ':') cc++;
              if (*cc == ':' && wm_now_ms() - loop_t0 > (u32)secs * 1000) { oldone = 1; dispatch_action(atoi(cc + 1)); dirty = 1; } } }
        /* test: DEBIAN95_INSTALL="/dev/X" runs the installer onto that disk, headless */
        { const char *it = getenv("DEBIAN95_INSTALL"); static int idone = 0;
          if (it && *it && !idone && wm_now_ms() - loop_t0 > 2000) { idone = 1; char c[160];
              snprintf(c, sizeof c, "DEBIAN95_INSTALL_YES=1 debian95-install %s >/dev/ttyS0 2>&1; echo DEBIAN95-INSTALL-EXIT=$? >/dev/ttyS0", it);
              system(c); } }
        u32 nowms0 = wm_now_ms();
        if (dirty) last_input = nowms0;          /* any input resets the idle timer */
        /* screen saver: activate after idle; any input dismisses it */
        if (wm_screensaver_active()) {
            if (dirty) { wm_screensaver_stop();                /* wake -> full recompose */
                char pw[8];                                    /* password-protected? require login to return */
                if (sys_get_setting("ss_pw", pw, sizeof pw) == 0 && pw[0] == '1' && sys_has_password("administrator"))
                    wm_logon_lock();                           /* lock on idle: re-auth to return */
                dirty = 1; }
            else { wm_screensaver_tick(); fb_present(); plat_present(); continue; }
        } else if (!wm_logon_active()) {
            char sv[24]; int enabled = 0;                /* off until a saver is chosen */
            if (sys_get_setting("screensaver", sv, sizeof sv) == 0 && sv[0] && sv[0] != '(') enabled = 1;
            if (enabled && nowms0 - last_input > (u32)ss_wait_ms) { wm_set_screensaver_by_name(sv); wm_screensaver_start(); continue; }
        }
        if (terminal_tick()) dirty = 1;          /* drain the shell's output */
        if (ext_x11_sync()) dirty = 1;           /* mirror debian95-wm's framed apps onto the taskbar */
        ext_x11_drain();                         /* send queued taskbar clicks to debian95-wm */
        if (sysmon_tick()) dirty = 1;            /* System Monitor: live CPU/memory */
        if (clippy_tick()) dirty = 1;            /* Clippy: async LLM reply arrived */
        if (mediaplayer_tick()) dirty = 1;       /* Media Player: advance video playback */
        if (scandisk_tick()) dirty = 1;          /* ScanDisk: surface-scan animation */
        if (defrag_tick()) dirty = 1;            /* Defragmenter: cluster consolidation */
        if (wm_hover_tick()) dirty = 1;          /* taskbar hover tooltips */
        if (wm_anim_tick()) dirty = 1;           /* minimize/maximize zoom animation */
        if (wm_busy_tick()) dirty = 1;           /* hourglass auto-clear -> restore the arrow */
        if (wm_take_profile_switched()) { plat_apply_profile(); dirty = 1; }   /* Log Off -> reload the new user's profile */
        { static u32 last_g; u32 n = wm_now_ms(); if (n - last_g > 1000) { last_g = n; if (wm_gadgets_tick()) dirty = 1; } }  /* desktop gadgets ~1Hz */
        { static u32 last_rs; u32 n = wm_now_ms(); if (n - last_rs > 15000) { last_rs = n;    /* live app re-scan ~15s: apps */
            if (register_installed_apps() > 0) dirty = 1; } }                                 /* installed via Add/Remove appear w/o restart */
        { static u32 last_am; u32 n = wm_now_ms(); if (n - last_am > 3000) { last_am = n; sys_disk_automount();  /* auto-mount inserted USB media */
            struct lg_disk { char mount[128]; char label[32]; char size[16]; int removable; } d[8];              /* + refresh the Safely Remove Hardware tray icon */
            int sys_disk_list(struct lg_disk *out, int max); int nd = sys_disk_list(d, 8), rem = 0;
            for (int i = 0; i < nd; i++) if (d[i].removable) rem = 1;
            wm_set_removable(rem || getenv("DEBIAN95_FAKEREMOVABLE") != 0); } }   /* test hook forces the tray icon */
        { u32 nowms = wm_now_ms(); if (nowms - last_clock >= 10000) { last_clock = nowms; dirty = 1; } }  /* live tray clock */
        { static u32 last_caret; u32 nowms = wm_now_ms();
          if (wm_wants_caret() && nowms - last_caret >= (u32)g_caret_ms) { last_caret = nowms; if (!dirty) dirty = 1; } }  /* blink text carets */
        if (shotframes >= 0 && shotframes++ < 130) dirty = 1;   /* force frames until the shot is taken */
        /* test: DEBIAN95_CURSORTEST=1 walks the cursor via the damage-limited path
         * (dirty==2) so the readback proves the texture retains the composed
         * desktop outside the small cursor rect. */
        { const char *ct = getenv("DEBIAN95_CURSORTEST"); static int cx = 120;
          if (ct && *ct && shotframes > 6) { cx += 8; if (cx > 1000) cx = 120; wm_mouse_abs(cx, 360, 0); dirty = 2; } }
        int now_logon = wm_logon_active();
        if (prev_logon && !now_logon) {          /* just logged in: session + chime */
            sys_session_login("administrator");
            snd_event("start");                  /* the assigned start-up sound */
            dirty = 1;
        }
        prev_logon = now_logon;
        if (wm_boot_tick()) dirty = 1;           /* reveal desktop icons one-by-one */
        int act = wm_take_action();
        if (act) { dispatch_action(act); dirty = 1; }
        if (dirty == 2) { wm_cursor_move(); plat_present(); }             /* damage-limited cursor */
        else if (dirty) { wm_compose(); fb_present(); wm_cursor_overlay(); plat_present(); }
    }
    return 0;
}
