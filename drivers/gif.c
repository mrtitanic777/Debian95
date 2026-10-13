/* Debian95 — GIF decoder (87a/89a): LZW decompression + colour tables.
 *
 * Decodes the first image of a GIF into an RGB buffer (handles interlacing and
 * a local or global colour table). Animation frames are exposed separately for
 * the media player. LZW dictionary is stored as prefix/suffix chains so strings
 * never need contiguous storage.
 */
#include <debian95/types.h>
#include <debian95/mm.h>

typedef struct {
    const u8 *d; u32 len, pos;
    u8 gct[256 * 3]; int ngct, gct_flag;
    int w, h;
} Gif;

/* gather the image's data sub-blocks (each: [len][len bytes], terminated by 0) */
static u8 *gather_subblocks(const u8 *d, u32 len, u32 *pp, u32 *outn) {
    u32 p = *pp, total = 0, scan = p;
    while (scan < len) { u32 n = d[scan++]; if (n == 0) break; scan += n; if (scan > len) return 0; }
    u8 *buf = (u8 *)kmalloc(scan - p + 1);
    if (!buf) return 0;
    while (p < len) {
        u32 n = d[p++]; if (n == 0) break;
        for (u32 i = 0; i < n && p < len; i++) buf[total++] = d[p++];
    }
    *pp = p; *outn = total; return buf;
}
static void skip_subblocks(const u8 *d, u32 len, u32 *pp) {
    u32 p = *pp;
    while (p < len) { u32 n = d[p++]; if (n == 0) break; p += n; }
    *pp = p;
}

/* LZW decode `src`(srcn) with min code size `mcs` into `idx` (cap pixels) */
static int lzw_decode(const u8 *src, u32 srcn, int mcs, u8 *idx, u32 cap) {
    int clear = 1 << mcs, eoi = clear + 1;
    u16 prefix[4096]; u8 suffix[4096], first[4096];
    for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (u8)i; first[i] = (u8)i; }
    int next = eoi + 1, codesize = mcs + 1, prev = -1;
    u32 outn = 0, bitpos = 0;
    u8 stack[4096]; int sp = 0;
    while (1) {
        if ((bitpos + codesize) > srcn * 8) break;
        int code = 0;
        for (int i = 0; i < codesize; i++) { int byte = src[(bitpos + i) >> 3]; int b = (byte >> ((bitpos + i) & 7)) & 1; code |= b << i; }
        bitpos += codesize;
        if (code == clear) { next = eoi + 1; codesize = mcs + 1; prev = -1; continue; }
        if (code == eoi) break;
        int cur = code;
        if (prev == -1) { if (outn < cap) idx[outn++] = first[code]; prev = code; continue; }
        if (code >= next) { stack[sp++] = (u8)first[prev]; cur = prev; }   /* KwKwK */
        while (cur >= clear && sp < 4096) { stack[sp++] = suffix[cur]; cur = prefix[cur]; }
        stack[sp++] = (u8)cur;
        while (sp > 0 && outn < cap) idx[outn++] = stack[--sp];
        sp = 0;
        if (next < 4096) { prefix[next] = (u16)prev; suffix[next] = (u8)cur; first[next] = first[prev]; next++; }
        if (next == (1 << codesize) && codesize < 12) codesize++;
        prev = code;
        if (outn >= cap) break;
    }
    return 0;
}

/* de-interlace order: passes start at 0,4,2,1 with steps 8,8,4,2 */
static void place_row(u8 *dst, const u8 *rowidx, int w, int y) {
    for (int x = 0; x < w; x++) dst[y * w + x] = rowidx[x];
}

int gif_decode(const u8 *data, u32 len, u32 **out, int *ow, int *oh) {
    if (len < 13 || data[0] != 'G' || data[1] != 'I' || data[2] != 'F') return -1;
    Gif g; g.d = data; g.len = len;
    int sw = data[6] | (data[7] << 8);
    (void)sw;
    int packed = data[10];
    g.gct_flag = (packed >> 7) & 1;
    g.ngct = 1 << ((packed & 7) + 1);
    u32 p = 13;
    if (g.gct_flag) { for (int i = 0; i < g.ngct * 3 && p < len; i++) g.gct[i] = data[p++]; }

    while (p < len) {
        int b = data[p++];
        if (b == 0x21) {                       /* extension: skip */
            p++;                               /* label */
            skip_subblocks(data, len, &p);
        } else if (b == 0x2C) {                /* image descriptor */
            int ix = data[p] | (data[p+1] << 8);
            int iy = data[p+2] | (data[p+3] << 8);
            int iw = data[p+4] | (data[p+5] << 8);
            int ih = data[p+6] | (data[p+7] << 8);
            int ipacked = data[p+8]; p += 9;
            (void)ix; (void)iy;
            u8 lct[256 * 3]; int lct_flag = (ipacked >> 7) & 1, interlace = (ipacked >> 6) & 1;
            int nlct = 1 << ((ipacked & 7) + 1);
            const u8 *ct = g.gct; int nct = g.ngct;
            if (lct_flag) { for (int i = 0; i < nlct * 3 && p < len; i++) lct[i] = data[p++]; ct = lct; nct = nlct; }
            if (iw <= 0 || ih <= 0 || iw > 4096 || ih > 4096) return -1;
            int mcs = data[p++];
            u32 srcn = 0; u8 *src = gather_subblocks(data, len, &p, &srcn);
            if (!src) return -1;
            u8 *idx = (u8 *)kmalloc((u32)iw * ih);
            if (!idx) { kfree(src); return -1; }
            lzw_decode(src, srcn, mcs, idx, (u32)iw * ih);
            kfree(src);
            /* de-interlace if needed */
            if (interlace) {
                u8 *lin = (u8 *)kmalloc((u32)iw * ih);
                if (lin) {
                    int row = 0;
                    int starts[4] = { 0, 4, 2, 1 }, steps[4] = { 8, 8, 4, 2 };
                    for (int pass = 0; pass < 4; pass++)
                        for (int y = starts[pass]; y < ih; y += steps[pass]) { place_row(lin, idx + row * iw, iw, y); row++; }
                    kfree(idx); idx = lin;
                }
            }
            u32 *rgb = (u32 *)kmalloc((u32)iw * ih * 4);
            if (!rgb) { kfree(idx); return -1; }
            for (int i = 0; i < iw * ih; i++) {
                int ci = idx[i]; if (ci >= nct) ci = 0;
                rgb[i] = ((u32)ct[ci*3] << 16) | ((u32)ct[ci*3+1] << 8) | ct[ci*3+2];
            }
            kfree(idx);
            *out = rgb; *ow = iw; *oh = ih;
            return 0;
        } else if (b == 0x3B) break;           /* trailer */
        else break;
    }
    return -1;
}

/* ---- animated GIF: decode every frame, composited onto the logical screen ---- */
#define GIF_MAXFR 256
static u32 ct_rgb(const u8 *ct, int i) { return ((u32)ct[i*3] << 16) | ((u32)ct[i*3+1] << 8) | ct[i*3+2]; }

int gif_decode_all(const u8 *data, u32 len, u32 ***frames_out, int **delays_out,
                   int *count, int *ow, int *oh) {
    if (len < 13 || data[0] != 'G' || data[1] != 'I' || data[2] != 'F') return -1;
    int sw = data[6] | (data[7] << 8), sh = data[8] | (data[9] << 8);
    int packed = data[10], gct_flag = (packed >> 7) & 1, ngct = 1 << ((packed & 7) + 1);
    int bgindex = data[11];
    if (sw <= 0 || sh <= 0 || sw > 2048 || sh > 2048) return -1;
    u8 gct[256 * 3]; u32 p = 13;
    if (gct_flag) for (int i = 0; i < ngct * 3 && p < len; i++) gct[i] = data[p++];

    u32 sz = (u32)sw * sh;
    u32 *canvas = (u32 *)kmalloc(sz * 4);
    u32 **frames = (u32 **)kmalloc(GIF_MAXFR * sizeof(u32 *));
    int *delays = (int *)kmalloc(GIF_MAXFR * sizeof(int));
    if (!canvas || !frames || !delays) { if (canvas) kfree(canvas); if (frames) kfree(frames); if (delays) kfree(delays); return -1; }
    u32 bg = gct_flag ? ct_rgb(gct, bgindex) : 0;
    for (u32 i = 0; i < sz; i++) canvas[i] = bg;

    int nf = 0, delay = 10, tindex = -1, disposal = 0;
    while (p < len && nf < GIF_MAXFR) {
        int b = data[p++];
        if (b == 0x21) {
            int label = data[p++];
            if (label == 0xF9) {
                int bs = data[p]; int flags = data[p + 1];
                disposal = (flags >> 2) & 7;
                tindex = (flags & 1) ? data[p + 4] : -1;
                delay = (data[p + 2] | (data[p + 3] << 8)); if (delay == 0) delay = 10;
                p += 1 + bs; skip_subblocks(data, len, &p);
            } else { skip_subblocks(data, len, &p); }
        } else if (b == 0x2C) {
            int ix = data[p] | (data[p+1] << 8), iy = data[p+2] | (data[p+3] << 8);
            int iw = data[p+4] | (data[p+5] << 8), ih = data[p+6] | (data[p+7] << 8);
            int ipacked = data[p+8]; p += 9;
            u8 lct[256 * 3]; int lct_flag = (ipacked >> 7) & 1, interlace = (ipacked >> 6) & 1;
            int nlct = 1 << ((ipacked & 7) + 1);
            const u8 *ct = gct; int nct = ngct;
            if (lct_flag) { for (int i = 0; i < nlct * 3 && p < len; i++) lct[i] = data[p++]; ct = lct; nct = nlct; }
            if (iw <= 0 || ih <= 0 || iw > sw || ih > sh) break;
            int mcs = data[p++];
            u32 srcn = 0; u8 *src = gather_subblocks(data, len, &p, &srcn);
            if (!src) break;
            u8 *idx = (u8 *)kmalloc((u32)iw * ih);
            if (!idx) { kfree(src); break; }
            lzw_decode(src, srcn, mcs, idx, (u32)iw * ih);
            kfree(src);
            if (interlace) {
                u8 *lin = (u8 *)kmalloc((u32)iw * ih);
                if (lin) { int row = 0; int st[4] = {0,4,2,1}, sp[4] = {8,8,4,2};
                    for (int pass = 0; pass < 4; pass++)
                        for (int y = st[pass]; y < ih; y += sp[pass]) { place_row(lin, idx + row * iw, iw, y); row++; }
                    kfree(idx); idx = lin; }
            }
            for (int row = 0; row < ih; row++)
                for (int col = 0; col < iw; col++) {
                    int ci = idx[row * iw + col];
                    if (tindex >= 0 && ci == tindex) continue;
                    if (ci >= nct) ci = 0;
                    int cx = ix + col, cy = iy + row;
                    if (cx >= 0 && cy >= 0 && cx < sw && cy < sh) canvas[cy * sw + cx] = ct_rgb(ct, ci);
                }
            kfree(idx);
            u32 *fr = (u32 *)kmalloc(sz * 4);
            if (!fr) break;
            for (u32 i = 0; i < sz; i++) fr[i] = canvas[i];
            frames[nf] = fr; delays[nf] = delay; nf++;
            if (disposal == 2)                       /* restore this frame's area to background */
                for (int row = 0; row < ih; row++)
                    for (int col = 0; col < iw; col++) {
                        int cx = ix + col, cy = iy + row;
                        if (cx >= 0 && cy >= 0 && cx < sw && cy < sh) canvas[cy * sw + cx] = bg;
                    }
            delay = 10; tindex = -1; disposal = 0;
        } else if (b == 0x3B) break;
        else break;
    }
    kfree(canvas);
    if (nf == 0) { kfree(frames); kfree(delays); return -1; }
    *frames_out = frames; *delays_out = delays; *count = nf; *ow = sw; *oh = sh;
    return 0;
}
