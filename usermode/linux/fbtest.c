/* fbtest.lnx — a real static AArch64 *Linux* binary (raw syscalls, no libc) that
 * drives Debian95's /dev/fb0 exactly like fbi/mplayer/SDL fbdev do on real Linux:
 * open("/dev/fb0") -> ioctl(FBIOGET_VSCREENINFO/FSCREENINFO) -> mmap() -> draw.
 * It animates a moving gradient straight into the mmap'd scanout to prove pixels
 * a downloaded Linux binary writes reach the screen, then exits cleanly. */

typedef unsigned int   u32;
typedef unsigned long  u64;

static long sys(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                     : "memory");
    return x0;
}

#define SYS_openat  56
#define SYS_ioctl   29
#define SYS_mmap    222
#define SYS_write   64
#define SYS_exit    94
#define AT_FDCWD    (-100)
#define O_RDWR      2
#define PROT_RW     3       /* PROT_READ|PROT_WRITE */
#define MAP_SHARED  1
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

static void puts_(const char *s) { long n = 0; while (s[n]) n++; sys(SYS_write, 1, (long)s, n, 0, 0, 0); }
static void putu_(u32 v) { char b[12]; int i = 10; b[11] = 0; if (!v) { puts_("0"); return; }
    while (v && i >= 0) { b[i--] = (char)('0' + v % 10); v /= 10; } puts_(&b[i + 1]); }

void _start(void) {
    puts_("fbtest: opening /dev/fb0\n");
    long fd = sys(SYS_openat, AT_FDCWD, (long)"/dev/fb0", O_RDWR, 0, 0, 0);
    if (fd < 0) { puts_("fbtest: open failed\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }

    unsigned char vinfo[160], finfo[80];
    sys(SYS_ioctl, fd, FBIOGET_VSCREENINFO, (long)vinfo, 0, 0, 0);
    sys(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (long)finfo, 0, 0, 0);
    u32 w     = *(u32 *)(vinfo + 0);
    u32 h     = *(u32 *)(vinfo + 4);
    u32 bpp   = *(u32 *)(vinfo + 24);
    u32 pitch = *(u32 *)(finfo + 48);
    puts_("fbtest: w="); putu_(w); puts_(" h="); putu_(h);
    puts_(" bpp="); putu_(bpp); puts_(" pitch="); putu_(pitch); puts_("\n");
    if (!w || !h || !pitch) { puts_("fbtest: bad geometry\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }

    u64 len = (u64)pitch * h;
    unsigned char *fb = (unsigned char *)sys(SYS_mmap, 0, (long)len, PROT_RW, MAP_SHARED, fd, 0);
    if ((long)fb < 0) { puts_("fbtest: mmap failed\n"); sys(SYS_exit, 1, 0, 0, 0, 0, 0); }
    puts_("fbtest: mmap ok, drawing\n");

    for (u32 frame = 0; frame < 240; frame++) {
        u32 sh = frame * 3;
        for (u32 y = 0; y < h; y++) {
            u32 *row = (u32 *)(fb + (u64)y * pitch);
            for (u32 x = 0; x < w; x++) {
                u32 r = ((x + sh) * 255) / w;
                u32 g = ((y + sh) * 255) / h;
                u32 b = (u32)(((x ^ y) + sh) & 0xFF);
                row[x] = (r << 16) | (g << 8) | b;    /* 0x00RRGGBB */
            }
        }
        for (volatile u64 d = 0; d < 400000; d++) { }  /* pace the animation */
    }

    puts_("fbtest: done\n");
    sys(SYS_exit, 0, 0, 0, 0, 0, 0);
}
