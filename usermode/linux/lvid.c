/* lvid.lnx — a static AArch64 *Linux* userspace MPEG-1 video player (raw syscalls,
 * no libc). It proves the whole downloaded-media stack end to end:
 *   open()/read() a video off the REAL FAT32 disk  ->  decode it with pl_mpeg
 *   (MIT, vendored)  ->  mmap /dev/fb0 and blit decoded frames to the screen.
 * Everything a distro `mplayer -vo fb` would do, but as our own tiny binary, so
 * the OS runs downloaded Linux media code with real video output. */

#include <stddef.h>   /* size_t  (freestanding, clang-provided) */
#include <stdint.h>   /* uint8_t/uint32_t used across the pl_mpeg API */

typedef unsigned char  u8;
typedef unsigned int   u32;
typedef unsigned long  u64;

/* ---- raw Linux/aarch64 syscalls ---- */
static long sys(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d, x4 __asm__("x4") = e, x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#define SYS_openat 56
#define SYS_read   63
#define SYS_write  64
#define SYS_ioctl  29
#define SYS_mmap   222
#define SYS_fstat  80
#define SYS_exit   94
#define AT_FDCWD   (-100)

static void puts_(const char *s) { long n = 0; while (s[n]) n++; sys(SYS_write, 1, (long)s, n, 0, 0, 0); }
static void putu_(u32 v) { char b[12]; int i = 10; b[11] = 0; if (!v) { puts_("0"); return; }
    while (v && i >= 0) { b[i--] = (char)('0' + v % 10); v /= 10; } puts_(&b[i + 1]); }

/* ---- the freestanding libc surface pl_mpeg needs ---- */
void *memset(void *d, int c, unsigned long n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, unsigned long n) {
    u8 *a = d; const u8 *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
unsigned long strlen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
int abs(int x) { return x < 0 ? -x : x; }
double round(double x) { return (double)(long long)(x < 0 ? x - 0.5 : x + 0.5); }

/* bump allocator over an mmap'd arena (pl_mpeg frees at teardown; never grows) */
static u8  *g_arena; static u64 g_used, g_cap;
static void *malloc(unsigned long n) {
    n = (n + 15) & ~15ul;
    if (g_used + n > g_cap) return 0;
    void *p = g_arena + g_used; g_used += n; return p;
}
static void  free(void *p) { (void)p; }
static void *realloc(void *p, unsigned long n) { (void)p; return malloc(n); }

#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#define PLM_MALLOC(sz)     malloc((unsigned long)(sz))
#define PLM_FREE(p)        free(p)
#define PLM_REALLOC(p, sz) realloc((p), (unsigned long)(sz))
#include "pl_mpeg.h"

void _start(void) {
    /* 32 MiB arena for pl_mpeg's frame buffers */
    g_cap = 32u * 1024 * 1024;
    g_arena = (u8 *)sys(SYS_mmap, 0, (long)g_cap, 3 /*RW*/, 0x22 /*ANON|PRIVATE*/, -1, 0);
    if ((long)g_arena < 0) { puts_("lvid: arena mmap failed\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }

    /* read the whole video file off the real FAT32 disk */
    long vfd = sys(SYS_openat, AT_FDCWD, (long)"TESTVID.MPG", 0 /*O_RDONLY*/, 0, 0, 0);
    if (vfd < 0) { puts_("lvid: cannot open TESTVID.MPG\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }
    u8 stbuf[128];
    sys(SYS_fstat, vfd, (long)stbuf, 0, 0, 0, 0);
    u32 vsize = *(u32 *)(stbuf + 48);                 /* st_size */
    u8 *vdata = (u8 *)malloc(vsize);
    u32 got = 0;
    while (got < vsize) {
        long r = sys(SYS_read, vfd, (long)(vdata + got), vsize - got, 0, 0, 0);
        if (r <= 0) break; got += (u32)r;
    }
    puts_("lvid: read "); putu_(got); puts_(" bytes of MPEG-1\n");

    /* set up the decoder */
    plm_buffer_t *buf = plm_buffer_create_with_memory(vdata, (size_t)got, 0);
    plm_video_t  *vid = plm_video_create_with_buffer(buf, 1);
    if (!vid) { puts_("lvid: not an MPEG-1 video stream\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }

    /* open + mmap /dev/fb0 */
    long fbfd = sys(SYS_openat, AT_FDCWD, (long)"/dev/fb0", 2 /*O_RDWR*/, 0, 0, 0);
    if (fbfd < 0) { puts_("lvid: cannot open /dev/fb0\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }
    u8 vinfo[160], finfo[80];
    sys(SYS_ioctl, fbfd, 0x4600, (long)vinfo, 0, 0, 0);
    sys(SYS_ioctl, fbfd, 0x4602, (long)finfo, 0, 0, 0);
    u32 sw = *(u32 *)(vinfo + 0), sh = *(u32 *)(vinfo + 4), pitch = *(u32 *)(finfo + 48);
    u8 *fb = (u8 *)sys(SYS_mmap, 0, (long)((u64)pitch * sh), 3, 1 /*SHARED*/, fbfd, 0);
    if ((long)fb < 0) { puts_("lvid: fb mmap failed\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }

    int vw = plm_video_get_width(vid), vh = plm_video_get_height(vid);
    puts_("lvid: "); putu_(vw); puts_("x"); putu_(vh); puts_(" video on ");
    putu_(sw); puts_("x"); putu_(sh); puts_(" screen\n");

    /* per-frame RGB scratch, centered on the screen */
    u8 *rgb = (u8 *)malloc((u64)vw * vh * 4);
    int ox = (int)(sw - (u32)vw) / 2, oy = (int)(sh - (u32)vh) / 2;
    if (ox < 0) ox = 0; if (oy < 0) oy = 0;
    for (u32 y = 0; y < sh; y++) memset(fb + (u64)y * pitch, 0, (u64)sw * 4);   /* clean black */

    for (int frames = 0; frames < 400; frames++) {
        plm_frame_t *fr = plm_video_decode(vid);
        if (!fr) { plm_video_rewind(vid); continue; }     /* loop the clip */
        plm_frame_to_bgra(fr, rgb, vw * 4);
        for (int y = 0; y < vh && (oy + y) < (int)sh; y++) {
            u8 *drow = fb + (u64)(oy + y) * pitch + (u64)ox * 4;
            memcpy(drow, rgb + (u64)y * vw * 4, (u64)vw * 4);
        }
        for (volatile u64 d = 0; d < 250000; d++) { }     /* pace playback */
    }
    puts_("lvid: done\n");
    sys(SYS_exit, 0, 0, 0, 0, 0, 0);
}
