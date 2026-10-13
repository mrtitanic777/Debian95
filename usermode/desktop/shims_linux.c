/* Debian95 desktop - Linux userspace shims. The shared win32 subsystem (WM, apps,
 * codecs) was written against a handful of kernel services; on real Linux those
 * become plain POSIX. This file provides every one the desktop links against:
 * kmalloc/kfree, the Fs* file API (backed by a real data directory), rtc_time,
 * system-info, process spawn, and network stubs.
 *
 * FAT-era callers pass upper-case 8.3 names (e.g. "FONT.LGF", "WALL.BMP"); we map
 * those onto files in the Debian95 data dir ($DEBIAN95_DATA or /usr/share/debian95),
 * trying the name as-is then lower-cased. */
#include <debian95/types.h>
#include <debian95/nt.h>
#include <debian95/ke.h>          /* sys_* prototypes incl. struct lg_kstat */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#if defined(__has_include) && __has_include(<crypt.h>)
#include <crypt.h>          /* glibc: crypt() lives here; musl declares it in <unistd.h> */
#endif
#include <shadow.h>
#include <pwd.h>
#include <grp.h>           /* initgroups() for the drop-to-debian95 app launcher */
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ---- kernel heap -> libc ---- */
void *kmalloc(u32 n) { return malloc(n ? n : 1); }
void  kfree(void *p) { free(p); }

/* ---- the data directory that holds the desktop's assets ---- */
static const char *data_dir(void) {
    const char *d = getenv("DEBIAN95_DATA");
    return (d && *d) ? d : "/usr/share/debian95";
}
/* build "<data>/<name>", optionally lower-casing the basename. An ABSOLUTE name
 * (leading '/') is the REAL filesystem, used verbatim - this is how Explorer
 * browses the actual machine while relative names stay in the sandboxed data dir. */
static void data_path(char *out, int n, const char *name, int lower) {
    if (name[0] == '/') { snprintf(out, n, "%s", name); return; }   /* real absolute path */
    char nm[64]; int i = 0;
    for (; name[i] && i < 63; i++) { char c = name[i]; out[0] = 0;
        nm[i] = (lower && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    nm[i] = 0;
    snprintf(out, n, "%s/%s", data_dir(), nm);
}
static FILE *data_open(const char *name, const char *mode) {
    char p[512];
    data_path(p, sizeof p, name, 0);  FILE *f = fopen(p, mode);  if (f) return f;
    data_path(p, sizeof p, name, 1);  return fopen(p, mode);      /* try lower-case */
}

/* ---- Fs* API (nt.h) ---- */
int FsReadAlloc(const char *name, u8 **out, u32 *size) {
    FILE *f = data_open(name, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    u8 *buf = (u8 *)malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f); fclose(f);
    *out = buf; *size = (u32)got; return 0;
}
int FsList(char names[][16], u32 *sizes, int max) {
    DIR *d = opendir(data_dir()); if (!d) return 0;
    struct dirent *e; int n = 0;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;
        int i = 0; for (; e->d_name[i] && i < 15; i++) {
            char c = e->d_name[i]; names[n][i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        names[n][i] = 0;
        char p[512]; snprintf(p, sizeof p, "%s/%s", data_dir(), e->d_name);
        FILE *f = fopen(p, "rb"); long sz = 0;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
        sizes[n] = (u32)(sz < 0 ? 0 : sz);
        n++;
    }
    closedir(d); return n;
}
/* resolve a path: absolute (leading '/') = real filesystem, else the data dir */
static void real_path(char *out, int n, const char *path) {
    if (path && path[0] == '/') snprintf(out, n, "%s", path);
    else if (path && *path)     snprintf(out, n, "%s/%s", data_dir(), path);
    else                        snprintf(out, n, "%s", data_dir());
}
/* real directory listing (subdirs), preserving on-disk case; dirs marked. */
int FsListDir(const char *path, FsEntry *entries, int max) {
    char dir[600]; real_path(dir, sizeof dir, path);
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; int n = 0;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;
        int i = 0; for (; e->d_name[i] && i < 15; i++) entries[n].name[i] = e->d_name[i];
        entries[n].name[i] = 0;
        char p[720]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st; entries[n].is_dir = 0; entries[n].size = 0; entries[n].mtime = 0;
        if (stat(p, &st) == 0) { entries[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0; entries[n].size = (u32)st.st_size; entries[n].mtime = (u32)st.st_mtime; }
        n++;
    }
    closedir(d); return n;
}
int FsWriteFile(const char *name, const u8 *data, u32 size) {
    FILE *f = data_open(name, "wb"); if (!f) {
        char p[512]; data_path(p, sizeof p, name, 0); f = fopen(p, "wb");
    }
    if (!f) return -1;
    size_t w = fwrite(data, 1, size, f);
    fflush(f); fsync(fileno(f));                  /* persist to the disk image */
    fclose(f);
    return w == size ? 0 : -1;
}
int FsDelete(const char *name) {
    char p[512];
    data_path(p, sizeof p, name, 0); if (unlink(p) == 0) return 0;
    data_path(p, sizeof p, name, 1); return unlink(p) == 0 ? 0 : -1;
}
int FsMkdir(const char *path) {
    char p[512]; real_path(p, sizeof p, path);
    return mkdir(p, 0755) == 0 ? 0 : -1;
}
int FsRename(const char *oldp, const char *newp) {
    char a[512], b[512]; real_path(a, sizeof a, oldp); real_path(b, sizeof b, newp);
    return rename(a, b) == 0 ? 0 : -1;
}
/* recursively delete a directory tree under the data dir (real uninstall) */
static int rm_tree(const char *abs) {
    DIR *d = opendir(abs);
    if (d) { struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[600]; snprintf(c, sizeof c, "%s/%s", abs, e->d_name);
            struct stat st;
            if (stat(c, &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(c); else unlink(c);
        }
        closedir(d);
    }
    return rmdir(abs) == 0 ? 0 : -1;
}
int FsRmdir(const char *path) {
    char p[512]; real_path(p, sizeof p, path);
    return rm_tree(p);
}

/* ---- installed-hardware probes: drive which tray status icons appear ---- */
int sys_has_audio(void) {
    return (access("/dev/snd", F_OK) == 0 || access("/dev/dsp", F_OK) == 0) ? 1 : 0;
}
int sys_has_network(void) {
    DIR *d = opendir("/sys/class/net"); if (!d) return 0;
    struct dirent *e; int found = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (e->d_name[0] == 'l' && e->d_name[1] == 'o' && !e->d_name[2]) continue;   /* skip loopback */
        found = 1; break;
    }
    closedir(d); return found;
}
/* ---- real drive capacity/free space (statvfs) for My Computer / Properties ----
 * Returns 0 on success with total + free bytes of the filesystem holding `path`. */
int sys_disk_free(const char *path, unsigned long long *total, unsigned long long *avail) {
    struct statvfs s;
    if (statvfs(path && path[0] ? path : "/", &s) != 0) return -1;
    unsigned long long bs = s.f_frsize ? s.f_frsize : s.f_bsize;
    if (total) *total = (unsigned long long)s.f_blocks * bs;
    if (avail) *avail = (unsigned long long)s.f_bavail * bs;
    return 0;
}

/* ---- honest hardware probes for the Joystick / Modem applets ---- */
int sys_has_joystick(void) {
    return (access("/dev/input/js0", F_OK) == 0 || access("/dev/js0", F_OK) == 0) ? 1 : 0;
}
int sys_has_modem(char *out_dev, int n) {
    static const char *cand[] = { "/dev/modem", "/dev/ttyACM0", "/dev/ttyUSB0", 0 };
    for (int i = 0; cand[i]; i++) if (access(cand[i], F_OK) == 0) {
        if (out_dev && n > 0) { int k = 0; for (; cand[i][k] && k < n - 1; k++) out_dev[k] = cand[i][k]; out_dev[k] = 0; }
        return 1;
    }
    if (out_dev && n > 0) out_dev[0] = 0;
    return 0;
}

/* ---- real memory stats (sysinfo) for System > Performance ---- */
int sys_mem(int *total_mb, int *free_mb) {
    struct sysinfo si;
    if (sysinfo(&si) != 0) return -1;
    unsigned long u = si.mem_unit ? si.mem_unit : 1;
    if (total_mb) *total_mb = (int)((unsigned long long)si.totalram * u / (1024 * 1024));
    if (free_mb)  *free_mb  = (int)((unsigned long long)(si.freeram + si.bufferram) * u / (1024 * 1024));
    return 0;
}

/* ---- real network identity/config for the Network applet ----
 * hostname (out_host) + the primary non-loopback IPv4 (out_ip) + adapter name
 * (out_dev). Any out pointer may be null. Returns 0 if an adapter with an IP
 * was found, 1 if only a hostname is known, -1 on error. */
int sys_net_info(char *out_host, int host_n, char *out_ip, int ip_n, char *out_dev, int dev_n) {
    if (out_host && host_n > 0) { if (gethostname(out_host, host_n) != 0) out_host[0] = 0; out_host[host_n - 1] = 0; }
    if (out_ip && ip_n > 0) out_ip[0] = 0;
    if (out_dev && dev_n > 0) out_dev[0] = 0;
    int rc = 1;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        struct ifconf ifc; char buf[2048];
        ifc.ifc_len = sizeof buf; ifc.ifc_buf = buf;
        if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
            struct ifreq *it = ifc.ifc_req;
            int n = ifc.ifc_len / (int)sizeof(struct ifreq);
            for (int i = 0; i < n; i++) {
                if (it[i].ifr_addr.sa_family != AF_INET) continue;
                if (it[i].ifr_name[0] == 'l' && it[i].ifr_name[1] == 'o' && !it[i].ifr_name[2]) continue;
                struct sockaddr_in *sin = (struct sockaddr_in *)&it[i].ifr_addr;
                if (out_ip && ip_n > 0) { const char *a = inet_ntoa(sin->sin_addr); int k = 0; for (; a[k] && k < ip_n - 1; k++) out_ip[k] = a[k]; out_ip[k] = 0; }
                if (out_dev && dev_n > 0) { int k = 0; for (; it[i].ifr_name[k] && k < dev_n - 1; k++) out_dev[k] = it[i].ifr_name[k]; out_dev[k] = 0; }
                rc = 0; break;
            }
        }
        close(s);
    }
    return rc;
}

/* ---- real logon authentication against the system's /etc/shadow ----
 * The desktop runs as root (started by init), so it can read + rewrite shadow.
 * This ties the Win95-style logon to the actual Linux account. */
int sys_has_password(const char *user) {
    struct spwd *sp = getspnam(user);
    if (!sp || !sp->sp_pwdp) return 0;
    const char *h = sp->sp_pwdp;
    return (h[0] && h[0] != '!' && h[0] != '*') ? 1 : 0;   /* a real hash is set */
}
int sys_auth(const char *user, const char *pass) {
    struct spwd *sp = getspnam(user);
    if (!sp || !sp->sp_pwdp) return -1;                    /* no such account */
    const char *h = sp->sp_pwdp;
    if (!h[0]) return pass[0] == 0 ? 1 : 0;                /* empty password */
    if (h[0] == '!' || h[0] == '*') return -1;            /* locked / no login */
    char *c = crypt(pass, h);
    return (c && strcmp(c, h) == 0) ? 1 : 0;
}
int sys_set_password(const char *user, const char *pass) {
    static const char b64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    char salt[24] = "$6$"; unsigned char r[16];
    int fd = open("/dev/urandom", O_RDONLY); if (fd < 0 || read(fd, r, 16) != 16) { if (fd >= 0) close(fd); return -1; }
    close(fd);
    for (int i = 0; i < 16; i++) salt[3 + i] = b64[r[i] & 63]; salt[19] = '$'; salt[20] = 0;
    char *h = crypt(pass, salt); if (!h) return -1;
    FILE *f = fopen("/etc/shadow", "r"); if (!f) return -1;
    FILE *o = fopen("/etc/shadow.tmp", "w"); if (!o) { fclose(f); return -1; }
    char line[512]; int found = 0; size_t ul = strlen(user);
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, user, ul) && line[ul] == ':') {
            char *p = strchr(line, ':'); char *q = p ? strchr(p + 1, ':') : 0;   /* q = 2nd colon (start of field 3) */
            if (q) { fprintf(o, "%s:%s%s", user, h, q); found = 1; continue; }
        }
        fputs(line, o);
    }
    fclose(f); fclose(o);
    if (!found) { unlink("/etc/shadow.tmp"); return -1; }
    if (rename("/etc/shadow.tmp", "/etc/shadow") != 0) return -1;
    chmod("/etc/shadow", 0600);
    return 0;
}

/* ---- LUKS-encrypted /data, unlocked with the logon password ----
 * The persistent data disk (/dev/vdb) is a LUKS2 volume. The password that
 * opens it IS the logon: if cryptsetup can unlock the volume the user is
 * authenticated (no /etc/shadow needed). On unlock we mount it at /data and
 * overlay the writable desktop data dir onto it, so user settings + files
 * persist ENCRYPTED at rest. The desktop runs as root (started by init). */
#define LG_DATA_MAP "logandata"
#define LG_DATA_NODE "/dev/mapper/" LG_DATA_MAP

/* Locate the persistent data partition. On a USB/hard-disk install it is the GPT
 * partition labelled DEBIAN95-DATA (found via findfs, hardware-independent); under
 * the QEMU two-disk layout it is the whole second disk /dev/vdb. Resolved once. */
static const char *LG_DATA_DEV(void) {
    static char dev[64];
    if (dev[0]) return dev;
    FILE *p = popen("findfs PARTLABEL=DEBIAN95-DATA 2>/dev/null", "r");
    if (p) { if (fgets(dev, sizeof dev, p)) { int n = (int)strlen(dev);
                 while (n && (dev[n-1] == '\n' || dev[n-1] == '\r' || dev[n-1] == ' ')) dev[--n] = 0; }
             pclose(p); }
    struct stat st;
    if (dev[0] && stat(dev, &st) == 0) return dev;
    strcpy(dev, "/dev/vdb");                             /* QEMU fallback */
    return dev;
}

/* run a command; if pass!=NULL, feed it on stdin (for cryptsetup --key-file=-).
 * returns the child's exit status (0 = success), or -1 on spawn failure. */
static int run_tool(const char *const argv[], const char *pass) {
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        dup2(pfd[0], 0); close(pfd[0]); close(pfd[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(pfd[0]);
    if (pass && *pass) { ssize_t r = write(pfd[1], pass, strlen(pass)); (void)r; }
    close(pfd[1]);                                  /* EOF: whole passphrase read */
    int st; if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int sys_data_is_luks(void) {
    const char *av[] = { "cryptsetup", "isLuks", LG_DATA_DEV(), NULL };
    return run_tool(av, NULL) == 0 ? 1 : 0;
}

/* mount the opened /dev/mapper node at /data and overlay the desktop data dir
 * (and /etc, for any residual writes) onto it so writes persist encrypted. */
static int mount_data_and_overlays(void) {
    mkdir("/data", 0755);
    if (mount(LG_DATA_NODE, "/data", "ext4", 0, "") != 0) return -1;
    mkdir("/data/debian95", 0700);
    mkdir("/data/debian95/upper", 0700); mkdir("/data/debian95/work", 0700);
    mkdir("/data/etc", 0700);
    mkdir("/data/etc/upper", 0700); mkdir("/data/etc/work", 0700);
    if (mount("overlay", "/usr/share/debian95", "overlay", 0,
              "lowerdir=/usr/share/debian95,upperdir=/data/debian95/upper,workdir=/data/debian95/work") != 0)
        return -1;
    /* /etc overlay is best-effort (some system writes); ignore failure */
    mount("overlay", "/etc", "overlay", 0,
          "lowerdir=/etc,upperdir=/data/etc/upper,workdir=/data/etc/work");
    return 0;
}

/* tear down the dm-crypt mapping (harmless if it is not currently open). */
static void data_close_map(void) {
    const char *av[] = { "cryptsetup", "close", LG_DATA_MAP, NULL };
    run_tool(av, NULL);
}

/* Verify the passphrase WITHOUT opening the volume (screen-lock re-auth while
 * /data stays mounted). 1 = correct, 0 = wrong. */
int sys_data_verify(const char *pass) {
    const char *av[] = { "cryptsetup", "--key-file=-", "open", "--test-passphrase", LG_DATA_DEV(), NULL };
    return run_tool(av, pass) == 0 ? 1 : 0;
}

int sys_data_unlock(const char *pass) {                 /* login path */
    const char *av[] = { "cryptsetup", "--key-file=-", "open", LG_DATA_DEV(), LG_DATA_MAP, NULL };
    if (run_tool(av, pass) != 0) return 0;              /* nonzero == wrong passphrase */
    /* The passphrase was right. If mounting the overlay fails, close the mapping
     * so a retry's "open" does not hit a stale mapper and look like a bad
     * password (which would lock the user out with the correct one). */
    if (mount_data_and_overlays() != 0) { data_close_map(); return 0; }
    return 1;
}

int sys_data_format(const char *pass) {                 /* first-run path */
    /* Use PBKDF2 (600k iterations, OWASP-strong for SHA-256) rather than Argon2:
     * this build's libgcrypt Argon2 hangs, while PBKDF2 works and is fast. The
     * volume cipher is still AES-256-XTS; only the passphrase KDF differs. */
    const char *avf[] = { "cryptsetup", "--key-file=-", "-q", "luksFormat",
                          "--type", "luks2", "--pbkdf", "pbkdf2",
                          "--pbkdf-force-iterations", "600000", LG_DATA_DEV(), NULL };
    if (run_tool(avf, pass) != 0) return -1;
    const char *avo[] = { "cryptsetup", "--key-file=-", "open", LG_DATA_DEV(), LG_DATA_MAP, NULL };
    if (run_tool(avo, pass) != 0) return -1;
    const char *avm[] = { "mke2fs", "-q", "-t", "ext4", "-F", LG_DATA_NODE, NULL };
    if (run_tool(avm, NULL) != 0) { data_close_map(); return -1; }
    if (mount_data_and_overlays() != 0) { data_close_map(); return -1; }
    return 0;
}

int sys_data_rekey(const char *oldp, const char *newp) {
    /* headless luksChangeKey reads the prompts from stdin in order: the existing
     * passphrase, then the new one twice (new + verify). Feed all three. */
    char in[128];
    int n = snprintf(in, sizeof in, "%s\n%s\n%s\n", oldp, newp, newp);
    if (n <= 0 || n >= (int)sizeof in) return -1;
    const char *av[] = { "cryptsetup", "-q", "luksChangeKey", LG_DATA_DEV(), NULL };
    return run_tool(av, in) == 0 ? 0 : -1;
}

/* ---- live kernel stats from the debian95.ko driver, via the ioctl ABI ---- */
#include <debian95/debian95_ioctl.h>
int sys_kstat(struct lg_kstat *out) {
    int fd = open("/dev/debian95", O_RDWR);
    if (fd < 0) return -1;                          /* module not loaded */
    struct debian95_stat st;
    int rc = ioctl(fd, DEBIAN95_IOC_STAT, &st);
    close(fd);
    if (rc != 0) return -1;
    out->ver_major = st.ver_major; out->ver_minor = st.ver_minor;
    out->logged_in = st.logged_in; out->boot_ready = st.boot_ready;
    out->nr_procs = st.nr_procs;   out->lowmem = (st.flags & DEBIAN95_STAT_LOWMEM) ? 1 : 0;
    out->uptime_ms = st.uptime_ms; out->session_ms = st.session_ms;
    out->mem_total_kb = st.mem_total_kb; out->mem_free_kb = st.mem_free_kb;
    return 0;
}

/* ---- kernel-metered boot pacing (debian95.ko /proc/debian95/boot_ready) ----
 * Returns how many desktop items the KERNEL currently allows to be shown after
 * login. If the module isn't loaded the file is absent -> no gate (instant). */
int sys_boot_ready(void) {
    FILE *f = fopen("/proc/debian95/boot_ready", "r");
    if (!f) return 1000000;                       /* module absent -> reveal all */
    int n = 1000000; if (fscanf(f, "%d", &n) != 1) n = 1000000;
    fclose(f); return n;
}

/* ---- a real, persistent settings store (SETTINGS.INI in the data dir) ---- */
/* ---- per-user profiles: each account keeps its own SETTINGS.INI (wallpaper,
 * theme, desktop, recent docs) under <data>/users/<name>/. Empty profile = the
 * shared/global SETTINGS.INI (first run, before anyone logs in). ---- */
static char g_profile[40] = "";
static const char *settings_file(void) {
    static char path[96];
    if (!g_profile[0]) return "SETTINGS.INI";
    snprintf(path, sizeof path, "users/%s/SETTINGS.INI", g_profile);
    return path;
}
void sys_set_profile(const char *user) {
    if (!user || !user[0]) { g_profile[0] = 0; return; }
    snprintf(g_profile, sizeof g_profile, "%s", user);
    char dir[512];  data_path(dir, sizeof dir, "users", 0); mkdir(dir, 0700);
    char udir[600]; snprintf(udir, sizeof udir, "%s/%s", dir, user); mkdir(udir, 0700);
    char pf[700];   snprintf(pf, sizeof pf, "%s/SETTINGS.INI", udir);
    if (access(pf, F_OK) != 0) {            /* first use: seed from the global profile */
        char g[512]; data_path(g, sizeof g, "SETTINGS.INI", 0);
        FILE *in = fopen(g, "r"), *out = fopen(pf, "w");
        if (in && out) { char b[1024]; size_t r; while ((r = fread(b,1,sizeof b,in)) > 0) fwrite(b,1,r,out); }
        if (in) fclose(in); if (out) fclose(out);
    }
}

int sys_get_setting(const char *key, char *out, int n) {
    out[0] = 0;
    FILE *f = data_open(settings_file(), "r"); if (!f) return -1;
    char line[256]; size_t kl = strlen(key); int ret = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            char *v = line + kl + 1; int i = 0;
            for (; v[i] && v[i] != '\n' && v[i] != '\r' && i < n - 1; i++) out[i] = v[i];
            out[i] = 0; ret = 0; break;
        }
    }
    fclose(f); return ret;
}
void sys_set_setting(const char *key, const char *val) {
    static char keys[64][64], vals[64][192]; int nk = 0;
    FILE *f = data_open(settings_file(), "r");
    if (f) { char line[256];
        while (fgets(line, sizeof line, f) && nk < 64) {
            char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
            char *v = eq + 1; int L = strlen(v); while (L && (v[L-1] == '\n' || v[L-1] == '\r')) v[--L] = 0;
            snprintf(keys[nk], 64, "%s", line); snprintf(vals[nk], 192, "%s", v); nk++;
        }
        fclose(f);
    }
    int found = 0;
    for (int i = 0; i < nk; i++) if (!strcmp(keys[i], key)) { snprintf(vals[i], 192, "%s", val); found = 1; break; }
    if (!found && nk < 64) { snprintf(keys[nk], 64, "%s", key); snprintf(vals[nk], 192, "%s", val); nk++; }
    char p[512]; data_path(p, sizeof p, settings_file(), 0);
    FILE *o = fopen(p, "w"); if (!o) return;
    for (int i = 0; i < nk; i++) fprintf(o, "%s=%s\n", keys[i], vals[i]);
    fflush(o); fsync(fileno(o));                  /* flush to the disk image so it survives reboot */
    fclose(o);
}
/* enumerate every persisted key=value pair (for the Registry Editor). */
int sys_list_settings(char keys[][64], char vals[][192], int max) {
    FILE *f = data_open(settings_file(), "r"); if (!f) return 0;
    char line[256]; int n = 0;
    while (fgets(line, sizeof line, f) && n < max) {
        char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
        char *v = eq + 1; int L = strlen(v); while (L && (v[L-1] == '\n' || v[L-1] == '\r')) v[--L] = 0;
        snprintf(keys[n], 64, "%s", line); snprintf(vals[n], 192, "%s", v); n++;
    }
    fclose(f); return n;
}
/* delete a persisted key (rewrites SETTINGS.INI without it). */
void sys_del_setting(const char *key) {
    static char keys[64][64], vals[64][192]; int nk = 0;
    FILE *f = data_open(settings_file(), "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f) && nk < 64) {
        char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
        char *v = eq + 1; int L = strlen(v); while (L && (v[L-1] == '\n' || v[L-1] == '\r')) v[--L] = 0;
        if (!strcmp(line, key)) continue;             /* drop the target */
        snprintf(keys[nk], 64, "%s", line); snprintf(vals[nk], 192, "%s", v); nk++;
    }
    fclose(f);
    char p[512]; data_path(p, sizeof p, settings_file(), 0);
    FILE *o = fopen(p, "w"); if (!o) return;
    for (int i = 0; i < nk; i++) fprintf(o, "%s=%s\n", keys[i], vals[i]);
    fflush(o); fsync(fileno(o)); fclose(o);
}

/* ================= software management (apt) - Add/Remove Programs =============
 * The Win95 shell fronts the real Debian package system, so "installing a program"
 * genuinely runs apt. list/search read via popen; install/remove run apt-get
 * (the Desktop-Edition session is root, so no pkexec needed). Everything the whole
 * Debian archive offers becomes installable from the Win95 UI. */
int sys_pkg_installed(char names[][48], int max) {
    FILE *p = popen("apt-mark showmanual 2>/dev/null | grep -vE '^(lib|linux-image|linux-headers)' | sort", "r");
    if (!p) return 0;
    char line[128]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        int i = 0; for (; line[i] && line[i] != '\n' && i < 47; i++) names[n][i] = line[i]; names[n][i] = 0;
        if (names[n][0]) n++;
    }
    pclose(p); return n;
}
int sys_pkg_search(const char *q, char names[][48], char descs[][96], int max) {
    if (!q || !q[0]) return 0;
    char safe[64]; int k = 0;                      /* sanitise the query (word chars only) */
    for (int i = 0; q[i] && k < 63; i++) { char c = q[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='+'||c=='.') safe[k++] = c; }
    safe[k] = 0; if (!safe[0]) return 0;
    char cmd[160]; snprintf(cmd, sizeof cmd, "apt-cache search --names-only %s 2>/dev/null | sort | head -%d", safe, max);
    FILE *p = popen(cmd, "r"); if (!p) return 0;
    char line[256]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        char *sp = strstr(line, " - "); int i = 0;
        if (sp) { for (char *c = line; c < sp && i < 47; c++) names[n][i++] = *c; names[n][i] = 0;
                  char *d = sp + 3; int j = 0; for (; d[j] && d[j] != '\n' && j < 95; j++) descs[n][j] = d[j]; descs[n][j] = 0; }
        else    { for (; line[i] && line[i] != '\n' && i < 47; i++) names[n][i] = line[i]; names[n][i] = 0; descs[n][0] = 0; }
        if (names[n][0]) n++;
    }
    pclose(p); return n;
}
/* install/remove run in the background (apt takes a while); returns 0 = started */
static int sys_pkg_run(const char *action, const char *pkg) {
    if (!pkg || !pkg[0]) return -1;
    char safe[64]; int k = 0;
    for (int i = 0; pkg[i] && k < 63; i++) { char c = pkg[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='+'||c=='.') safe[k++] = c; }
    safe[k] = 0; if (!safe[0]) return -1;
    pid_t pid = fork(); if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        const char *av[] = { "apt-get", action, "-y", safe, NULL };
        execvp(av[0], (char *const *)av); _exit(127);
    }
    return 0;   /* fire-and-forget; the installed list refreshes when the user reopens the tab */
}
int sys_pkg_install(const char *pkg) { return sys_pkg_run("install", pkg); }
int sys_pkg_remove(const char *pkg)  { return sys_pkg_run("remove",  pkg); }

/* ---- Backup / Restore via tar — a real Microsoft Backup ----------------------
 * Backup sets are gzip tarballs under <data>/Backups/<name>.tar.gz. Creating one
 * runs `tar -czf ...` over the chosen real folders of the data dir; restoring runs
 * `tar -xzf ...` back into it. Both run synchronously and return the real exit
 * status, so a listed set genuinely exists on disk and genuinely round-trips. */
struct lg_backup { char name[64]; unsigned long long size; unsigned mtime; };

/* the pickable sources: a display name + a path RELATIVE to the data dir. Only
 * those that actually exist are offered / archived. */
static const struct { const char *label; const char *rel; } BK_SRC[] = {
    { "My Documents",       "Documents" },
    { "Desktop",            "Desktop" },
    { "Favorites",          "Favorites" },
    { "Settings (Registry)","SETTINGS.INI" },
};
#define BK_NSRC ((int)(sizeof BK_SRC / sizeof BK_SRC[0]))

int sys_backup_available(void) {
    return system("command -v tar >/dev/null 2>&1") == 0;
}

/* report the sources, marking which exist on disk (exists=1). */
int sys_backup_sources(char labels[][48], int *exists, int max) {
    int n = 0;
    for (int i = 0; i < BK_NSRC && n < max; i++) {
        snprintf(labels[n], 48, "%s", BK_SRC[i].label);
        char p[600]; snprintf(p, sizeof p, "%s/%s", data_dir(), BK_SRC[i].rel);
        struct stat st; exists[n] = (stat(p, &st) == 0) ? 1 : 0;
        n++;
    }
    return n;
}

static void bk_dir(char *out, int n) { snprintf(out, n, "%s/Backups", data_dir()); }

/* tar the picked sources (pick[i]!=0) into <data>/Backups/<name>.tar.gz. */
int sys_backup_create(const char *name, const int *pick, int npick) {
    if (!name || !name[0]) return -1;
    char safe[64]; int k = 0;                               /* sanitise the set name */
    for (int i = 0; name[i] && k < 60; i++) { char c = name[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c==' ') safe[k++] = (c==' ') ? '_' : c; }
    safe[k] = 0; if (!safe[0]) return -1;
    char dir[600]; bk_dir(dir, sizeof dir); mkdir(dir, 0755);
    char out[720]; snprintf(out, sizeof out, "%s/%s.tar.gz", dir, safe);

    /* collect the existing picked sources (relative paths, archived with -C data) */
    const char *rels[BK_NSRC]; int nr = 0;
    for (int i = 0; i < BK_NSRC && i < npick; i++) {
        if (!pick[i]) continue;
        char p[600]; snprintf(p, sizeof p, "%s/%s", data_dir(), BK_SRC[i].rel);
        struct stat st; if (stat(p, &st) != 0) continue;
        rels[nr++] = BK_SRC[i].rel;
    }
    if (nr == 0) return -2;                                  /* nothing to back up */

    const char *av[8 + BK_NSRC];
    int a = 0; av[a++] = "tar"; av[a++] = "-czf"; av[a++] = out; av[a++] = "-C"; av[a++] = data_dir();
    for (int i = 0; i < nr; i++) av[a++] = rels[i];
    av[a] = NULL;
    pid_t pid = fork(); if (pid < 0) return -1;
    if (pid == 0) { execvp(av[0], (char *const *)av); _exit(127); }
    int st; if (waitpid(pid, &st, 0) < 0) return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

/* list the .tar.gz sets with size + mtime, newest first. */
int sys_backup_list(struct lg_backup *out, int max) {
    char dir[600]; bk_dir(dir, sizeof dir);
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; int n = 0;
    while ((e = readdir(d)) && n < max) {
        int L = strlen(e->d_name);
        if (L < 8 || strcmp(e->d_name + L - 7, ".tar.gz")) continue;
        char p[720]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st; if (stat(p, &st) != 0) continue;
        int nl = L - 7; if (nl > 63) nl = 63;
        memcpy(out[n].name, e->d_name, nl); out[n].name[nl] = 0;
        out[n].size = (unsigned long long)st.st_size; out[n].mtime = (unsigned)st.st_mtime;
        n++;
    }
    closedir(d);
    for (int i = 0; i < n - 1; i++)                          /* newest first */
        for (int j = i + 1; j < n; j++)
            if (out[j].mtime > out[i].mtime) { struct lg_backup t = out[i]; out[i] = out[j]; out[j] = t; }
    return n;
}

int sys_backup_restore(const char *name) {
    if (!name || !name[0]) return -1;
    char dir[600]; bk_dir(dir, sizeof dir);
    char in[720]; snprintf(in, sizeof in, "%s/%s.tar.gz", dir, name);
    struct stat st; if (stat(in, &st) != 0) return -1;
    const char *av[] = { "tar", "-xzf", in, "-C", data_dir(), NULL };
    pid_t pid = fork(); if (pid < 0) return -1;
    if (pid == 0) { execvp(av[0], (char *const *)av); _exit(127); }
    int s; if (waitpid(pid, &s, 0) < 0) return -1;
    return (WIFEXITED(s) && WEXITSTATUS(s) == 0) ? 0 : -1;
}

int sys_backup_delete(const char *name) {
    if (!name || !name[0]) return -1;
    char dir[600]; bk_dir(dir, sizeof dir);
    char p[720]; snprintf(p, sizeof p, "%s/%s.tar.gz", dir, name);
    return unlink(p) == 0 ? 0 : -1;
}

/* ---- Fonts via fontconfig — a real Control Panel Fonts folder -----------------
 * List the installed font families (fc-list), install a new .ttf/.otf/.ttc into
 * the user font dir (~/.fonts) + rebuild the cache (fc-cache), and report the
 * real file + style metadata for a family (fc-list). Everything is live data from
 * fontconfig; no glyph rendering is faked. */
static const char *user_fonts_dir(void) {
    static char d[512]; const char *h = getenv("HOME");
    snprintf(d, sizeof d, "%s/.fonts", (h && *h) ? h : data_dir());
    return d;
}

int sys_fonts_available(void) {
    return system("command -v fc-list >/dev/null 2>&1") == 0;
}

/* installed families, de-duplicated, alphabetical. */
int sys_fonts_list(char fam[][64], char file[][96], int max) {
    FILE *p = popen("fc-list --format='%{family[0]}\\t%{file}\\n' 2>/dev/null | sort -u", "r");
    if (!p) return 0;
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, p) && n < max) {
        char *tab = strchr(line, '\t'); if (!tab) continue;
        *tab = 0; char *fpath = tab + 1;
        int L = strlen(fpath); while (L && (fpath[L-1]=='\n'||fpath[L-1]=='\r')) fpath[--L] = 0;
        if (!line[0]) continue;
        int dup = 0; for (int i = 0; i < n; i++) if (!strcmp(fam[i], line)) { dup = 1; break; }
        if (dup) continue;
        snprintf(fam[n], 64, "%s", line); snprintf(file[n], 96, "%s", fpath); n++;
    }
    pclose(p); return n;
}

/* real style/file metadata for a family (for the info window). */
int sys_font_info(const char *family, char *out, int n) {
    if (!family || !family[0]) return -1;
    char fam[96]; int k = 0;                                 /* sanitise for the shell arg */
    for (int i = 0; family[i] && k < 94; i++) { char c = family[i]; if (c != '\'' && c != '\n') fam[k++] = c; }
    fam[k] = 0;
    char cmd[256]; snprintf(cmd, sizeof cmd, "fc-list '%s' family style file 2>/dev/null | head -12", fam);
    FILE *p = popen(cmd, "r"); if (!p) return -1;
    int w = 0; char line[512]; out[0] = 0;
    while (fgets(line, sizeof line, p) && w < n - 1) {
        for (int i = 0; line[i] && w < n - 1; i++) out[w++] = line[i];
    }
    out[w] = 0; pclose(p);
    return out[0] ? 0 : -1;
}

/* copy a .ttf/.otf/.ttc into ~/.fonts and rebuild the font cache. */
int sys_font_install(const char *src) {
    if (!src || !src[0]) return -1;
    char abs[600];                                           /* resolve shell path -> real path */
    if (src[0] == '/') snprintf(abs, sizeof abs, "%s", src);
    else snprintf(abs, sizeof abs, "%s/%s", data_dir(), src);
    const char *dot = strrchr(abs, '.'); if (!dot) return -2;
    if (strcasecmp(dot, ".ttf") && strcasecmp(dot, ".otf") && strcasecmp(dot, ".ttc")) return -2;
    struct stat st; if (stat(abs, &st) != 0 || !S_ISREG(st.st_mode)) return -1;

    const char *dir = user_fonts_dir(); mkdir(dir, 0755);
    const char *base = strrchr(abs, '/'); base = base ? base + 1 : abs;
    char dst[700]; snprintf(dst, sizeof dst, "%s/%s", dir, base);
    FILE *in = fopen(abs, "rb"); if (!in) return -1;
    FILE *out = fopen(dst, "wb"); if (!out) { fclose(in); return -1; }
    char buf[8192]; size_t r; int ok = 1;
    while ((r = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, r, out) != r) { ok = 0; break; }
    fclose(in); fflush(out); fsync(fileno(out)); fclose(out);
    if (!ok) { unlink(dst); return -1; }

    char cmd[800]; snprintf(cmd, sizeof cmd, "fc-cache -f '%s' >/dev/null 2>&1", dir);
    system(cmd);
    return 0;
}

/* remove a user-installed font file (only ones under ~/.fonts are removable). */
int sys_font_delete(const char *file) {
    if (!file || !file[0]) return -1;
    const char *dir = user_fonts_dir(); size_t dl = strlen(dir);
    if (strncmp(file, dir, dl) != 0) return -2;              /* refuse system fonts */
    if (unlink(file) != 0) return -1;
    char cmd[800]; snprintf(cmd, sizeof cmd, "fc-cache -f '%s' >/dev/null 2>&1", dir);
    system(cmd);
    return 0;
}

/* ---- Dial-Up / VPN via NetworkManager (nmcli) --------------------------------
 * List the configured VPN connections (marking which are up), connect/disconnect
 * one, and import a .ovpn OpenVPN profile. Real nmcli throughout; absent nmcli (or
 * the openvpn plugin) degrades to "no connections" / a failed import, never a
 * crash. This is the live back-end for My Computer > Dial-Up Networking. */
struct lg_vpn { char name[64]; char type[16]; int up; };

int sys_vpn_available(void) {
    return system("command -v nmcli >/dev/null 2>&1") == 0;
}

int sys_vpn_list(struct lg_vpn *out, int max) {
    /* the active connection names first (to mark state) */
    char active[32][64]; int na = 0;
    FILE *a = popen("nmcli -t -f NAME connection show --active 2>/dev/null", "r");
    if (a) { char l[128]; while (na < 32 && fgets(l, sizeof l, a)) {
        int L = strlen(l); while (L && (l[L-1]=='\n'||l[L-1]=='\r')) l[--L] = 0;
        if (l[0]) snprintf(active[na++], 64, "%s", l); } pclose(a); }

    FILE *p = popen("nmcli -t -f NAME,TYPE connection show 2>/dev/null", "r");
    if (!p) return 0;
    char line[256]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        /* NAME may contain escaped ':'; split on the LAST unescaped ':' = TYPE */
        char name[64]; int k = 0; char *colon = 0;
        for (int i = 0; line[i] && line[i] != '\n'; i++) {
            if (line[i] == '\\' && line[i+1]) { i++; continue; }
            if (line[i] == ':') colon = line + i;
        }
        if (!colon) continue;
        const char *type = colon + 1;
        int L = strlen(type); char ty[32] = ""; int tk = 0;
        for (int i = 0; type[i] && type[i] != '\n' && type[i] != '\r' && tk < 31; i++) ty[tk++] = type[i];
        ty[tk] = 0;
        (void)L;
        if (strcmp(ty, "vpn") && strcmp(ty, "wireguard")) continue;   /* only VPN-like */
        for (char *s = line; s < colon && k < 63; s++) { if (*s == '\\' && s[1]) s++; name[k++] = *s; }
        name[k] = 0; if (!name[0]) continue;
        snprintf(out[n].name, 64, "%s", name);
        snprintf(out[n].type, 16, "%s", ty);
        out[n].up = 0;
        for (int i = 0; i < na; i++) if (!strcmp(active[i], name)) { out[n].up = 1; break; }
        n++;
    }
    pclose(p); return n;
}

int sys_vpn_up(const char *name) {
    if (!name || !name[0]) return -1;
    const char *av[] = { "nmcli", "connection", "up", name, NULL };
    return run_tool(av, NULL);                                /* 0 = connected */
}
int sys_vpn_down(const char *name) {
    if (!name || !name[0]) return -1;
    const char *av[] = { "nmcli", "connection", "down", name, NULL };
    return run_tool(av, NULL);
}

/* import a real OpenVPN .ovpn profile (needs network-manager-openvpn). */
int sys_vpn_import(const char *path) {
    if (!path || !path[0]) return -1;
    char abs[600];
    if (path[0] == '/') snprintf(abs, sizeof abs, "%s", path);
    else snprintf(abs, sizeof abs, "%s/%s", data_dir(), path);
    struct stat st; if (stat(abs, &st) != 0) return -1;
    const char *av[] = { "nmcli", "connection", "import", "type", "openvpn", "file", abs, NULL };
    return run_tool(av, NULL);
}

/* ---- Windows Update via apt --------------------------------------------------
 * Check which packages can be upgraded (apt list --upgradable, reading the cached
 * package lists), refresh those lists (apt-get update), and install all upgrades
 * (apt-get upgrade). Real apt throughout; absent apt degrades to "up to date". */
struct lg_update { char name[48]; char ver[48]; };

int sys_update_available(void) {
    return system("command -v apt-get >/dev/null 2>&1") == 0;
}

/* list upgradable packages from the cached lists (no network). */
int sys_update_check(struct lg_update *out, int max) {
    FILE *p = popen("apt list --upgradable 2>/dev/null | sed -n 's#^\\([^/]*\\)/[^ ]* \\([^ ]*\\) .*#\\1\\t\\2#p'", "r");
    if (!p) return 0;
    char line[256]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        char *tab = strchr(line, '\t'); if (!tab) continue;
        *tab = 0; char *ver = tab + 1;
        int L = strlen(ver); while (L && (ver[L-1]=='\n'||ver[L-1]=='\r')) ver[--L] = 0;
        if (!line[0]) continue;
        snprintf(out[n].name, 48, "%s", line); snprintf(out[n].ver, 48, "%s", ver); n++;
    }
    pclose(p); return n;
}

/* refresh the package lists (apt-get update) in the background. */
int sys_update_refresh(void) {
    const char *av[] = { "apt-get", "update", NULL };
    pid_t pid = fork(); if (pid < 0) return -1;
    if (pid == 0) { setsid(); execvp(av[0], (char *const *)av); _exit(127); }
    return 0;   /* fire-and-forget; re-check reads the refreshed lists */
}

/* install all available upgrades (apt-get upgrade) in the background. */
int sys_update_apply(void) {
    const char *av[] = { "apt-get", "-y", "upgrade", NULL };
    pid_t pid = fork(); if (pid < 0) return -1;
    if (pid == 0) { setsid(); execvp(av[0], (char *const *)av); _exit(127); }
    return 0;   /* fire-and-forget; the list clears as packages update */
}

/* ---- indexed file search ("Search companion") --------------------------------
 * Build an in-memory index of the files under the data dir once (a recursive
 * walk), then answer substring queries instantly from memory. Real filesystem
 * data; the Find app does a live walk, this one is the fast indexed variant. */
#define IDX_MAX 4096
static char g_idx_name[IDX_MAX][64]; static char g_idx_path[IDX_MAX][192]; static int g_idx_n = -1;
static void idx_walk(const char *abs, const char *rel, int depth) {
    if (depth > 12 || g_idx_n >= IDX_MAX) return;
    DIR *d = opendir(abs); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_idx_n < IDX_MAX) {
        if (e->d_name[0] == '.') continue;
        char cabs[700]; snprintf(cabs, sizeof cabs, "%s/%s", abs, e->d_name);
        char crel[256]; snprintf(crel, sizeof crel, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        struct stat st; int isdir = (stat(cabs, &st) == 0 && S_ISDIR(st.st_mode));
        snprintf(g_idx_name[g_idx_n], 64, "%s", e->d_name);
        snprintf(g_idx_path[g_idx_n], 192, "%s", crel);
        g_idx_n++;
        if (isdir) idx_walk(cabs, crel, depth + 1);
    }
    closedir(d);
}
int sys_search_build(void) { g_idx_n = 0; idx_walk(data_dir(), "", 0); return g_idx_n; }
int sys_search_query(const char *q, char names[][64], char paths[][192], int max) {
    if (g_idx_n < 0) sys_search_build();
    if (!q || !q[0]) return 0;
    char ql[128]; int i = 0; for (; q[i] && i < 127; i++) { char c = q[i]; ql[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c; } ql[i] = 0;
    int n = 0;
    for (int k = 0; k < g_idx_n && n < max; k++) {
        const char *s = g_idx_name[k]; int match = 0;        /* case-insensitive substring on the name */
        for (int a = 0; s[a] && !match; a++) {
            int b = 0; for (; ql[b]; b++) { char cc = s[a + b]; if (cc >= 'A' && cc <= 'Z') cc += 32; if (cc != ql[b]) break; }
            if (!ql[b]) match = 1;
        }
        if (match) { snprintf(names[n], 64, "%s", g_idx_name[k]); snprintf(paths[n], 192, "%s", g_idx_path[k]); n++; }
    }
    return n;
}

/* ---- Wi-Fi via NetworkManager (nmcli) — a real wireless control surface ----
 * Scan nearby networks (SSID + signal + lock), join one (WPA passphrase handed
 * to nmcli), and report the active SSID. Absent nmcli (e.g. the Appliance edition
 * without NetworkManager) degrades to "no networks", never a crash. */
struct lg_wifi { char ssid[40]; int signal; int secure; int active; };
int sys_wifi_scan(struct lg_wifi *out, int max) {
    /* terse, escaped fields: ACTIVE:SSID:SIGNAL:SECURITY (nmcli escapes literal ':') */
    FILE *p = popen("nmcli -t -e yes -f ACTIVE,SSID,SIGNAL,SECURITY dev wifi list 2>/dev/null", "r");
    if (!p) return 0;
    char line[256]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        char f[4][96]; int fi = 0, k = 0;
        for (int i = 0; line[i] && line[i] != '\n' && fi < 4; i++) {
            if (line[i] == '\\' && line[i+1]) { if (k < 95) f[fi][k++] = line[++i]; continue; }
            if (line[i] == ':') { f[fi][k] = 0; if (fi < 3) fi++; k = 0; continue; }
            if (k < 95) f[fi][k++] = line[i];
        }
        f[fi][k] = 0;
        if (fi < 3 || !f[1][0]) continue;              /* need >=4 fields; skip hidden SSIDs */
        struct lg_wifi w; memset(&w, 0, sizeof w);
        int j = 0; for (; f[1][j] && j < 39; j++) w.ssid[j] = f[1][j]; w.ssid[j] = 0;
        w.signal = atoi(f[2]);
        w.secure = (f[3][0] && strcmp(f[3], "--") != 0) ? 1 : 0;
        w.active = (f[0][0] == 'y' || f[0][0] == '*') ? 1 : 0;
        int dup = 0; for (int m = 0; m < n; m++) if (strcmp(out[m].ssid, w.ssid) == 0) {   /* keep strongest per SSID */
            dup = 1; if (w.signal > out[m].signal) out[m].signal = w.signal; out[m].active |= w.active; break; }
        if (!dup) out[n++] = w;
    }
    pclose(p);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {     /* active first, then by signal */
        int a = out[j].active * 1000 + out[j].signal, b = out[i].active * 1000 + out[i].signal;
        if (a > b) { struct lg_wifi t = out[i]; out[i] = out[j]; out[j] = t; }
    }
    return n;
}
int sys_wifi_connect(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return -1;
    const char *av_open[] = { "nmcli", "dev", "wifi", "connect", ssid, NULL };
    const char *av_pass[] = { "nmcli", "dev", "wifi", "connect", ssid, "password", pass ? pass : "", NULL };
    return run_tool((pass && *pass) ? av_pass : av_open, NULL);   /* 0 = connected */
}
int sys_wifi_active(char *out, int n) {
    if (out && n > 0) out[0] = 0;
    FILE *p = popen("nmcli -t -f ACTIVE,SSID dev wifi list 2>/dev/null | sed -n 's/^yes://p' | head -1", "r");
    if (!p) return 0;
    int got = 0;
    if (out && n > 0 && fgets(out, n, p)) { int l = (int)strlen(out);
        while (l && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = 0; got = out[0] ? 1 : 0; }
    pclose(p); return got;
}

/* ---- real system user accounts (User Accounts applet) ----
 * List the login accounts from /etc/passwd (root + UID>=1000), add one with the
 * real useradd(8), remove one with userdel(8), and set a password via the same
 * /etc/shadow path the logon uses. A genuine account-management surface. */
struct lg_user { char name[32]; int uid; int is_admin; };
int sys_user_list(struct lg_user *out, int max) {
    struct passwd *pw; int n = 0;
    setpwent();
    while (n < max && (pw = getpwent())) {
        if (!(pw->pw_uid == 0 || (pw->pw_uid >= 1000 && pw->pw_uid < 60000))) continue;
        int j = 0; for (; pw->pw_name[j] && j < 31; j++) out[n].name[j] = pw->pw_name[j]; out[n].name[j] = 0;
        out[n].uid = (int)pw->pw_uid;
        out[n].is_admin = (pw->pw_uid == 0) ? 1 : 0;
        n++;
    }
    endpwent();
    /* mark members of the sudo/admin group as administrators */
    for (int i = 0; i < n; i++) {
        if (out[i].is_admin) continue;
        char cmd[96]; snprintf(cmd, sizeof cmd, "id -nG %s 2>/dev/null", out[i].name);
        FILE *p = popen(cmd, "r"); if (!p) continue;
        char g[256]; if (fgets(g, sizeof g, p)) {
            if (strstr(g, "sudo") || strstr(g, "adm ") || strstr(g, " adm") || strstr(g, "wheel")) out[i].is_admin = 1;
        }
        pclose(p);
    }
    return n;
}
static int name_ok(const char *s) {   /* POSIX-ish login name: [a-z_][a-z0-9_-]* */
    if (!s || !s[0]) return 0;
    for (int i = 0; s[i]; i++) { char c = s[i];
        if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return 0; }
    return 1;
}
int sys_user_add(const char *name, const char *pass, int admin) {
    if (!name_ok(name)) return -1;
    const char *av[] = { "useradd", "-m", "-s", "/bin/bash", name, NULL };
    int rc = run_tool(av, NULL);
    if (rc != 0) return rc;
    if (pass && *pass) sys_set_password(name, pass);
    if (admin) { const char *ag[] = { "usermod", "-aG", "sudo", name, NULL }; run_tool(ag, NULL); }
    return 0;
}
int sys_user_del(const char *name) {
    if (!name_ok(name)) return -1;
    const char *av[] = { "userdel", "-r", name, NULL };
    return run_tool(av, NULL);
}

/* ---- real printing through CUPS (lp / lpstat) ----
 * The Print Manager keeps its C:\Printed spool file (Print-to-File always works),
 * and, when a CUPS printer is configured, also hands that file to lp(1) so it
 * prints for real. Printer discovery + the live queue come from lpstat. */
int sys_printer_default(char *out, int n) {
    if (out && n > 0) out[0] = 0;
    FILE *p = popen("lpstat -d 2>/dev/null | sed -n 's/.*: *//p'", "r");
    if (!p) return 0;
    int got = 0;
    if (out && n > 0 && fgets(out, n, p)) { int l = (int)strlen(out);
        while (l && (out[l-1] == '\n' || out[l-1] == '\r' || out[l-1] == ' ')) out[--l] = 0; got = out[0] ? 1 : 0; }
    pclose(p); return got;
}
int sys_printer_list(char names[][48], int max) {
    FILE *p = popen("lpstat -a 2>/dev/null | awk '{print $1}'", "r");
    if (!p) return 0;
    char line[128]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        int i = 0; for (; line[i] && line[i] != '\n' && i < 47; i++) names[n][i] = line[i]; names[n][i] = 0;
        if (names[n][0]) n++;
    }
    pclose(p); return n;
}
/* lp the spooled file <data>/Printed/<doc> to the default printer; 0 = queued. */
int sys_print_spooled(const char *doc) {
    if (!doc || !doc[0]) return -1;
    char def[64]; if (!sys_printer_default(def, sizeof def)) return -1;   /* no printer -> file only */
    char path[600]; snprintf(path, sizeof path, "%s/Printed/%s", data_dir(), doc);
    if (access(path, R_OK) != 0) return -1;
    const char *av[] = { "lp", path, NULL };
    return run_tool(av, NULL);
}
struct lg_pjob { char id[40]; char doc[40]; unsigned long size; };
int sys_print_queue(struct lg_pjob *out, int max) {
    FILE *p = popen("lpstat -o 2>/dev/null", "r");     /* "printer-42 user 1024 <date>" */
    if (!p) return 0;
    char line[256]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        char id[40] = "", doc[40] = ""; unsigned long sz = 0;
        if (sscanf(line, "%39s %39s %lu", id, doc, &sz) >= 1 && id[0]) {
            strncpy(out[n].id, id, sizeof out[n].id - 1); out[n].id[sizeof out[n].id - 1] = 0;
            strncpy(out[n].doc, id, sizeof out[n].doc - 1); out[n].doc[sizeof out[n].doc - 1] = 0;
            out[n].size = sz; n++;
        }
    }
    pclose(p); return n;
}
int sys_print_cancel(const char *id) {
    if (!id || !id[0]) return -1;
    const char *av[] = { "cancel", id, NULL };
    return run_tool(av, NULL);
}

/* ---- removable / mounted drives (My Computer) via lsblk + udisks ----
 * Lists mounted partitions with a mountpoint (skipping the system roots), so a
 * plugged-in USB stick shows up as a drive that opens in Explorer. Eject unmounts
 * it with udisksctl (falls back to umount). */
struct lg_disk { char mount[128]; char label[32]; char size[16]; int removable; };
/* mount any inserted-but-unmounted removable media so it shows up in My Computer.
 * udisksctl mounts under /media with policykit (works as the desktop root too). */
void sys_disk_automount(void) {
    FILE *p = popen("lsblk -rno NAME,RM,TYPE,MOUNTPOINT 2>/dev/null", "r");
    if (!p) return;
    char line[256];
    while (fgets(line, sizeof line, p)) {
        char name[64] = "", type[16] = "", mnt[160] = ""; int rm = 0;
        int f = sscanf(line, "%63s %d %15s %159s", name, &rm, type, mnt);
        if (f < 3 || !name[0]) continue;
        int mounted = (f >= 4 && mnt[0] == '/');
        if (rm && !mounted && (!strcmp(type, "part") || !strcmp(type, "rom"))) {
            char dev[80]; snprintf(dev, sizeof dev, "/dev/%s", name);
            const char *av[] = { "udisksctl", "mount", "-b", dev, NULL };
            if (run_tool(av, NULL) != 0) {            /* udisks absent/failed: mount directly (we are root) */
                char mp[128]; snprintf(mp, sizeof mp, "/media/%s", name);
                mkdir("/media", 0755); mkdir(mp, 0755);
                const char *mv[] = { "mount", dev, mp, NULL };
                run_tool(mv, NULL);
            }
        }
    }
    pclose(p);
}
int sys_disk_list(struct lg_disk *out, int max) {
    /* NAME MOUNTPOINT LABEL SIZE RM TYPE, tab-safe via -P key="val" pairs */
    FILE *p = popen("lsblk -rno MOUNTPOINT,LABEL,SIZE,RM,TYPE 2>/dev/null", "r");
    if (!p) return 0;
    char line[400]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        char mnt[160] = "", lbl[64] = "", sz[24] = "", type[16] = ""; int rm = 0;
        /* fields are space-separated; MOUNTPOINT may be empty (leading space) */
        if (sscanf(line, "%159s %63s %23s %d %15s", mnt, lbl, sz, &rm, type) < 5) {
            /* empty mountpoint => first token is really LABEL; skip unmounted */
            continue;
        }
        if (strcmp(type, "part") != 0 && strcmp(type, "rom") != 0) continue;
        if (!mnt[0] || mnt[0] != '/') continue;
        if (!strcmp(mnt, "/") || !strncmp(mnt, "/boot", 5) || !strncmp(mnt, "/usr", 4) ||
            !strncmp(mnt, "/var", 4) || !strncmp(mnt, "/etc", 4)) continue;   /* system roots */
        struct lg_disk d; memset(&d, 0, sizeof d);
        strncpy(d.mount, mnt, sizeof d.mount - 1);
        strncpy(d.label, (lbl[0] && strcmp(lbl, "-")) ? lbl : "Removable Disk", sizeof d.label - 1);
        strncpy(d.size, sz, sizeof d.size - 1);
        d.removable = rm;
        out[n++] = d;
    }
    pclose(p); return n;
}
/* ---- system services (Services manager) via systemctl ----
 * Lists real systemd services with running + enabled state, and starts/stops one.
 * On a non-systemd host the list simply comes back empty. */
struct lg_svc { char name[48]; char desc[64]; int running; int enabled; };
static int svc_ok(const char *s) {   /* unit token: letters, digits, . _ - @ : */
    if (!s || !s[0]) return 0;
    for (int i = 0; s[i]; i++) { char c = s[i];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'||c=='@'||c==':')) return 0; }
    return 1;
}
int sys_svc_list(struct lg_svc *out, int max) {
    FILE *p = popen("systemctl list-units --type=service --all --no-legend --no-pager 2>/dev/null", "r");
    if (!p) return 0;
    char line[400]; int n = 0;
    while (n < max && fgets(line, sizeof line, p)) {
        /* [●] NAME.service LOAD ACTIVE SUB DESCRIPTION... */
        char *s = line; while (*s == ' ' || (unsigned char)*s > 127 || *s == '\t') s++;   /* skip bullet/space */
        char name[64] = "", load[24] = "", act[24] = "", sub[24] = "";
        int c = sscanf(s, "%63s %23s %23s %23s", name, load, act, sub);
        if (c < 4 || !name[0]) continue;
        /* description is the remainder after the 4th column */
        char *d = s; for (int f = 0; f < 4 && d; f++) { while (*d && *d != ' ') d++; while (*d == ' ') d++; }
        /* strip a trailing ".service" for a Windows-like display name */
        char disp[48]; int dn = 0; for (int i = 0; name[i] && dn < 47; i++) disp[dn++] = name[i]; disp[dn] = 0;
        char *dot = strstr(disp, ".service"); if (dot) *dot = 0;
        struct lg_svc v; memset(&v, 0, sizeof v);
        strncpy(v.name, disp, sizeof v.name - 1);
        if (d && *d) { int j = 0; for (; d[j] && d[j] != '\n' && j < 63; j++) v.desc[j] = d[j]; v.desc[j] = 0; }
        v.running = (strcmp(act, "active") == 0) ? 1 : 0;
        out[n++] = v;
    }
    pclose(p);
    /* enabled state: one is-enabled call per service */
    for (int i = 0; i < n; i++) {
        char cmd[128]; snprintf(cmd, sizeof cmd, "systemctl is-enabled %s.service 2>/dev/null", out[i].name);
        FILE *q = popen(cmd, "r"); if (!q) continue;
        char st[32] = ""; if (fgets(st, sizeof st, q)) out[i].enabled = (strncmp(st, "enabled", 7) == 0) ? 1 : 0;
        pclose(q);
    }
    return n;
}
static int svc_action(const char *action, const char *name) {
    if (!svc_ok(name)) return -1;
    char unit[64]; snprintf(unit, sizeof unit, "%s.service", name);
    const char *av[] = { "systemctl", action, unit, NULL };
    return run_tool(av, NULL);
}
int sys_svc_start(const char *name) { return svc_action("start", name); }
int sys_svc_stop (const char *name) { return svc_action("stop",  name); }

int sys_disk_eject(const char *mount) {
    if (!mount || mount[0] != '/') return -1;
    /* resolve the backing device so udisksctl (policykit, no root) can unmount it */
    char dev[128] = ""; char cmd[256];
    snprintf(cmd, sizeof cmd, "findmnt -no SOURCE --target %s 2>/dev/null", mount);
    FILE *p = popen(cmd, "r");
    if (p) { if (fgets(dev, sizeof dev, p)) { int l = (int)strlen(dev);
        while (l && (dev[l-1] == '\n' || dev[l-1] == '\r')) dev[--l] = 0; } pclose(p); }
    if (dev[0]) { const char *av[] = { "udisksctl", "unmount", "-b", dev, NULL };
        if (run_tool(av, NULL) == 0) return 0; }
    const char *uv[] = { "umount", mount, NULL };      /* fallback (desktop runs as root) */
    return run_tool(uv, NULL);
}

/* ---- real OS processes (Task Manager) - straight from /proc ---- */
#include <signal.h>
int sys_proc_list(struct lg_proc *out, int max) {
    DIR *d = opendir("/proc"); if (!d) return 0;
    struct dirent *e; int n = 0; unsigned long pg = (unsigned long)(sysconf(_SC_PAGESIZE) / 1024);
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        int pid = atoi(e->d_name); if (pid <= 0) continue;
        char p[64]; FILE *f;
        snprintf(p, sizeof p, "/proc/%d/comm", pid);
        f = fopen(p, "r"); if (!f) continue;
        out[n].name[0] = 0;
        if (fgets(out[n].name, sizeof out[n].name, f)) {
            int L = (int)strlen(out[n].name);
            while (L && (out[n].name[L-1] == '\n' || out[n].name[L-1] == '\r')) out[n].name[--L] = 0;
        }
        fclose(f);
        unsigned long sz = 0, res = 0;
        snprintf(p, sizeof p, "/proc/%d/statm", pid);
        f = fopen(p, "r"); if (f) { if (fscanf(f, "%lu %lu", &sz, &res) != 2) res = 0; fclose(f); }
        out[n].pid = pid; out[n].rss_kb = res * pg; n++;
    }
    closedir(d); return n;
}
void sys_proc_kill(int pid) { if (pid > 1) kill(pid, SIGTERM); }

/* ---- kernel-tracked session via the debian95.ko control device ----
 * The GUI logon calls these so the SESSION state lives in the kernel
 * (/dev/debian95 + /proc/debian95/session), not just the shell. No-op if the
 * module isn't loaded. */
void sys_session_login(const char *user) {
    int fd = open("/dev/debian95", O_WRONLY); if (fd < 0) return;
    char b[80]; int n = snprintf(b, sizeof b, "login:%s\n", user ? user : "administrator");
    if (write(fd, b, n) < 0) { /* ignore */ } close(fd);
}
void sys_session_logout(void) {
    int fd = open("/dev/debian95", O_WRONLY); if (fd < 0) return;
    if (write(fd, "logout\n", 7) < 0) { /* ignore */ } close(fd);
}

/* play the WAV assigned to a system event (real Win95 event sounds). The
 * assignment lives in SETTINGS.INI ("snd_<event>=<file>"); a built-in default
 * applies when unset. "(None)" = silent. */
void snd_play(const char *name);            /* fwd (defined below) */
void sandbox_nnp(void);                      /* fwd: no-new-privs for spawned children */
/* set the REAL system volume / mute via ALSA (the tray control) */
/* run argv detached, output discarded; a missing tool just _exit(127)s harmlessly */
static void spawn_bg(const char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) { setsid(); int nul = open("/dev/null", O_WRONLY); if (nul >= 0) { dup2(nul,1); dup2(nul,2); }
        execvp(argv[0], (char *const *)argv); _exit(127); }
}
/* Set the volume on whatever audio stack is present: PipeWire (wpctl) or
 * PulseAudio (pactl) drive the real default sink on the Desktop edition; amixer
 * covers bare ALSA on the Appliance edition. Applying to all is idempotent. */
void sys_set_volume(int pct) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    char pctf[12]; snprintf(pctf, sizeof pctf, "%d%%", pct);
    char frac[12]; snprintf(frac, sizeof frac, "%d.%02d", pct / 100, pct % 100);   /* wpctl wants 0.00-1.00 */
    const char *wp[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", frac, NULL };            spawn_bg(wp);
    const char *pa[] = { "pactl", "set-sink-volume", "@DEFAULT_SINK@", pctf, NULL };             spawn_bg(pa);
    const char *am[] = { "amixer", "-q", "sset", "Master", pctf, NULL };                         spawn_bg(am);
    const char *ap[] = { "amixer", "-q", "sset", "PCM", pctf, NULL };                            spawn_bg(ap);
}
void sys_set_mute(int on) {
    const char *w = on ? "1" : "0", *p = on ? "1" : "0", *a = on ? "mute" : "unmute";
    const char *wp[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", w, NULL };                 spawn_bg(wp);
    const char *pa[] = { "pactl", "set-sink-mute", "@DEFAULT_SINK@", p, NULL };                  spawn_bg(pa);
    const char *am[] = { "amixer", "-q", "sset", "Master", a, NULL };                            spawn_bg(am);
}

/* ---- audio output (sink) selection: the Multimedia applet "Preferred device"
 * dropdown. Enumerates PulseAudio/PipeWire sinks via pactl (pipewire-pulse
 * provides pactl on the Desktop edition), lists their human-readable
 * descriptions, and switches the default sink + moves playing streams onto it. */
static void rstrip(char *s) { int n = (int)strlen(s); while (n && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' ')) s[--n]=0; }
int sys_audio_outputs(char descs[][96], char ids[][128], int max) {
    FILE *p = popen("pactl list sinks 2>/dev/null", "r"); if (!p) return 0;
    char line[512]; int n = 0; int have_name = 0;
    while (fgets(line, sizeof line, p) && n < max) {
        char *s = line; while (*s==' '||*s=='\t') s++;
        if (!strncmp(s, "Name:", 5)) {
            char *v = s + 5; while (*v==' '||*v=='\t') v++; rstrip(v);
            snprintf(ids[n], 128, "%s", v); descs[n][0] = 0; have_name = 1;
        } else if (have_name && !strncmp(s, "Description:", 12)) {
            char *v = s + 12; while (*v==' '||*v=='\t') v++; rstrip(v);
            snprintf(descs[n], 96, "%s", v); n++; have_name = 0;
        }
    }
    pclose(p);
    /* no description seen but a name was: still count it, labelled by its id */
    for (int i = 0; i < n; i++) if (!descs[i][0]) snprintf(descs[i], 96, "%s", ids[i]);
    return n;
}
int sys_audio_current(char *id, int n) {
    id[0] = 0;
    FILE *p = popen("pactl get-default-sink 2>/dev/null", "r");
    if (p) { if (fgets(id, n, p)) rstrip(id); pclose(p); }
    if (!id[0]) {   /* older pactl: parse `pactl info` */
        p = popen("pactl info 2>/dev/null", "r");
        if (p) { char line[256]; while (fgets(line, sizeof line, p)) {
            if (!strncmp(line, "Default Sink:", 13)) { char *v = line + 13; while (*v==' ') v++; rstrip(v); snprintf(id, n, "%s", v); break; } }
            pclose(p); }
    }
    return id[0] ? 0 : -1;
}
void sys_audio_set_output(const char *id) {
    if (!id || !*id) return;
    const char *sd[] = { "pactl", "set-default-sink", id, NULL }; spawn_bg(sd);
    /* move already-playing streams onto the new default */
    char cmd[320];
    snprintf(cmd, sizeof cmd,
        "pactl list short sink-inputs 2>/dev/null | cut -f1 | while read i; do pactl move-sink-input \"$i\" '%s'; done", id);
    const char *mv[] = { "sh", "-c", cmd, NULL }; spawn_bg(mv);
    sys_set_setting("audio_sink", id);   /* remember the choice */
}

/* ---- display brightness: the Display applet "Settings" slider + the tray.
 * Drives the real backlight via /sys/class/backlight/<dev>/brightness (relative
 * to max_brightness); falls back to brightnessctl. -1 when no backlight exists
 * (typical on a desktop / in a VM). ---- */
static int backlight_dir(char *out, int n) {   /* first /sys/class/backlight entry; 0 ok */
    DIR *d = opendir("/sys/class/backlight"); if (!d) return -1;
    struct dirent *e; int got = -1;
    while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; snprintf(out, n, "/sys/class/backlight/%s", e->d_name); got = 0; break; }
    closedir(d); return got;
}
static long read_long_file(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) return -1; long v = -1; if (fscanf(f, "%ld", &v) != 1) v = -1; fclose(f); return v;
}
int sys_has_brightness(void) { char dir[256]; return backlight_dir(dir, sizeof dir) == 0 ? 1 : 0; }
int sys_brightness_get(void) {
    char dir[256]; if (backlight_dir(dir, sizeof dir) != 0) return -1;
    char p[320]; snprintf(p, sizeof p, "%s/brightness", dir);     long cur = read_long_file(p);
    snprintf(p, sizeof p, "%s/max_brightness", dir);               long mx  = read_long_file(p);
    if (cur < 0 || mx <= 0) return -1;
    int pct = (int)((cur * 100 + mx / 2) / mx); if (pct < 0) pct = 0; if (pct > 100) pct = 100; return pct;
}
void sys_brightness_set(int pct) {
    if (pct < 5) pct = 5; if (pct > 100) pct = 100;              /* never fully dark */
    char dir[256]; if (backlight_dir(dir, sizeof dir) != 0) return;
    char p[320]; snprintf(p, sizeof p, "%s/max_brightness", dir); long mx = read_long_file(p);
    if (mx <= 0) return;
    long val = (mx * pct + 50) / 100; if (val < 1) val = 1;
    snprintf(p, sizeof p, "%s/brightness", dir);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%ld\n", val); fclose(f); }
    else {                                                       /* not writable -> brightnessctl */
        char arg[16]; snprintf(arg, sizeof arg, "%d%%", pct);
        const char *bc[] = { "brightnessctl", "-q", "set", arg, NULL }; spawn_bg(bc);
    }
    char b[8]; snprintf(b, sizeof b, "%d", pct); sys_set_setting("brightness", b);
}

/* ---- Bluetooth: the Control-Panel Bluetooth applet, over bluetoothctl (BlueZ).
 * Enumerates known/scanned devices, toggles the adapter, and connects/pairs.
 * Returns "unavailable" cleanly when there is no adapter (VM / no hardware). ---- */
int sys_bt_available(void) {
    DIR *d = opendir("/sys/class/bluetooth"); if (!d) return 0;
    struct dirent *e; int any = 0;
    while ((e = readdir(d))) { if (e->d_name[0] != '.') { any = 1; break; } }
    closedir(d); return any;
}
int sys_bt_powered(void) {
    FILE *p = popen("bluetoothctl show 2>/dev/null | grep -m1 'Powered:'", "r"); if (!p) return 0;
    char line[128]; int on = 0; if (fgets(line, sizeof line, p)) on = strstr(line, "yes") != 0; pclose(p); return on;
}
void sys_bt_power(int on) {
    const char *a[] = { "bluetoothctl", "power", on ? "on" : "off", NULL }; spawn_bg(a);
}
void sys_bt_scan(int on) {
    if (on) { const char *a[] = { "sh", "-c", "bluetoothctl --timeout 12 scan on >/dev/null 2>&1", NULL }; spawn_bg(a); }
}
/* list devices: fills names/macs, marks which are connected. Returns count. */
int sys_bt_list(char names[][64], char macs[][20], int *connected, int max) {
    FILE *p = popen("bluetoothctl devices 2>/dev/null", "r"); if (!p) return 0;
    char line[256]; int n = 0;
    while (fgets(line, sizeof line, p) && n < max) {
        /* "Device AA:BB:CC:DD:EE:FF  Name With Spaces" */
        if (strncmp(line, "Device ", 7)) continue;
        char *mac = line + 7; char *sp = strchr(mac, ' '); if (!sp) continue; *sp = 0;
        char *name = sp + 1; rstrip(name);
        snprintf(macs[n], 20, "%s", mac); snprintf(names[n], 64, "%s", name[0] ? name : mac);
        connected[n] = 0; n++;
    }
    pclose(p);
    for (int i = 0; i < n; i++) {   /* per-device connected flag */
        char cmd[96]; snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null | grep -m1 'Connected:'", macs[i]);
        FILE *q = popen(cmd, "r"); if (q) { char l[96]; if (fgets(l, sizeof l, q)) connected[i] = strstr(l, "yes") != 0; pclose(q); }
    }
    return n;
}
void sys_bt_connect(const char *mac)    { if (!mac||!*mac) return; const char *a[]={"sh","-c",0,0}; char c[128]; snprintf(c,sizeof c,"bluetoothctl pair %s; bluetoothctl connect %s",mac,mac); a[2]=c; spawn_bg(a); }
void sys_bt_disconnect(const char *mac) { if (!mac||!*mac) return; const char *a[]={"bluetoothctl","disconnect",mac,NULL}; spawn_bg(a); }

/* ---- Device Manager: enumerate the REAL hardware tree (lspci / lsusb / cpuinfo /
 * lsblk), classified into Win95-style categories. Each entry is {category, name};
 * the app groups them under a collapsible tree. ---- */
/* category ids - MUST match the DEVCAT_* list in devmgr.c */
enum { DC_DISK=0, DC_DISPLAY, DC_SOUND, DC_NET, DC_BT, DC_CPU, DC_USB, DC_SYSTEM, DC_NCAT };
static int strcasestr2(const char *h, const char *n);   /* fwd */
static int dev_class_cat(const char *cls) {   /* map an lspci class string -> category */
    if (strcasestr2(cls,"VGA")||strcasestr2(cls,"Display")||strcasestr2(cls,"3D con")) return DC_DISPLAY;
    if (strcasestr2(cls,"Audio")||strcasestr2(cls,"Multimedia"))                       return DC_SOUND;
    if (strcasestr2(cls,"Ethernet")||strcasestr2(cls,"Network con"))                   return DC_NET;
    if (strcasestr2(cls,"USB"))                                                        return DC_USB;
    if (strcasestr2(cls,"SATA")||strcasestr2(cls,"IDE")||strcasestr2(cls,"RAID")||
        strcasestr2(cls,"Non-Vol")||strcasestr2(cls,"Mass stor")||strcasestr2(cls,"SCSI")) return DC_DISK;
    if (strcasestr2(cls,"Bluetooth"))                                                  return DC_BT;
    return DC_SYSTEM;
}
/* a tiny case-insensitive substring (no POSIX strcasestr guarantee) */
static int strcasestr2(const char *h, const char *n) {
    for (; *h; h++) { const char *a=h,*b=n;
        while (*a && *b && ((*a|32)==(*b|32))) { a++; b++; }
        if (!*b) return 1; }
    return 0;
}
/* read one quoted "field" from *p, advancing *p past it; returns 0 on none */
static int lspci_field(const char **p, char *out, int n) {
    const char *s = *p; while (*s && *s != '"') s++; if (!*s) return 0; s++;
    int k = 0; while (*s && *s != '"' && k < n-1) out[k++] = *s++; out[k] = 0;
    if (*s == '"') s++; *p = s; return 1;
}
int sys_device_list(struct lg_device *out, int max) {
    int n = 0;
    /* --- PCI devices (the bulk of the tree) --- */
    FILE *p = popen("lspci -mm 2>/dev/null", "r");
    if (p) { char line[512];
        while (fgets(line, sizeof line, p) && n < max) {
            const char *c = line; while (*c && *c != ' ') c++;       /* skip slot */
            char cls[64], ven[96], dev[96];
            if (!lspci_field(&c, cls, sizeof cls)) continue;
            lspci_field(&c, ven, sizeof ven); lspci_field(&c, dev, sizeof dev);
            out[n].cat = dev_class_cat(cls);
            snprintf(out[n].name, sizeof out[n].name, "%s %s", ven, dev); n++;
        }
        pclose(p);
    }
    /* --- processors --- */
    p = popen("grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2; nproc 2>/dev/null", "r");
    if (p) { char model[96] = "", cnt[16] = ""; char line[128];
        if (fgets(line, sizeof line, p)) { rstrip(line); char *s=line; while(*s==' ')s++; snprintf(model,sizeof model,"%s",s); }
        if (fgets(cnt, sizeof cnt, p)) rstrip(cnt);
        pclose(p);
        int ncpu = cnt[0] ? atoi(cnt) : 1; if (ncpu < 1) ncpu = 1; if (ncpu > 64) ncpu = 64;
        for (int i = 0; i < ncpu && n < max; i++) { out[n].cat = DC_CPU; snprintf(out[n].name, sizeof out[n].name, "%s", model[0]?model:"Processor"); n++; }
    }
    /* --- disk drives (models) --- */
    p = popen("lsblk -dno NAME,MODEL,TYPE 2>/dev/null | awk '$NF==\"disk\"'", "r");
    if (p) { char line[160];
        while (fgets(line, sizeof line, p) && n < max) { rstrip(line);
            char nm[32]="", md[96]=""; sscanf(line, "%31s %95[^\n]", nm, md);
            /* md ends with "disk"; trim it */
            char *d = strstr(md, " disk"); if (d) *d = 0;
            out[n].cat = DC_DISK; snprintf(out[n].name, sizeof out[n].name, "%s (%s)", md[0]?md:"Disk", nm); n++;
        }
        pclose(p);
    }
    /* --- USB devices --- */
    p = popen("lsusb 2>/dev/null", "r");
    if (p) { char line[200];
        while (fgets(line, sizeof line, p) && n < max) {
            char *id = strstr(line, "ID "); if (!id) continue;
            char *nm = id + 3; while (*nm && *nm != ' ') nm++; if (*nm == ' ') nm++;   /* skip the xxxx:xxxx */
            rstrip(nm); if (!*nm) continue;
            out[n].cat = DC_USB; snprintf(out[n].name, sizeof out[n].name, "%s", nm); n++;
        }
        pclose(p);
    }
    return n;
}

/* ---- Event Viewer: structured journal entries (type / time / source / message)
 * from `journalctl -o json` (one JSON object per line, newest first). Level maps
 * syslog PRIORITY -> 0 Error (<=3), 1 Warning (4), 2 Information (>=5). ---- */
static int json_str(const char *line, const char *key, char *out, int n) {
    char pat[48]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat); if (!p) { out[0] = 0; return 0; }
    p += strlen(pat); if (*p != '"') { out[0] = 0; return 0; } p++;   /* only string values */
    int k = 0; while (*p && *p != '"' && k < n-1) { if (*p == '\\' && p[1]) p++; out[k++] = *p++; }
    out[k] = 0; return 1;
}
int sys_log_list(struct lg_logent *out, int max) {
    FILE *p = popen("journalctl -b -r -n 400 -o json --no-pager 2>/dev/null || journalctl -r -n 400 -o json --no-pager 2>/dev/null", "r");
    if (!p) return 0;
    char line[4096]; int n = 0;
    while (fgets(line, sizeof line, p) && n < max) {
        char pri[8], ts[24], src[40], msg[200];
        json_str(line, "PRIORITY", pri, sizeof pri);
        int prio = pri[0] ? atoi(pri) : 6;
        out[n].level = prio <= 3 ? 0 : prio == 4 ? 1 : 2;
        if (!json_str(line, "SYSLOG_IDENTIFIER", src, sizeof src) || !src[0])
            json_str(line, "_COMM", src, sizeof src);
        snprintf(out[n].source, sizeof out[n].source, "%s", src[0] ? src : "system");
        if (json_str(line, "__REALTIME_TIMESTAMP", ts, sizeof ts) && ts[0]) {
            long long us = atoll(ts); time_t t = (time_t)(us / 1000000);
            struct tm *lt = localtime(&t);
            if (lt) strftime(out[n].time, sizeof out[n].time, "%m/%d %H:%M:%S", lt); else out[n].time[0] = 0;
        } else out[n].time[0] = 0;
        if (!json_str(line, "MESSAGE", msg, sizeof msg)) msg[0] = 0;
        snprintf(out[n].msg, sizeof out[n].msg, "%s", msg);
        n++;
    }
    pclose(p);
    return n;
}

/* ---- Network Neighborhood (SMB/CIFS): discover workgroup computers, list a
 * host's shares, and mount one. Uses the Samba client tools; degrades to "no
 * computers" where none are installed or reachable (e.g. in a VM). ---- */
int sys_net_hosts(char names[][64], int max) {
    int n = 0;
    /* avahi (modern networks) first, then NetBIOS broadcast */
    FILE *p = popen("avahi-browse -ptr _smb._tcp 2>/dev/null | awk -F';' '/^=/{print $7}' | sort -u", "r");
    if (p) { char line[128]; while (fgets(line, sizeof line, p) && n < max) { rstrip(line); if (line[0]) snprintf(names[n++], 64, "%s", line); } pclose(p); }
    if (n == 0) {
        p = popen("nmblookup -S '*' 2>/dev/null | awk '/<00>/{print $1}' | sort -u", "r");
        if (p) { char line[128]; while (fgets(line, sizeof line, p) && n < max) { rstrip(line); if (line[0] && line[0] != '\t') snprintf(names[n++], 64, "%s", line); } pclose(p); }
    }
    return n;
}
int sys_net_shares(const char *host, char shares[][64], int max) {
    if (!host || !*host) return 0;
    char cmd[256]; snprintf(cmd, sizeof cmd, "smbclient -N -g -L //%s 2>/dev/null | awk -F'|' '/^Disk\\|/{print $2}'", host);
    FILE *p = popen(cmd, "r"); if (!p) return 0;
    char line[160]; int n = 0;
    while (fgets(line, sizeof line, p) && n < max) { rstrip(line);
        if (line[0] && line[strlen(line)-1] != '$') snprintf(shares[n++], 64, "%s", line);   /* skip admin $ shares */
    }
    pclose(p); return n;
}
int sys_net_mount(const char *host, const char *share, char *mountpoint, int mn) {
    if (!host || !*host || !share || !*share) return -1;
    char mp[160]; snprintf(mp, sizeof mp, "/media/%s_%s", host, share);
    for (char *c = mp + 7; *c; c++) if (*c == ' ' || *c == '/') *c = '_';
    mkdir("/media", 0755); mkdir(mp, 0755);
    char src[200]; snprintf(src, sizeof src, "//%s/%s", host, share);
    const char *a[] = { "mount", "-t", "cifs", src, mp, "-o", "guest,vers=3.0,uid=0,gid=0,ro", NULL };
    int rc = run_tool(a, 0);
    if (rc != 0) { const char *b[] = { "mount", "-t", "cifs", src, mp, "-o", "guest,vers=2.1,uid=0,gid=0,ro", NULL }; rc = run_tool(b, 0); }
    if (rc != 0) { rmdir(mp); return -1; }
    snprintf(mountpoint, mn, "%s", mp);
    return 0;
}

/* ---- Scheduled Tasks: a front-end to the user's crontab (cron). Each task is a
 * schedule (5 cron fields or @reboot/@daily...) plus a command. ---- */
static int cron_read_all(char *buf, int n) {        /* current crontab into buf; returns length */
    FILE *p = popen("crontab -l 2>/dev/null", "r"); if (!p) { buf[0] = 0; return 0; }
    int len = 0; int c;
    while ((c = fgetc(p)) != EOF && len < n - 1) buf[len++] = (char)c;
    buf[len] = 0; pclose(p); return len;
}
int sys_cron_list(struct lg_cron *out, int max) {
    FILE *p = popen("crontab -l 2>/dev/null", "r"); if (!p) return 0;
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, p) && n < max) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == '\r' || *s == 0) continue;
        char when[48]; int wl = 0; char *cmd;
        if (*s == '@') {                              /* @reboot, @daily, ... */
            char *q = s; while (*q && *q != ' ' && *q != '\t') { if (wl < 46) when[wl++] = *q; q++; }
            while (*q == ' ' || *q == '\t') q++; cmd = q;
        } else {
            char *q = s; int field = 0;
            while (*q && field < 5) {
                while (*q == ' ' || *q == '\t') q++;
                while (*q && *q != ' ' && *q != '\t') { if (wl < 46) when[wl++] = *q; q++; }
                if (wl < 46) when[wl++] = ' ';
                field++;
            }
            while (*q == ' ' || *q == '\t') q++; cmd = q;
        }
        when[wl] = 0; rstrip(when); rstrip(cmd);
        snprintf(out[n].when, 48, "%s", when); snprintf(out[n].cmd, 160, "%s", cmd); n++;
    }
    pclose(p); return n;
}
static void cron_rewrite(const char *content) { const char *a[] = { "crontab", "-", NULL }; run_tool(a, content); }
int sys_cron_add(const char *when, const char *cmd) {
    if (!when || !*when || !cmd || !*cmd) return -1;
    static char buf[8192]; int len = cron_read_all(buf, sizeof buf);
    if (len && buf[len-1] != '\n') { buf[len++] = '\n'; buf[len] = 0; }
    int add = snprintf(buf + len, sizeof buf - len, "%s %s\n", when, cmd);
    if (add < 0 || len + add >= (int)sizeof buf) return -1;
    cron_rewrite(buf); return 0;
}
int sys_cron_del(int index) {                         /* drop the index-th data line */
    static char buf[8192]; FILE *p = popen("crontab -l 2>/dev/null", "r"); if (!p) return -1;
    char line[512]; int n = 0, out = 0;
    while (fgets(line, sizeof line, p) && out < (int)sizeof buf - 520) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        int isdata = !(*s == '#' || *s == '\n' || *s == '\r' || *s == 0);
        if (isdata && n++ == index) continue;         /* skip the one being deleted */
        int L = (int)strlen(line); memcpy(buf + out, line, L); out += L;
    }
    buf[out] = 0; pclose(p);
    cron_rewrite(buf); return 0;
}

/* ---- Snipping Tool: capture the whole screen to a PNG in the data dir (scrot). ---- */
int sys_snip(const char *relname) {
    char out[512]; data_path(out, sizeof out, relname, 0);
    const char *a[] = { "scrot", "-o", "-z", out, NULL };   /* -o overwrite, -z fast */
    return run_tool(a, 0);
}

static const char *snd_default(const char *ev) {
    if (!strcmp(ev, "start"))    return "mssound.wav";
    if (!strcmp(ev, "error"))    return "chord.wav";
    if (!strcmp(ev, "warning"))  return "chord.wav";
    if (!strcmp(ev, "question")) return "chord.wav";
    if (!strcmp(ev, "info"))     return "ding.wav";
    if (!strcmp(ev, "beep"))     return "ding.wav";
    if (!strcmp(ev, "exit"))     return "tada.wav";
    return "";
}
void snd_event(const char *ev) {
    char key[48], file[40];
    snprintf(key, sizeof key, "snd_%s", ev);
    if (sys_get_setting(key, file, sizeof file) == 0 && file[0]) {
        if (file[0] != '(') snd_play(file);       /* "(None)" -> silent */
        return;
    }
    const char *d = snd_default(ev); if (d[0]) snd_play(d);
}

/* play a WAV by forking a player (paplay/aplay/ffplay); silent if none exists.
 * The pid is tracked so snd_stop() can halt it (Media Player Stop/Pause). */
#include <signal.h>
static pid_t g_snd_pid = -1;
void snd_stop(void) { if (g_snd_pid > 0) { kill(g_snd_pid, SIGKILL); g_snd_pid = -1; } }
void snd_play(const char *name) {
    if (!name || !*name) return;
    char p[512]; int have = 0;
    data_path(p, sizeof p, name, 0); if (access(p, F_OK) == 0) have = 1;
    else { data_path(p, sizeof p, name, 1); if (access(p, F_OK) == 0) have = 1; }
    if (!have) return;
    pid_t pid = fork();
    if (pid == 0) {
        sandbox_nnp();                                   /* no privilege escalation from a media parser */
        int nul = open("/dev/null", O_WRONLY); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execlp("paplay", "paplay", p, (char *)0);
        execlp("aplay", "aplay", "-q", p, (char *)0);
        execlp("ffplay", "ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", p, (char *)0);
        _exit(127);
    }
    if (pid > 0) g_snd_pid = pid;
}

/* ---- text-to-speech ("Microsoft Sam" / Narrator) via espeak-ng. Pipes the text
 * to espeak on stdin (no shell quoting), runs async, tracks the pid so Stop works. */
static pid_t g_tts_pid = -1;
void sys_speak_stop(void) { if (g_tts_pid > 0) { kill(-g_tts_pid, SIGKILL); g_tts_pid = -1; } }
void sys_speak(const char *text) {
    if (!text || !*text) return;
    sys_speak_stop();
    int pfd[2]; if (pipe(pfd) != 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        dup2(pfd[0], 0); close(pfd[0]); close(pfd[1]);
        int nul = open("/dev/null", O_WRONLY); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execlp("espeak-ng", "espeak-ng", "-v", "en-us", "-s", "160", "-p", "40", (char *)0);  /* robotic, Sam-ish */
        execlp("espeak",    "espeak",    "-v", "en-us", "-s", "160", "-p", "40", (char *)0);
        _exit(127);
    }
    close(pfd[0]);
    if (pid > 0) { g_tts_pid = pid; if (write(pfd[1], text, strlen(text)) < 0) {/*ignore*/} }
    close(pfd[1]);                               /* EOF -> espeak speaks then exits */
}

/* ---- Remote Access: host the Debian95 desktop over VNC (x11vnc) so another
 * machine can view/control it. The boot-time LUKS logon is unchanged; this is an
 * opt-in, password-guarded VNC server on :0. The password is stored ONLY in an
 * x11vnc-obfuscated file under the (encrypted) data dir - never in SETTINGS.INI. */
static void vnc_passfile(char *out, int n) { data_path(out, n, "vnc.pass", 0); }

int sys_vnc_available(void) { return access("/usr/bin/x11vnc", X_OK) == 0 || access("/bin/x11vnc", X_OK) == 0; }

int sys_vnc_has_password(void) { char pf[512]; vnc_passfile(pf, sizeof pf); return access(pf, F_OK) == 0; }

int sys_vnc_running(void) {
    FILE *p = popen("pgrep -x x11vnc 2>/dev/null", "r");
    if (!p) return 0;
    char b[32]; int on = fgets(b, sizeof b, p) != NULL;
    pclose(p); return on;
}

/* this machine's primary LAN address + the VNC port, for the "connect to" line */
void sys_vnc_info(char *ip, int iplen, int *port) {
    if (ip && iplen) {
        ip[0] = 0;
        FILE *p = popen("hostname -I 2>/dev/null", "r");
        if (p) { if (fgets(ip, iplen, p)) { char *sp = strchr(ip, ' '); if (sp) *sp = 0; rstrip(ip); } pclose(p); }
        if (!ip[0]) snprintf(ip, iplen, "(not connected to a network)");
    }
    if (port) *port = 5900;
}

/* set/replace the VNC password (x11vnc -storepasswd writes an obfuscated file) */
int sys_vnc_set_password(const char *pw) {
    if (!pw || !*pw) return -1;
    char pf[512]; vnc_passfile(pf, sizeof pf);
    const char *a[] = { "x11vnc", "-storepasswd", pw, pf, NULL };
    if (run_tool(a, NULL) != 0) return -2;
    chmod(pf, 0600);
    return 0;
}

void sys_vnc_stop(void) { const char *a[] = { "pkill", "-x", "x11vnc", NULL }; run_tool(a, NULL); }

/* start the VNC host using the already-stored password file. Returns 0 on spawn,
 * -1 if x11vnc is missing, -2 if no password has been set yet. Double-forks so
 * the daemon reparents to init (no zombie, never blocks the UI). */
int sys_vnc_start(void) {
    if (!sys_vnc_available()) return -1;
    char pf[512]; vnc_passfile(pf, sizeof pf);
    if (access(pf, F_OK) != 0) return -2;
    sys_vnc_stop();                                  /* restart clean */
    mkdir("/var/log/debian95", 0755);
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int nul = open("/dev/null", O_RDWR);
        int log = open("/var/log/debian95/x11vnc.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (nul >= 0) dup2(nul, 0);
        if (log >= 0) { dup2(log, 1); dup2(log, 2); } else if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        if (fork() == 0) {                           /* grandchild = the long-lived daemon */
            execlp("x11vnc", "x11vnc", "-display", ":0", "-auth", "guess",
                   "-rfbauth", pf, "-rfbport", "5900", "-forever", "-shared",
                   "-noxdamage", "-ncache", "0", "-quiet", (char *)0);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) { int st; waitpid(pid, &st, 0); }   /* reap the middle process */
    return 0;
}

/* ---- Firewall: front-end to ufw (the uncomplicated firewall). The shell runs as
 * root, so ufw's privileged commands work directly. ---- */
int sys_fw_available(void) { return access("/usr/sbin/ufw", X_OK) == 0 || access("/sbin/ufw", X_OK) == 0; }
int sys_fw_status(void) {
    FILE *p = popen("ufw status 2>/dev/null", "r"); if (!p) return 0;
    char b[160]; int on = 0;
    while (fgets(b, sizeof b, p)) if (strstr(b, "Status:")) { on = (strstr(b, "active") && !strstr(b, "inactive")); break; }
    pclose(p); return on;
}
int sys_fw_set(int on) {
    const char *a1[] = { "ufw", "--force", "enable", NULL };   /* --force: no interactive prompt */
    const char *a0[] = { "ufw", "disable", NULL };
    return run_tool(on ? a1 : a0, NULL);
}
int sys_fw_rules(struct lg_fwrule *out, int max) {
    FILE *p = popen("ufw status numbered 2>/dev/null", "r"); if (!p) return 0;
    char b[256]; int n = 0;
    while (fgets(b, sizeof b, p) && n < max) {               /* "[ 1] 22/tcp   ALLOW IN   Anywhere" */
        char *lb = strchr(b, '['); if (!lb) continue;
        char *rb = strchr(lb, ']'); if (!rb) continue;
        int num = atoi(lb + 1); char *t = rb + 1; while (*t == ' ' || *t == '\t') t++;
        rstrip(t); if (!*t) continue;
        out[n].num = num; snprintf(out[n].text, sizeof out[n].text, "%s", t); n++;
    }
    pclose(p); return n;
}
int sys_fw_add(const char *spec) {                            /* spec e.g. "allow 22/tcp" or "deny 23" */
    if (!spec || !*spec) return -1;
    char buf[128]; snprintf(buf, sizeof buf, "%s", spec);
    const char *av[10]; int ac = 0; av[ac++] = "ufw";
    char *t = strtok(buf, " "); while (t && ac < 9) { av[ac++] = t; t = strtok(NULL, " "); } av[ac] = NULL;
    return run_tool(av, NULL);
}
int sys_fw_del(int num) {
    char ns[12]; snprintf(ns, sizeof ns, "%d", num);
    const char *a[] = { "ufw", "--force", "delete", ns, NULL };
    return run_tool(a, NULL);
}

/* ---- System Restore. On the btrfs root (the default) a restore point is a REAL
 * read-only whole-system snapshot (btrfs subvolume snapshot -r /), instant and
 * space-efficient, kept in /.snapshots. Because the user's live settings sit on the
 * separate encrypted /data overlay (not on the btrfs root), we ALSO tar the desktop
 * state beside each snapshot so Restore can safely bring settings back without a
 * risky live rollback. On a plain ext4 root we fall back to a settings tarball. ---- */
#define SNAPDIR "/.snapshots"
static int root_is_btrfs(void) {
    FILE *f = fopen("/proc/mounts", "r"); if (!f) return 0;
    char dev[160], mp[160], ty[48]; int r = 0;
    while (fscanf(f, "%159s %159s %47s %*[^\n]", dev, mp, ty) == 3)
        if (!strcmp(mp, "/") && !strcmp(ty, "btrfs")) { r = 1; break; }
    fclose(f); return r;
}
static void restore_root(char *out, int n) {
    if (access("/data", W_OK) == 0) snprintf(out, n, "/data/restore");
    else data_path(out, n, "restore", 0);
    mkdir(out, 0700);
}
int sys_restore_create(const char *label) {
    char safe[64]; int k = 0;
    for (const char *s = label; s && *s && k < 63; s++) { char c = *s; safe[k++] = (c == ' ' || c == '/' || c == '\t') ? '_' : c; }
    safe[k] = 0; if (!safe[0]) snprintf(safe, sizeof safe, "point");
    time_t t = time(0);
    if (root_is_btrfs()) {
        mkdir(SNAPDIR, 0755);
        char sp[300]; snprintf(sp, sizeof sp, "%s/%ld_%s", SNAPDIR, (long)t, safe);
        const char *a[] = { "btrfs", "subvolume", "snapshot", "-r", "/", sp, NULL };
        int r = run_tool(a, NULL);
        /* desktop/settings state lives on the /data overlay, not the btrfs root -
         * capture it beside the snapshot so Restore can bring it back safely. */
        char tgz[320]; snprintf(tgz, sizeof tgz, "%s.settings.tgz", sp);
        const char *b[] = { "tar", "czf", tgz, "-C", data_dir(), ".", NULL }; run_tool(b, NULL);
        return r;
    }
    char rr[256]; restore_root(rr, sizeof rr);
    char path[512]; snprintf(path, sizeof path, "%s/%ld_%s.tgz", rr, (long)t, safe);
    const char *a[] = { "tar", "czf", path, "-C", data_dir(), ".", NULL };
    return run_tool(a, NULL);
}
static void restore_label(const char *nm, struct lg_restore *o) {
    long ep = atol(nm);
    const char *u = strchr(nm, '_');
    char lbl[64]; snprintf(lbl, sizeof lbl, "%s", u ? u + 1 : nm);
    char *dot = strstr(lbl, ".tgz"); if (dot) *dot = 0;
    snprintf(o->label, sizeof o->label, "%s", lbl[0] ? lbl : "Restore point");
    time_t tt = (time_t)ep; struct tm *tmv = localtime(&tt);
    if (tmv) strftime(o->when, sizeof o->when, "%Y-%m-%d %H:%M", tmv); else o->when[0] = 0;
}
int sys_restore_list(struct lg_restore *out, int max) {
    int btrfs = root_is_btrfs();
    char rr[256]; if (btrfs) snprintf(rr, sizeof rr, "%s", SNAPDIR); else restore_root(rr, sizeof rr);
    DIR *d = opendir(rr); if (!d) return 0;
    struct dirent *e; int n = 0;
    while ((e = readdir(d)) && n < max) {
        const char *nm = e->d_name; size_t L = strlen(nm);
        if (btrfs) {                                          /* snapshot subvols: "<epoch>_<label>" dirs */
            if (nm[0] == '.' || (L > 4 && strcmp(nm + L - 4, ".tgz") == 0)) continue;
            if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN) continue;
        } else {
            if (L < 5 || strcmp(nm + L - 4, ".tgz") != 0) continue;
        }
        snprintf(out[n].id, sizeof out[n].id, "%s", nm);
        restore_label(nm, &out[n]);
        n++;
    }
    closedir(d); return n;
}
int sys_restore_apply(const char *id) {
    if (!id || !*id || strstr(id, "..") || strchr(id, '/')) return -1;
    if (root_is_btrfs()) {                                    /* restore the settings captured beside the snapshot */
        char tgz[320]; snprintf(tgz, sizeof tgz, "%s/%s.settings.tgz", SNAPDIR, id);
        if (access(tgz, R_OK) != 0) return -1;
        const char *a[] = { "tar", "xzf", tgz, "-C", data_dir(), NULL };
        return run_tool(a, NULL);
    }
    char rr[256]; restore_root(rr, sizeof rr);
    char path[512]; snprintf(path, sizeof path, "%s/%s", rr, id);
    if (access(path, R_OK) != 0) return -1;
    const char *a[] = { "tar", "xzf", path, "-C", data_dir(), NULL };
    return run_tool(a, NULL);
}
int sys_restore_del(const char *id) {
    if (!id || !*id || strstr(id, "..") || strchr(id, '/')) return -1;
    if (root_is_btrfs()) {
        char sp[300]; snprintf(sp, sizeof sp, "%s/%s", SNAPDIR, id);
        const char *a[] = { "btrfs", "subvolume", "delete", sp, NULL }; run_tool(a, NULL);
        char tgz[320]; snprintf(tgz, sizeof tgz, "%s/%s.settings.tgz", SNAPDIR, id); unlink(tgz);
        return 0;
    }
    char rr[256]; restore_root(rr, sizeof rr);
    char path[512]; snprintf(path, sizeof path, "%s/%s", rr, id);
    return unlink(path) == 0 ? 0 : -1;
}

/* ---- live system stats for the desktop gadget sidebar (CPU% / memory / net) ---- */
int sys_sysmon(struct lg_sysmon *out) {
    out->cpu_pct = 0; out->mem_pct = 0; out->mem_total_mb = 0; out->mem_used_mb = 0; out->net_rx_kbs = 0; out->net_tx_kbs = 0;
    /* CPU: delta of busy vs total jiffies from /proc/stat */
    { FILE *f = fopen("/proc/stat", "r");
      if (f) { char lbl[8]; unsigned long long u=0,ni=0,sy=0,id=0,io=0,ir=0,so=0;
          if (fscanf(f, "%7s %llu %llu %llu %llu %llu %llu %llu", lbl, &u,&ni,&sy,&id,&io,&ir,&so) >= 5) {
              unsigned long long idle = id + io, total = u+ni+sy+id+io+ir+so;
              static unsigned long long pidle, ptotal;
              unsigned long long dt = total - ptotal, di = idle - pidle;
              if (ptotal && dt) { int p = (int)((dt - di) * 100 / dt); out->cpu_pct = p < 0 ? 0 : p > 100 ? 100 : p; }
              pidle = idle; ptotal = total;
          }
          fclose(f);
      } }
    /* memory from /proc/meminfo */
    { FILE *f = fopen("/proc/meminfo", "r");
      if (f) { char k[32]; unsigned long v; char u[8]; unsigned long tot=0, avail=0;
          while (fscanf(f, "%31s %lu %7s", k, &v, u) >= 2) {
              if (!strcmp(k, "MemTotal:")) tot = v; else if (!strcmp(k, "MemAvailable:")) { avail = v; break; }
          }
          fclose(f);
          if (tot) { out->mem_total_mb = tot/1024; out->mem_used_mb = (tot-avail)/1024; out->mem_pct = (int)((tot-avail)*100/tot); }
      } }
    /* network: delta rx/tx bytes across non-lo interfaces from /proc/net/dev */
    { FILE *f = fopen("/proc/net/dev", "r");
      if (f) { char line[256]; unsigned long long rx=0, tx=0;
          while (fgets(line, sizeof line, f)) {
              char *c = strchr(line, ':'); if (!c) continue;
              char *ifn = line; while (*ifn==' ') ifn++; if (!strncmp(ifn, "lo:", 3)) continue;
              unsigned long long r=0, t=0; unsigned long long d;
              /* after ':' : rx_bytes rx_packets ... (field 1) ; tx_bytes is field 9 */
              int n = sscanf(c+1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &r,&d,&d,&d,&d,&d,&d,&d,&t);
              if (n >= 9) { rx += r; tx += t; }
          }
          fclose(f);
          static unsigned long long prx, ptx; static long pms;
          long now = (long)(time(0));
          if (pms) { long dsec = now - pms; if (dsec < 1) dsec = 1;
              out->net_rx_kbs = (rx > prx) ? (rx - prx)/1024/dsec : 0;
              out->net_tx_kbs = (tx > ptx) ? (tx - ptx)/1024/dsec : 0; }
          prx = rx; ptx = tx; pms = now;
      } }
    return 0;
}

/* ---- AI Clippy: a real local LLM (llama.cpp + a bundled instruct model). One-
 * shot ChatML generation, run async; the UI polls sys_ai_reply. ---- */
#define AI_SYS "You are Clippy, the cheerful Debian95 assistant. Be friendly, upbeat and brief - answer in one or two short sentences."
int sys_ai_available(void) {
    return access("/usr/share/debian95/ai/model.gguf", F_OK) == 0 && access("/usr/local/bin/llama-cli", X_OK) == 0;
}
int sys_ai_ask(const char *msg) {
    if (!msg || !*msg) return -1;
    FILE *f = fopen("/tmp/clippy.prompt", "w"); if (!f) return -1;
    fprintf(f, "<|im_start|>system\n%s<|im_end|>\n<|im_start|>user\n%s<|im_end|>\n<|im_start|>assistant\n", AI_SYS, msg);
    fclose(f);
    unlink("/tmp/clippy.done"); unlink("/tmp/clippy.out");
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int nul = open("/dev/null", O_WRONLY); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        execlp("sh", "sh", "-c", "/usr/local/bin/debian95-ai /tmp/clippy.prompt /tmp/clippy.out; touch /tmp/clippy.done", (char *)0);
        _exit(127);
    }
    return pid > 0 ? 0 : -1;
}
int sys_ai_reply(char *out, int n) {           /* 1 = done (out filled), 0 = still generating */
    out[0] = 0;
    if (access("/tmp/clippy.done", F_OK) != 0) return 0;
    FILE *f = fopen("/tmp/clippy.out", "r");
    if (f) { int len = (int)fread(out, 1, n - 1, f); if (len < 0) len = 0; out[len] = 0; fclose(f); }
    return 1;
}

/* ---- clock (real wall time) ---- */
void rtc_time(int *hh, int *mm) {
    time_t t = time(0); struct tm *lt = localtime(&t);
    if (hh) *hh = lt ? lt->tm_hour : 12;
    if (mm) *mm = lt ? lt->tm_min : 0;
}
/* real system date/time (Date/Time applet reads the actual clock) */
void sys_date(int *y, int *mo, int *d, int *dow, int *hh, int *mm, int *ss) {
    time_t t = time(0); struct tm *lt = localtime(&t);
    if (!lt) return;
    if (y) *y = lt->tm_year + 1900; if (mo) *mo = lt->tm_mon + 1; if (d) *d = lt->tm_mday;
    if (dow) *dow = lt->tm_wday; if (hh) *hh = lt->tm_hour; if (mm) *mm = lt->tm_min; if (ss) *ss = lt->tm_sec;
}
/* set the real system clock (Date/Time Apply); 0 on success (needs root) */
int sys_set_datetime(int y, int mo, int d, int hh, int mm, int ss) {
    struct tm tmv; time_t t0 = time(0); struct tm *lt = localtime(&t0);
    tmv = lt ? *lt : (struct tm){0};
    tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
    tmv.tm_hour = hh; tmv.tm_min = mm; tmv.tm_sec = ss; tmv.tm_isdst = -1;
    time_t nt = mktime(&tmv); if (nt == (time_t)-1) return -1;
    struct timeval tv = { nt, 0 };
    return settimeofday(&tv, 0);
}

/* ---- Internet time / NTP (Date-Time applet) via timedatectl ----
 * Report and toggle systemd's NTP client. On a host without timedatectl the
 * status reads "unavailable" and toggling returns -1 (honest, no crash). */
int sys_time_ntp(int *enabled, int *synced) {
    if (enabled) *enabled = 0; if (synced) *synced = 0;
    FILE *p = popen("timedatectl show -p NTP -p NTPSynchronized 2>/dev/null", "r");
    if (!p) return -1;
    char line[64]; int got = 0;
    while (fgets(line, sizeof line, p)) {
        if (!strncmp(line, "NTP=", 4)) { if (enabled) *enabled = (strstr(line, "yes") != 0); got = 1; }
        else if (!strncmp(line, "NTPSynchronized=", 16)) { if (synced) *synced = (strstr(line, "yes") != 0); got = 1; }
    }
    pclose(p); return got ? 0 : -1;
}
int sys_time_set_ntp(int on) {
    const char *av[] = { "timedatectl", "set-ntp", on ? "true" : "false", NULL };
    return run_tool(av, NULL);
}

/* enumerate real hardware for the System applet's Device Manager. Returns the
 * number of lines written (each a device-tree row, "  " = indented child). */
int sys_list_devices(char out[][64], int max) {
    int n = 0;
    #define DEVADD(str) do { if (n < max) { int _i = 0; const char *_s = (str); for (; _s[_i] && _i < 63; _i++) out[n][_i] = _s[_i]; out[n][_i] = 0; n++; } } while (0)
    DEVADD("Computer");
    DIR *d = opendir("/sys/block");
    if (d) { struct dirent *e; while ((e = readdir(d)) && n < max) { if (e->d_name[0] == '.') continue;
        char l[64]; snprintf(l, sizeof l, "  Disk drive - %s", e->d_name); DEVADD(l); } closedir(d); }
    d = opendir("/sys/class/net");
    if (d) { struct dirent *e; while ((e = readdir(d)) && n < max) { if (e->d_name[0] == '.') continue;
        char l[64]; snprintf(l, sizeof l, "  Network adapter - %s", e->d_name); DEVADD(l); } closedir(d); }
    if (access("/dev/fb0", F_OK) == 0) DEVADD("  Display adapter - virtio-gpu (fbdev)");
    if (access("/dev/snd", F_OK) == 0 || access("/proc/asound", F_OK) == 0) DEVADD("  Sound - AC97 (snd-intel8x0)");
    d = opendir("/dev/input");
    if (d) { struct dirent *e; int kb = 0, ms = 0; while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue; if (!kb) kb = 1; }
        closedir(d); if (kb) { DEVADD("  Keyboard - virtio / PS2"); DEVADD("  Mouse - virtio-tablet / PS2"); (void)ms; } }
    if (access("/proc/debian95/version", F_OK) == 0) DEVADD("  System - Debian95 native integration (debian95.ko)");
    #undef DEVADD
    return n;
}

/* read a small text file (e.g. /proc/debian95/version) into out; 0 on success */
int sys_read_file(const char *path, char *out, int n) {
    out[0] = 0;
    FILE *f = fopen(path, "r"); if (!f) return -1;
    size_t got = fread(out, 1, n - 1, f); fclose(f);
    out[got] = 0;
    for (int i = 0; out[i]; i++) if (out[i] == '\n') out[i] = ' ';   /* single line */
    return 0;
}

/* ---- system info (Control Panel) ---- */
const char *hal_arch(void) {
    static char s[96];
    if (!s[0]) {
        struct utsname u;
        const char *pretty = "Unknown";
        if (uname(&u) == 0) {
            pretty = u.machine;
            if      (!strcmp(u.machine, "x86_64"))  pretty = "x86-64 (AMD64)";
            else if (!strcmp(u.machine, "aarch64")) pretty = "AArch64 (ARM64)";
            else if (!strcmp(u.machine, "i686") || !strcmp(u.machine, "i386")) pretty = "x86 (32-bit)";
        }
        snprintf(s, sizeof s, "%s - Debian95 Linux", pretty);
    }
    return s;
}
unsigned hal_ram_mb(void) { struct sysinfo si; return sysinfo(&si) == 0 ? (unsigned)(si.totalram / (1024 * 1024)) : 0; }
u64 pmm_total_frames(void) { struct sysinfo si; return sysinfo(&si) == 0 ? si.totalram / 4096 : 0; }
u64 pmm_free_frames(void)  { struct sysinfo si; return sysinfo(&si) == 0 ? si.freeram / 4096 : 0; }
unsigned sys_uptime_s(void)  { struct sysinfo si; return sysinfo(&si) == 0 ? (unsigned)si.uptime : 0; }
unsigned sys_uptime_ms(void) { return sys_uptime_s() * 1000; }

/* ---- process spawn (a Start-menu "Run" etc.); fork/exec a real program ---- */
int ps_spawn(const char *name) {
    if (!name || !*name) return -1;
    pid_t pid = fork();
    if (pid == 0) { sandbox_nnp(); execlp(name, name, (char *)0); _exit(127); }
    return pid > 0 ? (int)pid : -1;
}
int pe_run(const char *name) { (void)name; return -1; }   /* Windows PE: n/a here */

/* set the machine's hostname (Welcome setup). Sanitizes to [A-Za-z0-9-], persists
 * via hostnamectl (also sets it live), with sethostname() as a runtime fallback. */
int sys_set_hostname(const char *name) {
    if (!name || !name[0]) return -1;
    char s[64]; int k = 0;
    for (int i = 0; name[i] && k < 63; i++) {
        char c = name[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-') s[k++] = c;
    }
    s[k] = 0; if (!s[0]) return -1;
    pid_t pid = fork();
    if (pid == 0) { setsid(); execlp("hostnamectl", "hostnamectl", "set-hostname", s, (char *)0); _exit(0); }
    sethostname(s, strlen(s));           /* also apply live in-process (best effort) */
    return 0;
}

/* Launch a downloaded GUI app (VLC, Chromium) from a shell icon / Programs entry.
 * The desktop runs as root (its Control-Panel features need it), but such apps
 * refuse to run as root - so we drop to the 'debian95' user with a private writable
 * HOME + XDG_RUNTIME_DIR and the X display. Double-fork so the grandchild reparents
 * to init (no zombie, and we never block the UI). Returns 0 once spawned. */
int sys_launch_app(const char *cmd) {
    if (!cmd || !*cmd) return -1;
    { void wm_set_busy(int); wm_set_busy(1); }    /* hourglass while the external app starts up */
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) { waitpid(pid, 0, 0); return 0; }        /* reap the middle child */
    pid_t g = fork();
    if (g > 0) _exit(0);                                  /* detach: grandchild -> init */
    setsid();
    struct passwd *pw = getpwnam("debian95");
    const char *home = "/tmp/debian95-home";
    if (pw && geteuid() == 0) {                           /* drop root -> debian95 */
        if (mkdir(home, 0700) != 0) { /* exists is fine */ }
        if (chown(home, pw->pw_uid, pw->pw_gid) != 0) {}
        if (setgid(pw->pw_gid) != 0) _exit(126);
        if (initgroups("debian95", pw->pw_gid) != 0) {}
        if (setuid(pw->pw_uid) != 0) _exit(126);
    }
    setenv("HOME", home, 1);
    setenv("XDG_RUNTIME_DIR", home, 1);   /* PipeWire's socket lives here */
    setenv("DISPLAY", ":0", 1);
    execlp("/bin/sh", "sh", "-c", cmd, (char *)0);
    _exit(127);
}

/* file associations: open a file the shell's file manager double-clicked in the
 * matching downloaded app - VLC (media), Image Viewer (images), Chromium (html/
 * pdf) - if that app is installed. Returns 1 if launched, 0 to let the shell fall
 * back to its own built-in viewer/editor. The file must be readable by the
 * 'debian95' user (world-readable paths; the data dir and mounts qualify). */
static int fa_ext(const char *p, const char *e) {
    const char *dot = 0; for (const char *s = p; *s; s++) if (*s == '.') dot = s;
    if (!dot) return 0; dot++;
    for (int i = 0;; i++) {
        char a = dot[i], b = e[i];
        if (b == 0) return a == 0;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != b) return 0;
    }
}
static int fa_any(const char *p, const char *const *exts) {
    for (int i = 0; exts[i]; i++) if (fa_ext(p, exts[i])) return 1;
    return 0;
}
int sys_open_file(const char *path) {
    if (!path || !*path) return 0;
    static const char *VIDEO[] = { "mp4","mkv","avi","webm","mov","mpg","mpeg","m4v","flv","wmv",
                                   "3gp","mp3","wav","flac","ogg","m4a","aac","opus","wma", 0 };
    static const char *IMAGE[] = { "png","jpg","jpeg","gif","webp","bmp","tiff","tif","ico","svg", 0 };
    static const char *WEB[]   = { "html","htm","pdf","xml", 0 };
    static const char *ARCHIVE[] = { "zip","tar","gz","tgz","bz2","xz","7z","rar","tbz2","tar.gz", 0 };
    static const char *DOS[]   = { "com","bat","exe", 0 };   /* DOS programs -> DOSBox */
    const char *pre = 0;
    if      (fa_any(path, DOS) && access("/usr/bin/dosbox", X_OK) == 0)
        pre = "dosbox ";
    else if (fa_any(path, VIDEO) && access("/usr/bin/vlc", X_OK) == 0)
        pre = "vlc --no-qt-privacy-ask --intf qt --vout=x11 ";
    else if (fa_any(path, IMAGE) && access("/usr/bin/gpicview", X_OK) == 0)
        pre = "gpicview ";
    else if (fa_any(path, ARCHIVE) && access("/usr/bin/xarchiver", X_OK) == 0)
        pre = "xarchiver ";
    else if (fa_any(path, WEB) && access("/usr/bin/chromium", X_OK) == 0)
        pre = "chromium --no-sandbox --disable-gpu --disable-dev-shm-usage --user-data-dir=/tmp/debian95-home/cr ";
    if (!pre) return 0;
    /* <app> [flags] '<path>' - single-quote the path, escaping embedded quotes */
    char cmd[640]; int n = 0;
    for (const char *s = pre; *s && n < 500; s++) cmd[n++] = *s;
    cmd[n++] = '\'';
    for (const char *s = path; *s && n < 620; s++) {
        if (*s == '\'') { cmd[n++]='\''; cmd[n++]='\\'; cmd[n++]='\''; cmd[n++]='\''; }
        else cmd[n++] = *s;
    }
    cmd[n++] = '\''; cmd[n] = 0;
    sys_launch_app(cmd);
    return 1;
}

/* enumerate installed GUI apps from freedesktop .desktop files, so anything the
 * user apt-installs shows up in Start > Programs automatically. */
struct lg_app { char name[64]; char exec[192]; char icon[64]; int cat; };
/* freedesktop Categories= -> Debian95 Start-menu group (0 Internet, 1 Multimedia,
 * 2 Graphics, 3 Office, -1 flat). First match wins, in that priority order. */
static int desktop_category(const char *cats) {
    if (!cats) return -1;
    if (strstr(cats, "Network") || strstr(cats, "WebBrowser") || strstr(cats, "Email") || strstr(cats, "Chat")) return 0;
    if (strstr(cats, "AudioVideo") || strstr(cats, "Audio") || strstr(cats, "Video") || strstr(cats, "Player") || strstr(cats, "Music")) return 1;
    if (strstr(cats, "Graphics") || strstr(cats, "Photography") || strstr(cats, "Viewer")) return 2;
    if (strstr(cats, "Office") || strstr(cats, "WordProcessor") || strstr(cats, "Spreadsheet") || strstr(cats, "Presentation")) return 3;
    return -1;
}
static void desktop_strip_exec(const char *in, char *out, int n) {
    int k = 0;
    for (const char *s = in; *s && k < n - 1; s++) {
        if (*s == '%' && s[1]) { s++; continue; }   /* drop field codes %U %f %i ... */
        if (*s == '"') continue;
        out[k++] = *s;
    }
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}
int sys_list_apps(struct lg_app *out, int max) {
    DIR *d = opendir("/usr/share/applications"); if (!d) return 0;
    struct dirent *e; int n = 0;
    while (n < max && (e = readdir(d))) {
        int L = (int)strlen(e->d_name);
        if (L < 9 || strcmp(e->d_name + L - 8, ".desktop")) continue;
        char path[320]; snprintf(path, sizeof path, "/usr/share/applications/%s", e->d_name);
        FILE *f = fopen(path, "r"); if (!f) continue;
        char name[64] = "", exec[192] = "", icon[64] = "", cats[256] = "", line[512];
        int in_entry = 0, is_app = 1, hide = 0, terminal = 0, have_name = 0;
        while (fgets(line, sizeof line, f)) {
            int i = 0; while (line[i] && line[i] != '\n' && line[i] != '\r') i++; line[i] = 0;
            if (line[0] == '[') { in_entry = (strcmp(line, "[Desktop Entry]") == 0); continue; }
            if (!in_entry) continue;
            if      (!strncmp(line, "Type=", 5))      is_app  = (strcmp(line + 5, "Application") == 0);
            else if (!strncmp(line, "NoDisplay=", 10)) hide   |= (strcmp(line + 10, "true") == 0);
            else if (!strncmp(line, "Hidden=", 7))     hide   |= (strcmp(line + 7, "true") == 0);
            else if (!strncmp(line, "Terminal=", 9))   terminal = (strcmp(line + 9, "true") == 0);
            else if (!have_name && !strncmp(line, "Name=", 5)) { int j = 0; for (; line[5+j] && j < 63; j++) name[j] = line[5+j]; name[j] = 0; have_name = 1; }
            else if (!exec[0] && !strncmp(line, "Exec=", 5)) desktop_strip_exec(line + 5, exec, sizeof exec);
            else if (!icon[0] && !strncmp(line, "Icon=", 5)) { int j = 0; for (; line[5+j] && j < 63; j++) icon[j] = line[5+j]; icon[j] = 0; }
            else if (!cats[0] && !strncmp(line, "Categories=", 11)) { int j = 0; for (; line[11+j] && j < 255; j++) cats[j] = line[11+j]; cats[j] = 0; }
        }
        fclose(f);
        if (is_app && !hide && !terminal && name[0] && exec[0]) {
            snprintf(out[n].name, sizeof out[n].name, "%s", name);
            snprintf(out[n].exec, sizeof out[n].exec, "%s", exec);
            snprintf(out[n].icon, sizeof out[n].icon, "%s", icon);
            out[n].cat = desktop_category(cats);
            n++;
        }
    }
    closedir(d); return n;
}

/* ---- network: minimal stubs (real sockets wired later) ---- */
int  net_init(void) { return -1; }               /* "not configured" for now */
void net_poll(void) {}
int  net_ping(u32 ip) { (void)ip; return -1; }
int  net_dhcp(void) { return -1; }
int  net_dns(const char *host, u32 *out) { (void)host; (void)out; return -1; }
int  net_http_get(const char *host, const char *path, char *out, int outmax) {
    (void)host; (void)path; if (out && outmax > 0) out[0] = 0; return -1;
}
void net_listen(u16 port, const char *html) { (void)port; (void)html; }
void net_unlisten(void) {}
void net_status(char *line, int n) { if (line && n > 0) snprintf(line, n, "Network: not configured"); }
void net_ip_to_str(u32 ip, char *out) { if (out) snprintf(out, 16, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF); }
u32  net_gateway_ip(void) { return 0; }
u32  net_dns_ip(void) { return 0; }
void net_firewall_set(int on) { (void)on; }
int  net_firewall_on(void) { return 1; }
u32  net_firewall_drops(void) { return 0; }
/* ---- sandboxing (defense-in-depth for spawned children) ---- */
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#ifdef __has_include
#  if __has_include(<linux/landlock.h>)
#    include <linux/landlock.h>
#    define HAVE_LANDLOCK 1
#  endif
#endif
void sandbox_nnp(void) { prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0); }   /* no privilege escalation */
/* Best-effort Landlock: deny ALL filesystem writes/creates/removes for this
 * process; reads + execute stay allowed (so glibc DNS still reads /etc + NSS).
 * Fails open where Landlock is unavailable (e.g. the WSL kernel). Requires
 * NO_NEW_PRIVS first. Used for the network-fetch helper (untrusted input). */
static void sandbox_readonly_fs(void) {
#ifdef HAVE_LANDLOCK
    long abi = syscall(SYS_landlock_create_ruleset, (void *)0, (size_t)0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 1) return;                                  /* not supported -> no restriction */
    unsigned long long w = LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR |
        LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
        LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO |
        LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM;
#ifdef LANDLOCK_ACCESS_FS_REFER
    if (abi >= 2) w |= LANDLOCK_ACCESS_FS_REFER;
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    if (abi >= 3) w |= LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
    struct landlock_ruleset_attr attr; memset(&attr, 0, sizeof attr); attr.handled_access_fs = w;
    int fd = syscall(SYS_landlock_create_ruleset, &attr, sizeof attr, 0);
    if (fd < 0) return;                                  /* no allow rules -> every write denied */
    syscall(SYS_landlock_restrict_self, fd, 0);
    close(fd);
#endif
}

/* Real HTTP/1.0 GET over Linux sockets. hostport = "host[:port]", path begins
 * with '/'. Returns the raw response (headers + body) in out; the browser strips
 * the headers. No TLS, so it browses plain-HTTP sites (and the QEMU user-net
 * 10.0.2.x host). Returns bytes read, or -1 on failure. */
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
static int http_fetch(const char *hostport, const char *path, char *out, int max) {
    if (!out || max <= 0) return -1; out[0] = 0;
    char host[128]; int port = 80, i = 0;
    for (; hostport[i] && hostport[i] != ':' && i < 127; i++) host[i] = hostport[i]; host[i] = 0;
    if (hostport[i] == ':') { port = 0; for (int k = i + 1; hostport[k] >= '0' && hostport[k] <= '9'; k++) port = port * 10 + (hostport[k] - '0'); }
    char ports[8]; snprintf(ports, sizeof ports, "%d", port ? port : 80);
    struct addrinfo hints, *res = 0; memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, ports, &hints, &res) != 0 || !res) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    struct timeval tv; tv.tv_sec = 8; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) { close(fd); freeaddrinfo(res); return -1; }
    freeaddrinfo(res);
    char req[400];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Debian95/1.0\r\nAccept: text/html\r\nConnection: close\r\n\r\n",
        (path && path[0]) ? path : "/", host);
    if (send(fd, req, rl, 0) != rl) { close(fd); return -1; }
    int n = 0, r;
    while (n < max - 1 && (r = recv(fd, out + n, max - 1 - n, 0)) > 0) n += r;
    close(fd); out[n] = 0;
    return n > 0 ? n : -1;
}
/* Public fetch: runs http_fetch in a SANDBOXED forked helper (own address space,
 * no-new-privs, read-only filesystem) so a bug in the network/HTTP path can't
 * corrupt the desktop, escalate privilege, or write to disk. Result piped back. */
int www_get(const char *hostport, const char *path, char *out, int max) {
    if (!out || max <= 0) return -1; out[0] = 0;
    int pfd[2]; if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {                                       /* sandboxed child */
        close(pfd[0]);
        sandbox_nnp(); sandbox_readonly_fs();
        char *b = (char *)malloc(max); int m = b ? http_fetch(hostport, path, b, max) : -1;
        for (int off = 0; b && m > 0 && off < m; ) { int w = write(pfd[1], b + off, m - off); if (w <= 0) break; off += w; }
        _exit(0);
    }
    close(pfd[1]);
    int n = 0, r;
    if (pid > 0) while (n < max - 1 && (r = read(pfd[0], out + n, max - 1 - n)) > 0) n += r;
    close(pfd[0]); if (pid > 0) waitpid(pid, 0, 0); out[n] = 0;
    return n > 0 ? n : -1;
}

/* ---- power: real Linux shutdown/reboot ---- */
#include <sys/reboot.h>
#include <linux/reboot.h>
void sys_shutdown(int restart) {
    sync();
    /* clean shutdown through systemd (stops services, unmounts the USB safely) */
    pid_t pid = fork();
    if (pid == 0) {
        const char *act = restart ? "reboot" : "poweroff";
        execlp("systemctl", "systemctl", act, (char *)0);
        execlp(act, act, (char *)0);          /* sysvinit fallback: /sbin/reboot|poweroff */
        _exit(127);
    }
    if (pid > 0) sleep(3);                     /* let systemd begin tearing down */
    /* last resort (no systemd / it failed): the raw syscall */
    reboot(restart ? LINUX_REBOOT_CMD_RESTART : LINUX_REBOOT_CMD_POWER_OFF);
}
/* Stand By: real suspend-to-RAM via systemd (pm-suspend fallback). */
void sys_suspend(void) {
    sync();
    pid_t pid = fork();
    if (pid == 0) {
        execlp("systemctl", "systemctl", "suspend", (char *)0);
        execlp("pm-suspend", "pm-suspend", (char *)0);
        _exit(127);
    }
}
/* battery: 0 + fills pct/charging when a battery exists; -1 on a desktop/VM. */
int sys_battery(int *pct, int *charging) {
    { const char *fk = getenv("DEBIAN95_FAKEBAT"); if (fk && *fk) { if (pct) *pct = atoi(fk); if (charging) *charging = 0; return 0; } }  /* test */
    DIR *d = opendir("/sys/class/power_supply"); if (!d) return -1;
    struct dirent *e; char base[128] = "";
    while ((e = readdir(d))) if (!strncmp(e->d_name, "BAT", 3)) {
        snprintf(base, sizeof base, "/sys/class/power_supply/%s", e->d_name); break; }
    closedir(d);
    if (!base[0]) return -1;
    char p[192], st[32] = ""; FILE *f; int cap = -1;
    snprintf(p, sizeof p, "%s/capacity", base); f = fopen(p, "r"); if (f) { if (fscanf(f, "%d", &cap) != 1) cap = -1; fclose(f); }
    snprintf(p, sizeof p, "%s/status", base);   f = fopen(p, "r"); if (f) { if (!fgets(st, sizeof st, f)) st[0] = 0; fclose(f); }
    if (cap < 0) return -1;
    if (pct) *pct = cap;
    if (charging) *charging = (strncmp(st, "Charging", 8) == 0 || strncmp(st, "Full", 4) == 0) ? 1 : 0;
    return 0;
}
