/* Debian95 — PNG decoder (RFC 2083) over the DEFLATE inflater.
 *
 * Supports the common cases: bit depth 8 (and 16, high byte), colour types
 * 0/2/3/4/6 (gray, RGB, palette, gray+alpha, RGBA) and sub-byte (1/2/4) palette
 * images. No interlacing (Adam7). Alpha is dropped to opaque RGB for the blit.
 */
#include <debian95/types.h>
#include <debian95/mm.h>

int inflate_zlib(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *written);

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

int png_decode(const u8 *data, u32 len, u32 **out, int *ow, int *oh) {
    static const u8 SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (len < 8) return -1;
    for (int i = 0; i < 8; i++) if (data[i] != SIG[i]) return -1;

    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    u8 plte[256 * 3]; int nplte = 0;
    u8 *idat = 0; u32 idatn = 0, idatcap = 0;

    u32 p = 8;
    while (p + 12 <= len) {
        u32 clen = be32(data + p);
        const u8 *type = data + p + 4;
        const u8 *cd = data + p + 8;
        if (p + 12 + clen > len) break;
        if (type[0]=='I'&&type[1]=='H'&&type[2]=='D'&&type[3]=='R') {
            if (clen < 13) return -1;
            w = (int)be32(cd); h = (int)be32(cd + 4);
            depth = cd[8]; ctype = cd[9]; interlace = cd[12];
        } else if (type[0]=='P'&&type[1]=='L'&&type[2]=='T'&&type[3]=='E') {
            nplte = (int)(clen / 3); if (nplte > 256) nplte = 256;
            for (int i = 0; i < nplte * 3; i++) plte[i] = cd[i];
        } else if (type[0]=='I'&&type[1]=='D'&&type[2]=='A'&&type[3]=='T') {
            if (!idat) { idatcap = len; idat = (u8 *)kmalloc(idatcap); if (!idat) return -1; }
            for (u32 i = 0; i < clen && idatn < idatcap; i++) idat[idatn++] = cd[i];
        } else if (type[0]=='I'&&type[1]=='E'&&type[2]=='N'&&type[3]=='D') {
            break;
        }
        p += 12 + clen;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || interlace != 0 || !idat) { if (idat) kfree(idat); return -1; }

    int channels = ctype == 2 ? 3 : ctype == 6 ? 4 : ctype == 4 ? 2 : 1;   /* 0/3 -> 1 */
    int bpp = (channels * depth + 7) / 8; if (bpp < 1) bpp = 1;
    u32 stride = ((u32)w * channels * depth + 7) / 8;
    u32 rawlen = (stride + 1) * (u32)h;

    u8 *raw = (u8 *)kmalloc(rawlen ? rawlen : 1);
    if (!raw) { kfree(idat); return -1; }
    u32 got = 0;
    int rc = inflate_zlib(idat, idatn, raw, rawlen, &got);
    kfree(idat);
    if (rc != 0 || got < rawlen) { kfree(raw); return -1; }

    /* un-filter into place (row = filter byte + stride data) */
    for (int y = 0; y < h; y++) {
        u8 *line = raw + (u32)y * (stride + 1);
        int f = line[0]; u8 *cur = line + 1;
        u8 *prev = y > 0 ? raw + (u32)(y - 1) * (stride + 1) + 1 : 0;
        for (u32 i = 0; i < stride; i++) {
            int a = i >= (u32)bpp ? cur[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= (u32)bpp) ? prev[i - bpp] : 0;
            int x = cur[i];
            if (f == 1) x += a; else if (f == 2) x += b;
            else if (f == 3) x += (a + b) / 2; else if (f == 4) x += paeth(a, b, c);
            cur[i] = (u8)x;
        }
    }

    u32 *rgb = (u32 *)kmalloc((u32)w * h * 4);
    if (!rgb) { kfree(raw); return -1; }
    int bps = depth == 16 ? 2 : 1;   /* bytes per sample; 16-bit -> take high byte */
    for (int y = 0; y < h; y++) {
        u8 *cur = raw + (u32)y * (stride + 1) + 1;
        for (int x = 0; x < w; x++) {
            u32 r, g, bl;
            if (ctype == 3) {                         /* palette (1/2/4/8-bit index) */
                int idx;
                if (depth == 8) idx = cur[x];
                else { int per = 8 / depth, shift = (per - 1 - (x % per)) * depth;
                       idx = (cur[x / per] >> shift) & ((1 << depth) - 1); }
                if (idx >= nplte) idx = 0;
                r = plte[idx * 3]; g = plte[idx * 3 + 1]; bl = plte[idx * 3 + 2];
            } else if (ctype == 0 || ctype == 4) {    /* gray (+alpha) */
                int gg = cur[x * channels * bps];
                r = g = bl = (u32)gg;
            } else {                                   /* RGB / RGBA */
                r = cur[(x * channels) * bps];
                g = cur[(x * channels + 1) * bps];
                bl = cur[(x * channels + 2) * bps];
            }
            rgb[y * w + x] = (r << 16) | (g << 8) | bl;
        }
    }
    kfree(raw);
    *out = rgb; *ow = w; *oh = h;
    return 0;
}
