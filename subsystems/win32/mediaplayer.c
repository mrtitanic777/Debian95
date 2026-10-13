/* Debian95 — Media Player: a Windows-95-style media player. Plays animated GIF
 * and MPEG-1 video in a window with a real transport bar: a seek trackbar with
 * time tick marks, Play/Pause/Stop, skip-to-start / step-back / step-forward /
 * skip-to-end, a numeric time readout, a File/Edit/Device/Scale/Help menu bar
 * and a Scale ▸ Double Size (2x) mode. Frames advance on the real-time clock via
 * mediaplayer_tick(). Everything is one in-process window (no separate .exe). */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/nt.h>
#include <debian95/img.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include <debian95/win32.h>

#define SLIDERH 26          /* seek trackbar + tick labels */
#define BTNROW  30          /* transport buttons row */
#define CTRLH   (SLIDERH + BTNROW)
#define MP_BG   RGB(0x00, 0x00, 0x00)
#define FACE    RGB(0xC0, 0xC0, 0xC0)
#define DK      RGB(0x10, 0x10, 0x10)

extern unsigned sys_uptime_ms(void);
extern int  mpeg_open(u8 *data, u32 len, int *w, int *h);
extern int  mpeg_next(u32 *rgb);
extern void mpeg_rewind(void);
extern void mpeg_close(void);
extern int  mpeg_audio_to_wav(u8 *data, u32 len, const char *wavpath);   /* extract the audio track */
extern void snd_play(const char *name);
extern void snd_stop(void);
#define AUD_WAV "_MPAUD.WAV"
static int  g_has_audio;         /* 1 = the MPEG had an audio track (extracted to AUD_WAV) */

static HWND g_win;            /* the single Media Player window (Win32 veneer) */
static u8   *g_data; static u32 g_len;
static u32 **g_frames; static int *g_delays;
static int  g_nf, g_cur, g_w, g_h;
static int  g_playing;               /* 1 = playing, 0 = stopped/paused */
static unsigned g_last_ms;
static char g_name[16];
static int  g_mpeg;                  /* 1 = streaming MPEG-1 source */
static u32 *g_rgb;                   /* single decoded frame (MPEG) */
static int  g_frameno;               /* MPEG frame counter */
static int  g_scale2;                /* Scale ▸ Double Size */
static int  g_fps = 15;              /* effective frames/second for the time readout */

static void mp_free(void) {
    if (g_has_audio) { snd_stop(); g_has_audio = 0; }
    if (g_mpeg) { mpeg_close(); g_mpeg = 0; }
    if (g_rgb) { kfree(g_rgb); g_rgb = 0; }
    if (g_frames) { for (int i = 0; i < g_nf; i++) if (g_frames[i]) kfree(g_frames[i]); kfree(g_frames); g_frames = 0; }
    if (g_delays) { kfree(g_delays); g_delays = 0; }
    if (g_data) { kfree(g_data); g_data = 0; }
    g_nf = 0; g_cur = 0; g_frameno = 0;
}

/* current / total position in centiseconds (1/100 s) for the readout + slider */
static int gif_cs(int upto) { int cs = 0; for (int i = 0; i < upto && i < g_nf; i++) { int d = g_delays ? g_delays[i] : 10; cs += d < 3 ? 3 : d; } return cs; }
static int cur_cs(void)   { return g_mpeg ? g_frameno * 100 / g_fps : gif_cs(g_cur); }
static int total_cs(void) { return g_mpeg ? 0 : gif_cs(g_nf); }   /* 0 = unknown (streaming) */

int mediaplayer_tick(void) {
    if (!g_win || !IsWindowVisible(g_win) || !g_playing) return 0;
    unsigned now = sys_uptime_ms();
    if (g_mpeg) {
        if (now - g_last_ms < 33 || !g_rgb) return 0;      /* ~30fps ceiling */
        g_last_ms = now;
        if (!mpeg_next(g_rgb)) { mpeg_rewind(); if (!mpeg_next(g_rgb)) return 0; g_frameno = 0; }  /* loop */
        g_frameno++;
        return 1;
    }
    if (g_nf <= 1 || !g_frames) return 0;
    int d = (g_delays ? g_delays[g_cur] : 10) * 10; if (d < 30) d = 30;
    if (now - g_last_ms >= (unsigned)d) { g_cur = (g_cur + 1) % g_nf; g_last_ms = now; return 1; }
    return 0;
}

/* ---- transport glyphs (drawn into a raised button) ---- */
static void gl_tri(int x, int y, int h, int dir, u32 c) {   /* dir: 1 right, -1 left */
    for (int i = 0; i < h; i++) { int w = (h - i > i ? i : h - i); if (w < 0) w = 0;
        if (dir > 0) fb_fill_rect(x, y + i, w + 1, 1, c); else fb_fill_rect(x - w, y + i, w + 1, 1, c); }
}
static const int BTN_N = 7;
enum { B_PLAY, B_PAUSE, B_STOP, B_START, B_BACK, B_FWD, B_END };
static void draw_btn(int x, int y, int w, int h, int id) {
    fb_fill_rect(x, y, w, h, FACE); wm_bevel_raised(x, y, w, h);
    int cx = x + w / 2, cy = y + h / 2, s = h - 12;
    switch (id) {
        case B_PLAY:  gl_tri(cx - 3, cy - s/2, s, 1, id == B_PLAY && g_playing ? RGB(0x00,0x80,0x00) : DK); break;
        case B_PAUSE: fb_fill_rect(cx - 4, cy - s/2, 3, s, DK); fb_fill_rect(cx + 2, cy - s/2, 3, s, DK); break;
        case B_STOP:  fb_fill_rect(cx - s/2, cy - s/2, s, s, DK); break;
        case B_START: fb_fill_rect(cx - 6, cy - s/2, 2, s, DK); gl_tri(cx - 3, cy - s/2, s, -1, DK); break;
        case B_BACK:  gl_tri(cx, cy - s/2, s, -1, DK); gl_tri(cx + 5, cy - s/2, s, -1, DK); break;
        case B_FWD:   gl_tri(cx - 5, cy - s/2, s, 1, DK); gl_tri(cx, cy - s/2, s, 1, DK); break;
        case B_END:   gl_tri(cx - 2, cy - s/2, s, 1, DK); fb_fill_rect(cx + 4, cy - s/2, 2, s, DK); break;
    }
}

static void mp_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, MP_BG);
    int vh = ch - CTRLH;
    const u32 *frame = g_mpeg ? g_rgb : (g_frames && g_nf > 0 ? g_frames[g_cur] : 0);
    if (frame && g_w > 0) {
        int sw = g_scale2 ? g_w * 2 : g_w, sh = g_scale2 ? g_h * 2 : g_h;
        int x = cx + (cw - sw) / 2, y = cy + (vh - sh) / 2; if (x < cx) x = cx; if (y < cy) y = cy;
        if (g_scale2) { for (int j = 0; j < sh && y + j < cy + vh; j++) for (int i = 0; i < sw && x + i < cx + cw; i++)
                            fb_pixel(x + i, y + j, frame[(j/2) * g_w + (i/2)]); }
        else fb_blit(x, y, g_w, g_h, frame);
    } else {
        fb_text_t(cx + 12, cy + 12, "No media loaded.", RGB(0xE0, 0xE0, 0xE0));
        fb_text_t(cx + 12, cy + 30, g_name, RGB(0xA0, 0xA0, 0xA0));
    }
    /* ---- seek trackbar with tick marks + time labels ---- */
    int sy = cy + vh;
    fb_fill_rect(cx, sy, cw, CTRLH, FACE); wm_bevel_raised(cx, sy, cw, CTRLH);
    int tx = cx + 8, tw = cw - 16, ty = sy + 8;
    fb_fill_rect(tx, ty, tw, 4, RGB(0x88,0x88,0x88)); wm_bevel_sunken(tx, ty, tw, 4);
    int tot = total_cs(), cur = cur_cs();
    for (int s = 0; s * 100 <= (tot ? tot : 1000); s++) {    /* 1-second ticks + labels */
        int px = tot ? tx + s * 100 * (tw - 6) / tot : tx + s * (tw / 11);
        if (px > tx + tw - 2) break;
        fb_fill_rect(px, ty + 6, 1, 4, RGB(0x40,0x40,0x40));
        char l[8]; int k = 0; int v = s; char t[4]; int m = 0; if (!v) t[m++]='0'; while (v){t[m++]=(char)('0'+v%10);v/=10;} while(m)l[k++]=t[--m];
        l[k++]='.'; l[k++]='0'; l[k++]='0'; l[k]=0;
        if (s % 2 == 0) fb_text_t(px - 8, ty + 11, l, DK);
    }
    int thumbx = tot ? tx + cur * (tw - 6) / tot : tx + (g_frameno % 200) * (tw - 6) / 200;
    fb_fill_rect(thumbx, sy + 4, 6, 12, RGB(0x00,0x00,0x80)); wm_bevel_raised(thumbx, sy + 4, 6, 12);
    /* ---- transport buttons ---- */
    int bx = cx + 6, by = sy + SLIDERH + 2, bw = 26, bh = BTNROW - 6;
    for (int i = 0; i < BTN_N; i++) { draw_btn(bx + i * (bw + 2), by, bw, bh, i);
        if (i == B_STOP) bx += 6; }   /* small gap after Stop */
    /* ---- time readout "X.XX (sec)" ---- */
    { char r[24]; int k = 0; int cs = cur; int sec = cs / 100, frac = cs % 100;
      char t[6]; int m = 0; if (!sec) t[m++]='0'; while (sec){t[m++]=(char)('0'+sec%10);sec/=10;} while(m)r[k++]=t[--m];
      r[k++]='.'; r[k++]=(char)('0'+frac/10); r[k++]=(char)('0'+frac%10);
      const char *u = " (sec)"; for (int i = 0; u[i]; i++) r[k++]=u[i]; r[k]=0;
      int rx = cx + cw - (int)font_measure(r) - 10;
      fb_fill_rect(rx - 6, by, (int)font_measure(r) + 12, bh, RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(rx - 6, by, (int)font_measure(r) + 12, bh);
      fb_text_t(rx, by + (bh - (int)font_height()) / 2, r, DK); }
}

static void mp_audio_start(void) { if (g_has_audio) { snd_stop(); snd_play(AUD_WAV); } }
static void mp_transport(int id) {
    switch (id) {
        case B_PLAY:  g_playing = 1; g_last_ms = sys_uptime_ms(); mp_audio_start(); break;   /* restart audio in sync */
        case B_PAUSE: g_playing = 0; if (g_has_audio) snd_stop(); break;
        case B_STOP:  g_playing = 0; if (g_has_audio) snd_stop(); if (g_mpeg) { mpeg_rewind(); if (g_rgb) mpeg_next(g_rgb); g_frameno = 0; } else g_cur = 0; break;
        case B_START: if (g_mpeg) { mpeg_rewind(); if (g_rgb) mpeg_next(g_rgb); g_frameno = 0; } else g_cur = 0; if (g_playing) mp_audio_start(); break;
        case B_BACK:  if (!g_mpeg && g_nf) { g_cur = g_cur > 0 ? g_cur - 1 : 0; } break;
        case B_FWD:   if (g_mpeg) { if (g_rgb && mpeg_next(g_rgb)) g_frameno++; } else if (g_nf) g_cur = (g_cur + 1) % g_nf; break;
        case B_END:   if (!g_mpeg && g_nf) g_cur = g_nf - 1; break;
    }
}
static void mp_click(int rx, int ry) {
    int cx0, cy0, cw, ch; GetClientOrigin(g_win, &cx0, &cy0, &cw, &ch); (void)cx0; (void)cy0;
    int vh = ch - CTRLH, sy = vh;
    if (ry >= sy && ry < sy + SLIDERH) {                    /* seek on the trackbar */
        int tx = 8, tw = cw - 16, tot = total_cs();
        if (rx >= tx && rx < tx + tw && !g_mpeg && g_nf > 0 && tot > 0) {
            int cs = (rx - tx) * tot / (tw > 1 ? tw : 1);   /* map x -> time -> frame */
            int acc = 0, f = 0; for (; f < g_nf; f++) { acc += (g_delays ? g_delays[f] : 10) < 3 ? 3 : (g_delays ? g_delays[f] : 10); if (acc >= cs) break; }
            g_cur = f < g_nf ? f : g_nf - 1;
        }
        return;
    }
    int by = sy + SLIDERH + 2, bw = 26;                     /* transport buttons */
    if (ry >= by && ry < by + BTNROW - 6) {
        int bx = 6;
        for (int i = 0; i < BTN_N; i++) { int x = bx + i * (bw + 2); if (i > B_STOP) x += 6;
            if (rx >= x && rx < x + bw) { mp_transport(i); return; } }
        return;
    }
    if (ry < vh) g_playing = !g_playing;                    /* click the video toggles play */
}

/* ---- menu bar ---- */
static const MenuItemW32 MM_file[]   = { { "Open...", 1 }, { "Close", 2 }, { "", -1 }, { "Exit", 3 } };
static const MenuItemW32 MM_edit[]   = { { "Copy Object", 0 }, { "Options...", 0 } };
static const MenuItemW32 MM_device[] = { { "Video for Windows", 0 }, { "Properties...", 0 } };
static const MenuItemW32 MM_scale[]  = { { "Time", 0 }, { "Frames", 0 }, { "", -1 }, { "Double Size", 4 }, { "Original Size", 5 } };
static const MenuItemW32 MM_help[]   = { { "About Media Player", 6 } };
static const MenuW32 MP_MENUS[] = {
    { "File", MM_file, 4 }, { "Edit", MM_edit, 2 }, { "Device", MM_device, 2 },
    { "Scale", MM_scale, 5 }, { "Help", MM_help, 1 },
};
static void mp_open_cb(const char *name) { mediaplayer_open(name); }
static void mp_menu(int id) {
    if (id == 1) { static OPENFILENAME o; o.title = "Open"; o.filter = 0; o.file[0] = 0; o.onok = mp_open_cb; GetOpenFileName(&o); }
    else if (id == 2 || id == 3) DestroyWindow(g_win);
    else if (id == 4) g_scale2 = 1;
    else if (id == 5) g_scale2 = 0;
    else if (id == 6) wm_messagebox("About Media Player",
        "Debian95 Media Player\nPlays MPEG-1 and animated GIF video\nwith a Win95-style transport bar.");
}

/* ---- Win32 veneer: WndProc ---- */
static LRESULT CALLBACK Mediaplayer_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, MP_MENUS, 5);
            SetWindowIconMod(hwnd, "MMSYS.CPL", 7);   /* a real multimedia icon */
            g_win = hwnd;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            mp_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_LBUTTONDOWN: mp_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND: mp_menu(LOWORD(wParam)); return 0;
        case WM_DESTROY: mp_free(); g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void mediaplayer_open(const char *name) {
    int i = 0; for (; name[i] && i < 15; i++) g_name[i] = name[i]; g_name[i] = 0;
    mp_free(); g_scale2 = 0; g_fps = 15;
    if (FsReadAlloc(name, &g_data, &g_len) == 0) {
        int is_gif = g_len >= 6 && g_data[0] == 'G' && g_data[1] == 'I' && g_data[2] == 'F';
        int is_mpg = g_len >= 4 && g_data[0] == 0 && g_data[1] == 0 && g_data[2] == 1 &&
                     (g_data[3] == 0xBA || g_data[3] == 0xB3);
        if (is_mpg && mpeg_open(g_data, g_len, &g_w, &g_h) == 0) {
            g_rgb = (u32 *)kmalloc((u32)g_w * g_h * 4); g_fps = 30;
            if (g_rgb) { g_mpeg = 1; if (!mpeg_next(g_rgb)) { for (int j = 0; j < g_w*g_h; j++) g_rgb[j] = 0; } g_frameno = 1; }
            g_has_audio = (mpeg_audio_to_wav(g_data, g_len, AUD_WAV) == 0);   /* extract sound, if any */
        } else if (is_gif) {
            gif_decode_all(g_data, g_len, &g_frames, &g_delays, &g_nf, &g_w, &g_h);
        }
    }
    g_cur = 0; g_playing = 1; g_last_ms = sys_uptime_ms();
    int ww = (g_w ? g_w : 320) + 16, wh = (g_h ? g_h : 200) + CTRLH + 40;
    if (ww < 320) ww = 320; if (ww > 900) ww = 900; if (wh > 700) wh = 700;
    /* static: the WM keeps the title POINTER, not a copy, so it must outlive us */
    static char title[40]; int t = 0; for (; g_name[t] && t < 15; t++) title[t] = g_name[t];
    const char *suf = " - Media Player"; for (int j = 0; suf[j] && t < 39; j++) title[t++] = suf[j]; title[t] = 0;
    if (g_win) { SetWindowSize(g_win, ww, wh); SetWindowText(g_win, title); }
    else CreateAppWindow(150, 90, ww, wh, title, Mediaplayer_WndProc, 0);
    if (g_has_audio) { snd_stop(); snd_play(AUD_WAV); }   /* start sound in sync with playback */
}
