/* Debian95 — System Information. An authentic Win95-style "winmsd" accessory that
 * reports the REAL machine: OS/kernel (uname), processor + core count
 * (/proc/cpuinfo), physical memory (/proc/meminfo), display resolution, and
 * uptime (/proc/uptime). Ships as a hot-swappable app module. */
#include <debian95/appsdk.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/utsname.h>

static HWND g_win;

#define MAXL 22
static char g_line[MAXL][96];
static int  g_hdr[MAXL];      /* 1 = section header (navy) */
static int  g_nline;

static void add(int hdr, const char *fmt, ...) {
    if (g_nline >= MAXL) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_line[g_nline], sizeof g_line[0], fmt, ap);
    va_end(ap);
    g_hdr[g_nline] = hdr; g_nline++;
}

static void gather(void) {
    if (g_nline) return;                      /* gather once per open */
    struct utsname u; memset(&u, 0, sizeof u); uname(&u);
    char host[64] = ""; if (gethostname(host, sizeof host) != 0) host[0] = 0;

    char cpu[96] = "(unknown)"; int cores = 0; char ln[256];
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) { while (fgets(ln, sizeof ln, f)) {
            if (!strncmp(ln, "processor", 9)) cores++;
            else if (!strncmp(ln, "model name", 10)) { char *c = strchr(ln, ':');
                if (c) { c += 2; c[strcspn(c, "\n")] = 0; snprintf(cpu, sizeof cpu, "%s", c); } }
        } fclose(f); }

    long memtot = 0, memav = 0;
    f = fopen("/proc/meminfo", "r");
    if (f) { while (fgets(ln, sizeof ln, f)) {
            if (!strncmp(ln, "MemTotal:", 9))      sscanf(ln, "MemTotal: %ld", &memtot);
            else if (!strncmp(ln, "MemAvailable:", 13)) sscanf(ln, "MemAvailable: %ld", &memav);
        } fclose(f); }

    double up = 0; f = fopen("/proc/uptime", "r");
    if (f) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }
    int uh = (int)(up / 3600), um = ((int)up % 3600) / 60;

    add(1, "Operating System");
    add(0, "    Debian95 Desktop Edition 1.0");
    add(0, "    Kernel: %s %s", u.sysname, u.release);
    add(0, "    Computer name: %s", host[0] ? host : "debian95");
    add(0, "    Architecture: %s", u.machine);
    add(1, "Processor");
    add(0, "    %s", cpu);
    add(0, "    Logical processors: %d", cores);
    add(1, "Physical Memory");
    add(0, "    Total: %ld MB", memtot / 1024);
    add(0, "    Available: %ld MB", memav / 1024);
    add(1, "Display");
    add(0, "    Resolution: %u x %u", fb_width(), fb_height());
    add(0, "    Color depth: 32-bit");
    add(1, "System");
    add(0, "    Uptime: %d hours %d minutes", uh, um);
}

static void paint(int ox, int oy, int cw, int ch) {
    fb_fill_rect((u32)ox, (u32)oy, (u32)cw, (u32)ch, RGB(192, 192, 192));
    int px = ox + 8, py = oy + 8, pw = cw - 16, ph = ch - 16;
    wm_bevel_sunken(px, py, pw, ph);
    fb_fill_rect((u32)px + 2, (u32)py + 2, (u32)pw - 4, (u32)ph - 4, RGB(255, 255, 255));
    int lh = font_height() + 5, y = py + 8;
    for (int i = 0; i < g_nline; i++) {
        fb_text_t((u32)(px + 10), (u32)y, g_line[i], g_hdr[i] ? RGB(0, 0, 128) : RGB(0, 0, 0));
        y += lh;
    }
}

#define IDM_EXIT  1
#define IDM_ABOUT 2
static const MenuItemW32 MI_file[] = { { "Exit", IDM_EXIT } };
static const MenuItemW32 MI_help[] = { { "About System Information", IDM_ABOUT } };
static const MenuW32 MENUS[] = { { "File", MI_file, 1 }, { "Help", MI_help, 1 } };

static LRESULT CALLBACK SysInfo_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetMenuW32(hwnd, MENUS, 2);
            SetWindowIcon(hwnd, IC_APP);
            gather();
            g_win = hwnd;
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_EXIT:  DestroyWindow(hwnd); break;
                case IDM_ABOUT: MessageBoxW32(hwnd,
                    "System Information\nDebian95 Desktop Edition", "About", 0); break;
            }
            return 0;
        case WM_DESTROY: g_win = 0; g_nline = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void sysinfo_open(void) {
    if (g_win) return;                                    /* single instance */
    CreateAppWindow(210, 120, 440, 380, "System Information", SysInfo_WndProc, 0);
}

#ifdef DEBIAN95_MODULE
const Debian95Module debian95_module = { DEBIAN95_MODULE_ABI, 60, "System Information", IC_APP, 0, sysinfo_open, -1 };
#endif
