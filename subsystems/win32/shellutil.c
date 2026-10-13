/* Debian95 — shared shell formatting helpers. */
#include "shellutil.h"

static void u2a(u32 v, char *b) { char t[12]; int i = 0;
    if (!v) { b[0] = '0'; b[1] = 0; return; }
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; } int j = 0; while (i) b[j++] = t[--i]; b[j] = 0; }
static void cat(char *d, const char *s) { int l = 0; while (d[l]) l++; for (int i = 0; s[i]; i++) d[l++] = s[i]; d[l] = 0; }

void shell_size(u32 bytes, char *out) {
    u32 kb = (bytes + 1023) / 1024; if (!kb) kb = 1;
    char n[16]; out[0] = 0;
    if (kb < 1024) { u2a(kb, n); cat(out, n); cat(out, " KB"); return; }
    u32 num, den; const char *suf;
    if      (kb < 1024u * 1024)        { num = kb;              den = 1024; suf = " MB"; }
    else if (kb < 1024u * 1024 * 1024) { num = kb / 1024;       den = 1024; suf = " GB"; }
    else                               { num = kb / 1024 / 1024; den = 1024; suf = " TB"; }
    u32 whole = num / den, tenth = (num % den) * 10 / den;
    u2a(whole, n); cat(out, n); cat(out, ".");
    n[0] = (char)('0' + tenth); n[1] = 0; cat(out, n); cat(out, suf);
}

void shell_date(u32 epoch, char *out) {
    out[0] = 0;
    if (!epoch) return;
    u32 days = epoch / 86400, secs = epoch % 86400;
    int hh = (int)(secs / 3600), mm = (int)((secs % 3600) / 60);
    /* civil date from days since 1970-01-01 (Howard Hinnant's algorithm) */
    long z = (long)days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    int yy = (int)(y % 100);
    int pm = hh >= 12, h12 = hh % 12; if (h12 == 0) h12 = 12;
    char n[8];
    u2a(m, n); cat(out, n); cat(out, "/");
    u2a(d, n); cat(out, n); cat(out, "/");
    if (yy < 10) cat(out, "0"); u2a((u32)yy, n); cat(out, n); cat(out, " ");
    u2a((u32)h12, n); cat(out, n); cat(out, ":");
    if (mm < 10) cat(out, "0"); u2a((u32)mm, n); cat(out, n);
    cat(out, pm ? " PM" : " AM");
}

static int ends_ci(const char *n, const char *e) {
    int a = 0, b = 0; while (n[a]) a++; while (e[b]) b++; if (b > a) return 0;
    for (int i = 0; i < b; i++) { char x = n[a-b+i], y = e[i]; if (x>='A'&&x<='Z')x+=32; if (y>='A'&&y<='Z')y+=32; if (x != y) return 0; }
    return 1;
}
const char *shell_type(const char *name, int is_dir) {
    if (is_dir) return "File Folder";
    if (ends_ci(name, ".txt") || ends_ci(name, ".log") || ends_ci(name, ".ini") || ends_ci(name, ".me")) return "Text Document";
    if (ends_ci(name, ".bmp")) return "Bitmap Image";
    if (ends_ci(name, ".png") || ends_ci(name, ".jpg") || ends_ci(name, ".gif") || ends_ci(name, ".ico") || ends_ci(name, ".lgb")) return "Image";
    if (ends_ci(name, ".wav") || ends_ci(name, ".mid")) return "Sound";
    if (ends_ci(name, ".mpg") || ends_ci(name, ".avi")) return "Video Clip";
    if (ends_ci(name, ".exe") || ends_ci(name, ".com") || ends_ci(name, ".elf")) return "Application";
    if (ends_ci(name, ".dll") || ends_ci(name, ".cpl")) return "Application Extension";
    if (ends_ci(name, ".lnk")) return "Shortcut";
    return "File";
}
