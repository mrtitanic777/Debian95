/* Debian95 — the System Control-Panel applet as a Win32 PROPERTY SHEET: General /
 * Device Manager / Performance, filled from the real system (uname, hal, sysinfo,
 * the debian95.ko driver, and the hardware probe). Resource templates + a DlgProc;
 * no hand-placed pixels. */
#include <debian95/win32.h>
#include <debian95/ke.h>      /* struct lg_kstat + sys_kstat */
#include <debian95/version.h>

int  sys_read_file(const char *path, char *out, int n);
int  sys_list_devices(char out[][64], int max);
int  sys_mem(int *total_mb, int *free_mb);
const char *hal_arch(void);
unsigned hal_ram_mb(void);

enum {
    IDC_SYS_OS = 600, IDC_SYS_KERNEL, IDC_SYS_CPU, IDC_SYS_MEM, IDC_SYS_MOD,   /* General */
    IDC_DEV_LIST = 610,                                                        /* Device Manager */
    IDC_PERF_MEM = 620, IDC_PERF_RES, IDC_PERF_MOD, IDC_PERF_UP,               /* Performance */
};

static const DLGITEM p_gen[] = {
    { WC_BUTTON, BS_GROUPBOX, 8,  6, 236, 40, -1, "System" },
    { WC_STATIC, 0,          16, 20, 220, 9, IDC_SYS_OS,     "Debian95" },
    { WC_STATIC, 0,          16, 32, 220, 9, IDC_SYS_KERNEL, "Kernel:" },
    { WC_BUTTON, BS_GROUPBOX, 8, 52, 236, 54, -1, "Computer" },
    { WC_STATIC, 0,          16, 66, 220, 9, IDC_SYS_CPU, "Processor:" },
    { WC_STATIC, 0,          16, 80, 220, 9, IDC_SYS_MEM, "Memory:" },
    { WC_STATIC, 0,          16, 94, 220, 9, IDC_SYS_MOD, "" },
};
static const DLGTEMPLATE_L t_gen = { "", 252, 124, p_gen, 7, DS_DLU };

static const DLGITEM p_dev[] = {
    { WC_STATIC,  0,          8,  6, 236, 9, -1, "Hardware devices on this computer:" },
    { WC_LISTBOX, LBS_NOTIFY, 8, 18, 236, 92, IDC_DEV_LIST, 0 },
};
static const DLGTEMPLATE_L t_dev = { "", 252, 124, p_dev, 2, DS_DLU };

static const DLGITEM p_perf[] = {
    { WC_STATIC, 0, 10, 10, 236, 9, IDC_PERF_MEM, "Memory:" },
    { WC_STATIC, 0, 10, 26, 236, 9, IDC_PERF_RES, "System Resources:" },
    { WC_STATIC, 0, 10, 42, 236, 9, -1, "File System:  32-bit" },
    { WC_STATIC, 0, 10, 58, 236, 9, IDC_PERF_MOD, "" },
    { WC_STATIC, 0, 10, 74, 236, 9, IDC_PERF_UP, "" },
};
static const DLGTEMPLATE_L t_perf = { "", 252, 124, p_perf, 5, DS_DLU };

static HWND g_sheet;
static void utoa_(unsigned v, char *b) { char t[12]; int i=0; if(!v){b[0]='0';b[1]=0;return;} while(v){t[i++]=(char)('0'+v%10);v/=10;} int j=0; while(i)b[j++]=t[--i]; b[j]=0; }
static char *cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }

static LRESULT CALLBACK Sys_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    switch (msg) {
        case WM_INITDIALOG: {
            char b[80];
            if (sys_read_file("/proc/debian95/version", b, sizeof b) == 0 && b[0]) SetDlgItemTextW32(h, IDC_SYS_OS, b);
            else SetDlgItemTextW32(h, IDC_SYS_OS, DEBIAN95_NAME);
            b[0] = 0; { char k[64] = "Kernel:  "; if (sys_read_file("/proc/sys/kernel/osrelease", k + 9, 48) == 0) SetDlgItemTextW32(h, IDC_SYS_KERNEL, k); }
            { char l[48] = "Processor:  "; cat(l, hal_arch()); SetDlgItemTextW32(h, IDC_SYS_CPU, l); }
            { char l[48] = "Memory:  ", n[12]; utoa_(hal_ram_mb(), n); cat(l, n); cat(l, " MB"); SetDlgItemTextW32(h, IDC_SYS_MEM, l);
              SetDlgItemTextW32(h, IDC_PERF_MEM, l); }
            { struct lg_kstat ks; char l[64] = "Debian95 module:  ";
              if (sys_kstat(&ks) == 0) { char n[12]; cat(l, "active v"); utoa_(ks.ver_major, n); cat(l, n); cat(l, "."); utoa_(ks.ver_minor, n); cat(l, n); }
              else cat(l, "not loaded");
              SetDlgItemTextW32(h, IDC_SYS_MOD, l); SetDlgItemTextW32(h, IDC_PERF_MOD, l); }
            { int tmb = (int)hal_ram_mb(), fmb = 0; sys_mem(&tmb, &fmb);
              char r[48] = "System Resources:  ", n[12]; int pct = tmb > 0 ? fmb * 100 / tmb : 0; utoa_((unsigned)pct, n); cat(r, n); cat(r, "% free");
              SetDlgItemTextW32(h, IDC_PERF_RES, r); }
            { struct lg_kstat ks; if (sys_kstat(&ks) == 0) { char l[64] = "Kernel uptime:  ", n[16]; utoa_((unsigned)(ks.uptime_ms/1000ULL), n); cat(l, n); cat(l, " s"); SetDlgItemTextW32(h, IDC_PERF_UP, l); } }
            { char devs[24][64]; int nd = sys_list_devices(devs, 24);
              DlgListReset(h, IDC_DEV_LIST);
              for (int i = 0; i < nd; i++) DlgListAdd(h, IDC_DEV_LIST, devs[i]); }
            return 0;
        }
        case WM_DESTROY: g_sheet = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

static const PROPSHEETPAGE_L sys_pages[] = {
    { "General",        &t_gen,  Sys_DlgProc },
    { "Device Manager", &t_dev,  Sys_DlgProc },
    { "Performance",    &t_perf, Sys_DlgProc },
};
static const PROPSHEETHEADER_L sys_psh = { "System Properties", 252, 124, sys_pages, 3, DS_DLU };

void system_sheet_open(int startpage) { if (g_sheet) return; g_sheet = PropertySheetL(&sys_psh, startpage); }
