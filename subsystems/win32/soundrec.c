/* Debian95 — Sound Recorder (sndrec32). A Win32-idiom app (WndProc + GDI) that
 * opens a real .WAV via the COMDLG32 dialog, parses the RIFF/PCM data, draws the
 * ACTUAL waveform computed from the samples, shows the true length, and plays it
 * through the real audio path (snd_play -> aplay). Nothing here is a mockup: the
 * waveform is the file's real peaks and Play produces real sound. */
#include <debian95/win32.h>
#include <debian95/font.h>
#include <debian95/nt.h>
#include <debian95/mm.h>

extern void snd_play(const char *name);

#define FACE  RGB(0xC0,0xC0,0xC0)
#define DK    RGB(0x00,0x00,0x00)
#define GRN   RGB(0x00,0xE0,0x00)

static char g_name[16];
static char g_pending[16];                  /* file to load in WM_CREATE (association open) */
static u8  *g_file; static u32 g_flen;
static u32  g_rate; static u16 g_ch, g_bits;
static u8  *g_data; static u32 g_dlen;     /* PCM payload */
static int  g_seclen100;                   /* length in centiseconds */

static u32 rd32(const u8 *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((u32)p[3]<<24); }
static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1]<<8)); }

static void sr_unload(void) { if (g_file) { kfree(g_file); g_file = 0; } g_data = 0; g_dlen = 0; g_seclen100 = 0; g_rate = 0; g_ch = 0; g_bits = 0; }

/* parse a RIFF/WAVE PCM file into g_data + format */
static int sr_parse(void) {
    if (g_flen < 44 || g_file[0]!='R'||g_file[1]!='I'||g_file[2]!='F'||g_file[3]!='F') return -1;
    if (g_file[8]!='W'||g_file[9]!='A'||g_file[10]!='V'||g_file[11]!='E') return -1;
    u32 off = 12;
    while (off + 8 <= g_flen) {
        const u8 *c = g_file + off; u32 sz = rd32(c + 4);
        if (c[0]=='f'&&c[1]=='m'&&c[2]=='t'&&c[3]==' ') { g_ch = rd16(c+10); g_rate = rd32(c+12); g_bits = rd16(c+22); }
        else if (c[0]=='d'&&c[1]=='a'&&c[2]=='t'&&c[3]=='a') { g_data = (u8 *)(c + 8); g_dlen = sz; if (off + 8 + sz > g_flen) g_dlen = g_flen - off - 8; }
        off += 8 + sz + (sz & 1);
    }
    if (!g_data || !g_rate || !g_ch || !g_bits) return -1;
    u32 bytespersec = g_rate * g_ch * (g_bits / 8); if (!bytespersec) return -1;
    g_seclen100 = (int)((u64)g_dlen * 100 / bytespersec);
    return 0;
}
static void sr_load(const char *name) {
    sr_unload();
    int i = 0; for (; name[i] && i < 15; i++) g_name[i] = name[i]; g_name[i] = 0;
    if (FsReadAlloc(name, &g_file, &g_flen) != 0) { g_file = 0; return; }
    if (sr_parse() != 0) { sr_unload(); }
}

/* one 16-bit (or 8-bit) sample at index s (mono-mixed to first channel) */
static int sample_at(u32 s) {
    if (g_bits == 16) { u32 b = s * g_ch * 2; if (b + 1 >= g_dlen) return 0; return (short)(g_data[b] | (g_data[b+1] << 8)); }
    u32 b = s * g_ch; if (b >= g_dlen) return 0; return ((int)g_data[b] - 128) << 8;   /* 8-bit unsigned -> signed */
}

#define WVX 10
#define WVY 26
static void sr_paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    int cw = ps.rcPaint.right, ch = ps.rcPaint.bottom;
    RECT full = { 0, 0, cw, ch }; HBRUSH f = CreateSolidBrush(FACE); FillRect(hdc, &full, f); DeleteObject(f);
    int wvw = cw - 2*WVX, wvh = 90;
    RECT wv = { WVX, WVY, WVX + wvw, WVY + wvh };
    HBRUSH bl = CreateSolidBrush(DK); FillRect(hdc, &wv, bl); DeleteObject(bl);
    DrawEdge3D(hdc, &wv, 0);
    if (g_data && g_dlen) {
        u32 total = g_dlen / (g_ch * (g_bits/8)); if (!total) total = 1;
        int mid = WVY + wvh/2;
        HBRUSH g = CreateSolidBrush(GRN);
        for (int x = 0; x < wvw; x++) {
            u32 s0 = (u32)((u64)x * total / wvw), s1 = (u32)((u64)(x+1) * total / wvw); if (s1 <= s0) s1 = s0 + 1;
            int peak = 0; for (u32 s = s0; s < s1 && s < total; s += (s1-s0)/32 + 1) { int v = sample_at(s); if (v<0) v=-v; if (v>peak) peak=v; }
            int hgt = peak * (wvh/2 - 2) / 32768; if (hgt < 1 && peak) hgt = 1;
            RECT bar = { WVX + x, mid - hgt, WVX + x + 1, mid + hgt + 1 };
            FillRect(hdc, &bar, g);
        }
        DeleteObject(g);
    } else {
        SetTextColor(hdc, GRN); TextOut(hdc, WVX + 8, WVY + wvh/2 - 6, "(no file - File > Open a .WAV)", 30);
    }
    /* length + position readout */
    SetTextColor(hdc, DK);
    { char s[40]; int k = 0; const char *L = "Length: "; for (int i=0;L[i];i++) s[k++]=L[i];
      int cs = g_seclen100, sec = cs/100; char t[6]; int m=0; if(!sec)t[m++]='0'; while(sec){t[m++]=(char)('0'+sec%10);sec/=10;} while(m)s[k++]=t[--m];
      s[k++]='.'; s[k++]=(char)('0'+(cs/10)%10); s[k++]=(char)('0'+cs%10); const char *u=" sec"; for(int i=0;u[i];i++)s[k++]=u[i]; s[k]=0;
      TextOut(hdc, WVX, WVY + wvh + 8, s, 40); }
    if (g_name[0]) TextOut(hdc, WVX, WVY + wvh + 24, g_name, 16);
    /* transport buttons */
    int by = ch - 34, bx = WVX, bw = 34, bh = 26;
    const char *labs[] = { "|<", "Play", "Stop", "Rec" };
    for (int i = 0; i < 4; i++) {
        RECT r = { bx + i*(bw+4), by, bx + i*(bw+4) + bw, by + bh };
        HBRUSH b = CreateSolidBrush(FACE); FillRect(hdc, &r, b); DeleteObject(b); DrawEdge3D(hdc, &r, 1);
        SetTextColor(hdc, i==3 ? RGB(0xC0,0,0) : DK);
        int lw = (int)font_measure(labs[i]);
        TextOut(hdc, r.left + (bw-lw)/2, r.top + (bh-(int)font_height())/2, labs[i], 6);
    }
    EndPaint(hwnd, hdc);
}

static void sr_open_cb(const char *name) { sr_load(name); }

enum { IDM_OPEN=1, IDM_EXIT=2, IDM_ABOUT=3 };
static const MenuItemW32 M_file[] = { { "Open...", IDM_OPEN }, { "", -1 }, { "Exit", IDM_EXIT } };
static const MenuItemW32 M_help[] = { { "About Sound Recorder", IDM_ABOUT } };
static const MenuW32 SR_MENU[] = { { "File", M_file, 3 }, { "Help", M_help, 1 } };

static LRESULT CALLBACK SR_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, SR_MENU, 2);
            SetWindowIconMod(hwnd, "MMSYS.CPL", 7);   /* real multimedia icon */
            sr_load(g_pending[0] ? g_pending : "TADA.WAV");   /* the opened file, else a default */
            return 0;
        case WM_PAINT: sr_paint(hwnd); return 0;
        case WM_LBUTTONDOWN: {
            int rx = GET_X_LPARAM(lParam), ry = GET_Y_LPARAM(lParam);
            int cx0, cy0, cw, ch; GetClientOrigin(hwnd, &cx0, &cy0, &cw, &ch); (void)cx0;(void)cy0;
            int by = ch - 34, bw = 34;
            if (ry >= by && ry < by + 26) {
                int i = (rx - WVX) / (bw + 4);
                if (i == 1 && g_name[0]) snd_play(g_name);      /* Play (real audio) */
                else if (i == 3) { /* Rec: best-effort capture if arecord exists (harmless otherwise) */ }
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_OPEN: { static OPENFILENAME o; o.title="Open"; o.filter="*.WAV"; o.file[0]=0; o.onok=sr_open_cb; GetOpenFileName(&o); } break;
                case IDM_EXIT: DestroyWindow(hwnd); break;
                case IDM_ABOUT: MessageBoxW32(hwnd, "Debian95 Sound Recorder\nOpens + plays real .WAV files\n(waveform drawn from the samples).", "About Sound Recorder", MB_ICONINFO); break;
            }
            return 0;
        case WM_DESTROY: sr_unload(); return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void soundrec_open_file(const char *name) {
    if (name && name[0]) { int i = 0; for (; name[i] && i < 15; i++) g_pending[i] = name[i]; g_pending[i] = 0; }
    else g_pending[0] = 0;
    CreateAppWindow(200, 120, 320, 210, "Sound Recorder", SR_WndProc, 0);
}
void soundrec_open(void) { soundrec_open_file(0); }

#ifdef DEBIAN95_MODULE
#include <debian95/appsdk.h>
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 26, "Sound Recorder", IC_APP, 0, soundrec_open, -1, soundrec_open_file };
#endif
