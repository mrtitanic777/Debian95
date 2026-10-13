/* Debian95 — print spooler + Print Manager. Apps submit jobs (text or a bitmap);
 * the spooler writes each job as a REAL file under C:\Printed (the "printer" is a
 * file device, like Win95's "Print to File"), records it in the queue, and the
 * Print Manager window shows the queue exactly like the Win95 printer window.
 * Genuinely functional: after printing, the output file exists on disk and opens
 * in Notepad / the Image Viewer. */
#include <debian95/win32.h>   /* Win32 veneer: WndProc + WM_* + GetClientOrigin */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/nt.h>
#include <debian95/mm.h>

/* real CUPS printing (shims_linux.c) */
int  sys_printer_default(char *out, int n);
int  sys_print_spooled(const char *doc);

#define FACE  RGB(0xC0,0xC0,0xC0)
#define WHITE RGB(0xFF,0xFF,0xFF)
#define DK    RGB(0x00,0x00,0x00)
#define SEL   RGB(0x00,0x00,0x80)
#define ROWH  17

/* ---- the spool queue ---- */
typedef struct { char doc[40]; char status[16]; u32 size; } PrintJob;
static PrintJob g_job[32]; static int g_njob;
static HWND g_win; static int g_sel = -1;

static void s_cpy(char *d, const char *s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static void utoa_(u32 v, char *b) { char t[12]; int i = 0; if (!v) { b[0]='0'; b[1]=0; return; }
    while (v) { t[i++] = (char)('0'+v%10); v/=10; } int j=0; while (i) b[j++]=t[--i]; b[j]=0; }

/* record a finished job in the queue (newest last, cap 32). If the job was handed
 * to a real CUPS printer the status reads "Printing", otherwise "Printed" (to file). */
static void job_record(const char *doc, u32 size, int to_cups) {
    if (g_njob >= 32) { for (int i = 1; i < 32; i++) g_job[i-1] = g_job[i]; g_njob = 31; }
    s_cpy(g_job[g_njob].doc, doc, 40);
    s_cpy(g_job[g_njob].status, to_cups ? "Printing" : "Printed", 16);
    g_job[g_njob].size = size;
    g_njob++;
}

/* build "Printed/<doc>" and write the bytes there (creates the folder once). */
static int spool_write(const char *doc, const u8 *data, u32 len) {
    FsMkdir("Printed");
    char path[64]; s_cpy(path, "Printed/", 64);
    int k = 8; for (int i = 0; doc[i] && k < 63; i++) path[k++] = doc[i]; path[k] = 0;
    return FsWriteFile(path, data, len);
}

/* ---- public: submit a text document ---- */
void print_submit_text(const char *doc, const char *body, u32 len) {
    if (spool_write(doc, (const u8 *)body, len) == 0) {
        int cups = (sys_print_spooled(doc) == 0);
        job_record(doc, len, cups);
        wm_messagebox("Print", cups
            ? "Document sent to the printer.\n(also saved in C:\\Printed)"
            : "Document printed to C:\\Printed.\n(no printer configured)");
    } else wm_messagebox_ex("Print", "Could not print the document.", MB_ERROR);
}

/* ---- public: submit a 32bpp image as a 24-bit BMP ---- */
void print_submit_bmp(const char *doc, const u32 *px, int w, int h) {
    int rowb = (w * 3 + 3) & ~3;                 /* rows padded to 4 bytes */
    u32 pixofs = 54, size = pixofs + (u32)rowb * h;
    u8 *bmp = (u8 *)kmalloc(size); if (!bmp) { wm_messagebox_ex("Print", "Out of memory.", MB_ERROR); return; }
    for (u32 i = 0; i < size; i++) bmp[i] = 0;
    bmp[0]='B'; bmp[1]='M';
    bmp[2]=size; bmp[3]=size>>8; bmp[4]=size>>16; bmp[5]=size>>24;
    bmp[10]=pixofs;
    bmp[14]=40; bmp[18]=w; bmp[19]=w>>8; bmp[20]=w>>16; bmp[21]=w>>24;
    bmp[22]=h; bmp[23]=h>>8; bmp[24]=h>>16; bmp[25]=h>>24;
    bmp[26]=1; bmp[28]=24;                        /* planes=1, bpp=24 */
    for (int y = 0; y < h; y++) {                 /* BMP rows are bottom-up */
        u8 *row = bmp + pixofs + (u32)(h - 1 - y) * rowb;
        for (int x = 0; x < w; x++) { u32 c = px[y * w + x];
            row[x*3+0] = (u8)(c & 0xFF); row[x*3+1] = (u8)((c>>8)&0xFF); row[x*3+2] = (u8)((c>>16)&0xFF); }
    }
    int ok = spool_write(doc, bmp, size) == 0; kfree(bmp);
    if (ok) { int cups = (sys_print_spooled(doc) == 0); job_record(doc, size, cups);
        wm_messagebox("Print", cups ? "Image sent to the printer.\n(also saved in C:\\Printed)"
                                     : "Image printed to C:\\Printed.\n(no printer configured)"); }
    else wm_messagebox_ex("Print", "Could not print the image.", MB_ERROR);
}

/* ---- Print Manager window ---- */
static void pm_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, FACE);
    /* column header */
    int lx = cx + 4, ly = cy + 4, lw = cw - 8, lh = ch - 8;
    fb_fill_rect(lx, ly, lw, lh, WHITE); wm_bevel_sunken(lx, ly, lw, lh);
    int c0 = lx + 6, c1 = lx + lw * 45 / 100, c2 = lx + lw * 70 / 100;
    fb_fill_rect(lx + 1, ly + 1, lw - 2, 16, FACE);
    fb_text_t(c0, ly + 3, "Document Name", DK); fb_text_t(c1, ly + 3, "Status", DK); fb_text_t(c2, ly + 3, "Size", DK);
    fb_fill_rect(lx + 1, ly + 17, lw - 2, 1, RGB(0x80,0x80,0x80));
    int fh = (int)font_height();
    if (g_njob == 0) { fb_text_t(c0, ly + 24, "There are no documents in the queue.", RGB(0x60,0x60,0x60)); return; }
    for (int i = 0; i < g_njob; i++) {
        int y = ly + 20 + i * ROWH; if (y > ly + lh - ROWH) break;
        int sel = (i == g_sel);
        if (sel) fb_fill_rect(lx + 2, y, lw - 4, ROWH, SEL);
        u32 fg = sel ? WHITE : DK;
        fb_text_t(c0, y + (ROWH-fh)/2, g_job[i].doc, fg);
        fb_text_t(c1, y + (ROWH-fh)/2, g_job[i].status, fg);
        char sz[16]; utoa_(g_job[i].size, sz); { int n=0; while(sz[n])n++; const char*u=" bytes"; int k=n; for(int q=0;u[q];q++)sz[k++]=u[q]; sz[k]=0; }
        fb_text_t(c2, y + (ROWH-fh)/2, sz, fg);
    }
}
static void pm_click(int rx, int ry) {
    int cx, cy, cw, ch; GetClientOrigin(g_win, &cx, &cy, &cw, &ch); (void)cx; (void)cy; (void)cw;
    int i = (ry - (4 + 20)) / ROWH;
    if (i >= 0 && i < g_njob && ry < ch - 4) g_sel = i;
}
static void pm_purge(void) { g_njob = 0; g_sel = -1; }
static void pm_cancel(void) {
    if (g_sel < 0 || g_sel >= g_njob) return;
    for (int i = g_sel + 1; i < g_njob; i++) g_job[i-1] = g_job[i];
    g_njob--; g_sel = -1;
}
enum { IDM_PAUSE = 1, IDM_PURGE, IDM_CLOSE, IDM_CANCEL, IDM_ABOUT };
static const MenuItemW32 M_printer[]  = { { "Pause Printing", IDM_PAUSE }, { "Purge Print Jobs", IDM_PURGE }, { "", -1 }, { "Close", IDM_CLOSE } };
static const MenuItemW32 M_document[] = { { "Cancel", IDM_CANCEL } };
static const MenuItemW32 M_help[]     = { { "About Print Manager", IDM_ABOUT } };
static const MenuW32 PM_MENUS[] = { { "Printer", M_printer, 4 }, { "Document", M_document, 1 }, { "Help", M_help, 1 } };
static void pm_menu(int id) {
    switch (id) {
        case IDM_PURGE:  pm_purge(); break;
        case IDM_CANCEL: pm_cancel(); break;
        case IDM_CLOSE:  DestroyWindow(g_win); break;
        case IDM_ABOUT:  wm_messagebox("Print Manager",
            "Debian95 Print Manager\nJobs print to files in C:\\Printed\n(open them in Notepad or the Image Viewer)."); break;
        default: break;
    }
}
static LRESULT CALLBACK Printman_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: SetMenuW32(hwnd, PM_MENUS, 3); SetWindowIcon(hwnd, 8); SetWindowSizeGrip(hwnd); g_win = hwnd; return 0;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            pm_paint(ox, oy, cw, ch); EndPaint(hwnd, ps.hdc); return 0; }
        case WM_LBUTTONDOWN: pm_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_COMMAND:     pm_menu(LOWORD(wParam)); return 0;
        case WM_DESTROY:     g_win = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
static char g_pm_title[96];   /* persists: the WM keeps the title pointer, not a copy */
void printman_open(void) {
    if (g_win) return;
    char def[64];
    if (sys_printer_default(def, sizeof def)) s_cpy(g_pm_title, def, sizeof g_pm_title);
    else s_cpy(g_pm_title, "Print to File (C:\\Printed)", sizeof g_pm_title);
    CreateAppWindow(160, 120, 420, 240, g_pm_title, Printman_WndProc, 0);
}
