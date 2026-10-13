/* Debian95 — native image decoders for Windows formats (no offline conversion).
 * Shared DIB core decodes 1/4/8/24/32-bpp bitmaps; used by BMP files and, later,
 * the DIBs inside .ICO/.CUR resources. */
#include <debian95/img.h>
#include <debian95/cursor.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>

static inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 rd32(const u8 *p) { return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24)); }

static inline u32 pal_rgb(const u8 *pal, int i) {
    return ((u32)pal[i * 4 + 2] << 16) | ((u32)pal[i * 4 + 1] << 8) | pal[i * 4 + 0];
}

/* Decode a DIB body: a BITMAPINFOHEADER at `hdr`, its palette, and pixel bits at
 * `bits`, `rows` scanlines of width `w`, bottom-up. Writes w*rows RGB pixels into
 * `out` (row 0 = top). Returns 0 on success. */
int dib_decode(const u8 *hdr, const u8 *pal, const u8 *bits, int w, int rows,
               int bpp, u32 *out) {
    (void)hdr;
    int rowsz = ((w * bpp + 31) / 32) * 4;
    for (int y = 0; y < rows; y++) {
        const u8 *row = bits + (rows - 1 - y) * rowsz;   /* bottom-up */
        for (int x = 0; x < w; x++) {
            u32 c;
            if (bpp == 1)      { int b = (row[x >> 3] >> (7 - (x & 7))) & 1; c = pal_rgb(pal, b); }
            else if (bpp == 4) { int b = row[x >> 1]; int i = (x & 1) ? (b & 0xF) : (b >> 4); c = pal_rgb(pal, i); }
            else if (bpp == 8) { c = pal_rgb(pal, row[x]); }
            else if (bpp == 24){ c = ((u32)row[x*3+2] << 16) | ((u32)row[x*3+1] << 8) | row[x*3]; }
            else               { c = ((u32)row[x*4+2] << 16) | ((u32)row[x*4+1] << 8) | row[x*4]; }
            out[y * w + x] = c;
        }
    }
    return 0;
}

/* Decode a .CUR cursor into a Debian95Cursor (2-bit codes: 0=transparent 1=black
 * 2=white 3=gray) with its hotspot. pix is kmalloc'd. Returns 0 on success. */
int cur_decode(const u8 *d, u32 len, Debian95Cursor *out) {
    if (len < 22 || rd16(d + 2) != 2) return -1;             /* CUR type */
    int hotx = rd16(d + 10), hoty = rd16(d + 12);
    u32 off = rd32(d + 18);
    if (off + 40 > len) return -1;
    const u8 *hdr = d + off;
    int w = (int)rd32(hdr + 4), bh = (int)rd32(hdr + 8), bpp = rd16(hdr + 14);
    int h = bh / 2;
    if (w <= 0 || h <= 0 || w > 64 || h > 64) return -1;
    int ncol = bpp <= 8 ? (1 << bpp) : 0;
    const u8 *pal = hdr + 40;
    const u8 *xr = pal + ncol * 4;
    int rowsz = ((w * bpp + 31) / 32) * 4;
    int androw = ((w + 31) / 32) * 4;
    const u8 *andm = xr + rowsz * h;
    if ((u32)(andm - d) + androw * h > len) return -1;

    u8 *pix = (u8 *)kmalloc((u32)w * h);
    if (!pix) return -1;
    for (int y = 0; y < h; y++) {
        int sy = h - 1 - y;                                   /* bottom-up */
        for (int x = 0; x < w; x++) {
            int am = (andm[sy * androw + (x >> 3)] >> (7 - (x & 7))) & 1;
            u32 c;
            const u8 *row = xr + sy * rowsz;
            if (bpp == 1)      { int b = (row[x >> 3] >> (7 - (x & 7))) & 1; c = pal_rgb(pal, b); }
            else if (bpp == 4) { int b = row[x >> 1]; int i = (x & 1) ? (b & 0xF) : (b >> 4); c = pal_rgb(pal, i); }
            else if (bpp == 8) { c = pal_rgb(pal, row[x]); }
            else if (bpp == 24){ c = ((u32)row[x*3+2]<<16)|((u32)row[x*3+1]<<8)|row[x*3]; }
            else               { c = ((u32)row[x*4+2]<<16)|((u32)row[x*4+1]<<8)|row[x*4]; }
            int lum = (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3;
            if (am) pix[y * w + x] = lum > 128 ? 1 : 0;       /* invert-on -> black, else clear */
            else    pix[y * w + x] = lum < 64 ? 1 : lum > 192 ? 2 : 3;
        }
    }
    out->w = (u8)w; out->h = (u8)h; out->hotx = (u8)hotx; out->hoty = (u8)hoty; out->pix = pix;
    return 0;
}

/* Decode an icon DIB (BITMAPINFOHEADER + palette + XOR + AND, height=2*h) at
 * `hdr` into a kmalloc'd RGB buffer; transparent pixels filled with `bg`. */
static int icon_dib(const u8 *hdr, u32 avail, u32 bg, u32 **out, int *ow, int *oh) {
    if (avail < 40) return -1;
    int w = (int)rd32(hdr + 4), fullh = (int)rd32(hdr + 8), bpp = rd16(hdr + 14);
    int h = fullh / 2;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) return -1;
    int ncol = bpp <= 8 ? (1 << bpp) : 0;
    const u8 *pal = hdr + 40;
    const u8 *xr = pal + ncol * 4;
    int rowsz = ((w * bpp + 31) / 32) * 4;
    int androw = ((w + 31) / 32) * 4;
    const u8 *andm = xr + rowsz * h;
    u32 *px = (u32 *)kmalloc((u32)w * h * 4);
    if (!px) return -1;
    for (int y = 0; y < h; y++) {
        int sy = h - 1 - y;
        const u8 *row = xr + sy * rowsz;
        for (int x = 0; x < w; x++) {
            int am = (andm[sy * androw + (x >> 3)] >> (7 - (x & 7))) & 1;
            u32 c;
            if (bpp == 1)      { int b = (row[x >> 3] >> (7 - (x & 7))) & 1; c = pal_rgb(pal, b); }
            else if (bpp == 4) { int b = row[x >> 1]; int i = (x & 1) ? (b & 0xF) : (b >> 4); c = pal_rgb(pal, i); }
            else if (bpp == 8) { c = pal_rgb(pal, row[x]); }
            else if (bpp == 24){ c = ((u32)row[x*3+2]<<16)|((u32)row[x*3+1]<<8)|row[x*3]; }
            else               { c = ((u32)row[x*4+2]<<16)|((u32)row[x*4+1]<<8)|row[x*4]; }
            px[y * w + x] = am ? bg : c;
        }
    }
    *out = px; *ow = w; *oh = h;
    return 0;
}

/* Decode a plain DIB (BITMAPINFOHEADER + palette + pixels, no AND mask) into a
 * kmalloc'd RGB buffer; pixels equal to `transp` are replaced with `bg`. Used for
 * RT_BITMAP resources like the COMCTL32 standard toolbar strip. */
static int plain_dib(const u8 *hdr, u32 avail, u32 transp, u32 bg, u32 **out, int *ow, int *oh) {
    if (avail < 40) return -1;
    int w = (int)rd32(hdr + 4), h = (int)rd32(hdr + 8), bpp = rd16(hdr + 14);
    u32 comp = rd32(hdr + 16);
    int topdown = h < 0; if (topdown) h = -h;
    if (w <= 0 || h <= 0 || w > 1024 || h > 256) return -1;
    int ncol = bpp <= 8 ? (1 << bpp) : 0;
    const u8 *pal = hdr + 40; const u8 *px0 = pal + ncol * 4;
    u32 *px = (u32 *)kmalloc((u32)w * h * 4); if (!px) return -1;

    if (comp == 2 && bpp == 4) {                    /* BI_RLE4 (used by the COMCTL32 toolbar strip) */
        u8 *idx = (u8 *)kmalloc((u32)w * h); if (!idx) { kfree(px); return -1; }
        for (int i = 0; i < w * h; i++) idx[i] = 0;
        const u8 *p = px0, *end = hdr + avail; int x = 0, y = 0;
        while (p + 1 < end) {
            u8 cnt = p[0], val = p[1]; p += 2;
            if (cnt > 0) { for (int k = 0; k < cnt; k++) { if (x < w && y < h) idx[y*w+x] = (k&1) ? (val&0xF) : (val>>4); x++; } }
            else if (val == 0) { x = 0; y++; }
            else if (val == 1) break;                                  /* end of bitmap */
            else if (val == 2) { if (p + 1 < end) { x += p[0]; y += p[1]; p += 2; } }
            else {                                                     /* absolute run of `val` 4-bit indices */
                for (int k = 0; k < val; k++) { u8 b = p[k>>1]; int ci = (k&1) ? (b&0xF) : (b>>4); if (x < w && y < h) idx[y*w+x] = ci; x++; }
                int by = (val + 1) / 2; p += by + (by & 1);            /* word-aligned */
            }
            if (y >= h) break;
        }
        for (int r = 0; r < h; r++) { int gy = h - 1 - r;             /* RLE rows are bottom-up */
            for (int xx = 0; xx < w; xx++) { u32 c = pal_rgb(pal, idx[gy*w+xx]); px[r*w+xx] = (c == transp) ? bg : c; } }
        kfree(idx);
    } else {                                        /* uncompressed */
        int rowsz = ((w * bpp + 31) / 32) * 4;
        for (int y = 0; y < h; y++) {
            int sy = topdown ? y : (h - 1 - y);
            const u8 *row = px0 + sy * rowsz;
            for (int x = 0; x < w; x++) {
                u32 c;
                if (bpp == 1)      { int b = (row[x >> 3] >> (7 - (x & 7))) & 1; c = pal_rgb(pal, b); }
                else if (bpp == 4) { int b = row[x >> 1]; int i = (x & 1) ? (b & 0xF) : (b >> 4); c = pal_rgb(pal, i); }
                else if (bpp == 8) { c = pal_rgb(pal, row[x]); }
                else if (bpp == 24){ c = ((u32)row[x*3+2]<<16)|((u32)row[x*3+1]<<8)|row[x*3]; }
                else               { c = ((u32)row[x*4+2]<<16)|((u32)row[x*4+1]<<8)|row[x*4]; }
                px[y * w + x] = (c == transp) ? bg : c;
            }
        }
    }
    *out = px; *ow = w; *oh = h; return 0;
}

/* ---- native PE resource icon extraction (read icons from a real .DLL/.EXE) ---- */
static u32 pe_rva2off(const u8 *d, u32 rva) {
    u32 pe = rd32(d + 0x3C), coff = pe + 4;
    u16 nsec = rd16(d + coff + 2), optsz = rd16(d + coff + 16);
    u32 sh = coff + 20 + optsz;
    for (int i = 0; i < nsec; i++) {
        const u8 *s = d + sh + i * 40;
        u32 va = rd32(s + 12), sz = rd32(s + 16), raw = rd32(s + 20);
        if (rva >= va && rva < va + sz) return raw + (rva - va);
    }
    return 0;
}
static u32 rsrc_child(const u8 *d, u32 dir, u32 rbase, u32 id) {   /* find sub by id */
    u16 nn = rd16(d + dir + 12), ni = rd16(d + dir + 14); u32 e = dir + 16;
    for (int i = 0; i < nn + ni; i++) {
        u32 name = rd32(d + e + i * 8), off = rd32(d + e + i * 8 + 4);
        if ((name & 0x7fffffff) == id && (off & 0x80000000)) return rbase + (off & 0x7fffffff);
    }
    return 0;
}
static u32 rsrc_first(const u8 *d, u32 dir) {                     /* first child's offset field */
    if (rd16(d + dir + 12) + rd16(d + dir + 14) < 1) return 0;
    return rd32(d + dir + 16 + 4);
}

/* Extract RT_ICON `res_id` from a PE, decoded to RGB (transparent -> bg). 0=ok. */
int pe_icon(const u8 *d, u32 len, u16 res_id, u32 bg, u32 **out, int *w, int *h) {
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    u32 pe = rd32(d + 0x3C);
    if (pe + 0x18 > len || rd32(d + pe) != 0x00004550) return -1;   /* 'PE\0\0' */
    u32 opt = pe + 4 + 20; u16 magic = rd16(d + opt);
    u32 ddir = opt + (magic == 0x20b ? 112 : 96);
    u32 res_rva = rd32(d + ddir + 2 * 8);
    if (!res_rva) return -1;
    u32 rbase = pe_rva2off(d, res_rva);
    if (!rbase || rbase + 16 > len) return -1;

    u32 t = rsrc_child(d, rbase, rbase, 3);          /* RT_ICON */
    if (!t) return -1;
    u32 idd = rsrc_child(d, t, rbase, res_id);
    if (!idd) return -1;
    /* the id dir's first child is either a data entry directly, or a language
     * subdirectory whose first child is the data entry */
    u32 c1 = rsrc_first(d, idd);
    if (!c1) return -1;
    u32 leaf;
    if (c1 & 0x80000000) {                       /* language subdirectory */
        u32 c2 = rsrc_first(d, rbase + (c1 & 0x7fffffff));
        if (!c2) return -1;
        leaf = rbase + (c2 & 0x7fffffff);
    } else {                                     /* data entry directly */
        leaf = rbase + c1;
    }
    u32 data_rva = rd32(d + leaf), size = rd32(d + leaf + 4);
    u32 doff = pe_rva2off(d, data_rva);
    if (!doff || doff + 40 > len) return -1;
    return icon_dib(d + doff, size, bg, out, w, h);
}

/* Extract RT_BITMAP `res_id` from a PE, decoded to RGB (pixels == transp -> bg). */
int pe_bitmap(const u8 *d, u32 len, u16 res_id, u32 transp, u32 bg, u32 **out, int *w, int *h) {
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    u32 pe = rd32(d + 0x3C);
    if (pe + 0x18 > len || rd32(d + pe) != 0x00004550) return -1;
    u32 opt = pe + 4 + 20; u16 magic = rd16(d + opt);
    u32 ddir = opt + (magic == 0x20b ? 112 : 96);
    u32 res_rva = rd32(d + ddir + 2 * 8);
    if (!res_rva) return -1;
    u32 rbase = pe_rva2off(d, res_rva);
    if (!rbase || rbase + 16 > len) return -1;
    u32 t = rsrc_child(d, rbase, rbase, 2);          /* RT_BITMAP */
    if (!t) return -1;
    u32 idd = rsrc_child(d, t, rbase, res_id);
    if (!idd) return -1;
    u32 c1 = rsrc_first(d, idd);
    if (!c1) return -1;
    u32 leaf;
    if (c1 & 0x80000000) { u32 c2 = rsrc_first(d, rbase + (c1 & 0x7fffffff)); if (!c2) return -1; leaf = rbase + (c2 & 0x7fffffff); }
    else leaf = rbase + c1;
    u32 data_rva = rd32(d + leaf), size = rd32(d + leaf + 4);
    u32 doff = pe_rva2off(d, data_rva);
    if (!doff || doff + 40 > len) return -1;
    return plain_dib(d + doff, size, transp, bg, out, w, h);
}

/* Extract RT_ICON `res_id` from a 16-bit NE module (user.exe/pifmgr/moricons).
 * NE resources are alignment-shifted (offset/length in units of 2^shift). */
int ne_icon(const u8 *d, u32 len, u16 res_id, u32 bg, u32 **out, int *w, int *h) {
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    u32 ne = rd32(d + 0x3C);
    if (ne + 0x26 > len || d[ne] != 'N' || d[ne + 1] != 'E') return -1;
    u32 rtab = ne + rd16(d + ne + 0x24);                 /* resource table */
    if (rtab + 2 > len) return -1;
    u16 shift = rd16(d + rtab);
    u32 o = rtab + 2;
    while (o + 8 <= len) {
        u16 rtype = rd16(d + o);
        if (rtype == 0) break;
        u16 count = rd16(d + o + 2);
        o += 8;                                          /* skip rtTypeID,count,resvd(4) */
        for (int i = 0; i < count && o + 12 <= len; i++, o += 12) {
            if (rtype == 0x8003) {                       /* RT_ICON */
                u16 rid = rd16(d + o + 6) & 0x7fff;
                if (rid == res_id) {
                    u32 off = (u32)rd16(d + o) << shift;
                    u32 sz  = (u32)rd16(d + o + 2) << shift;
                    if (off + 40 > len || off < 0x40) return -1;
                    return icon_dib(d + off, sz, bg, out, w, h);
                }
            }
        }
    }
    return -1;
}

/* Dispatch RT_ICON extraction on the container kind (PE vs NE). */
int res_icon(const u8 *d, u32 len, u16 res_id, u32 bg, u32 **out, int *w, int *h) {
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    u32 off = rd32(d + 0x3C);
    if (off + 2 <= len && d[off] == 'N' && d[off + 1] == 'E')
        return ne_icon(d, len, res_id, bg, out, w, h);
    return pe_icon(d, len, res_id, bg, out, w, h);
}

/* Decode a .ICO icon (largest image) into a kmalloc'd RGB buffer; transparent
 * pixels (per the AND mask) are filled with `bg`. Returns 0 on success. */
int ico_decode(const u8 *d, u32 len, u32 bg, u32 **out, int *ow, int *oh) {
    if (len < 22 || rd16(d + 2) != 1) return -1;             /* ICO type */
    int n = rd16(d + 4);
    /* pick the largest image */
    u32 best_off = 0; int best_area = -1, bw = 0, bh = 0;
    for (int i = 0; i < n; i++) {
        const u8 *e = d + 6 + i * 16;
        int w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
        u32 off = rd32(e + 12);
        if (off + 40 > len) continue;
        if (w * h > best_area) { best_area = w * h; best_off = off; bw = w; bh = h; }
    }
    if (!best_off) return -1;
    const u8 *hdr = d + best_off;
    int w = (int)rd32(hdr + 4), fullh = (int)rd32(hdr + 8), bpp = rd16(hdr + 14);
    int h = fullh / 2;
    if (w != bw || h != bh || w <= 0 || h <= 0 || w > 256 || h > 256) { w = bw; h = bh; }
    int ncol = bpp <= 8 ? (1 << bpp) : 0;
    const u8 *pal = hdr + 40;
    const u8 *xr = pal + ncol * 4;
    int rowsz = ((w * bpp + 31) / 32) * 4;
    int androw = ((w + 31) / 32) * 4;
    const u8 *andm = xr + rowsz * h;

    u32 *px = (u32 *)kmalloc((u32)w * h * 4);
    if (!px) return -1;
    for (int y = 0; y < h; y++) {
        int sy = h - 1 - y;
        const u8 *row = xr + sy * rowsz;
        for (int x = 0; x < w; x++) {
            int am = (andm[sy * androw + (x >> 3)] >> (7 - (x & 7))) & 1;
            u32 c;
            if (bpp == 1)      { int b = (row[x >> 3] >> (7 - (x & 7))) & 1; c = pal_rgb(pal, b); }
            else if (bpp == 4) { int b = row[x >> 1]; int i = (x & 1) ? (b & 0xF) : (b >> 4); c = pal_rgb(pal, i); }
            else if (bpp == 8) { c = pal_rgb(pal, row[x]); }
            else if (bpp == 24){ c = ((u32)row[x*3+2]<<16)|((u32)row[x*3+1]<<8)|row[x*3]; }
            else               { c = ((u32)row[x*4+2]<<16)|((u32)row[x*4+1]<<8)|row[x*4]; }
            px[y * w + x] = am ? bg : c;
        }
    }
    *out = px; *ow = w; *oh = h;
    return 0;
}

/* Decode a .BMP file into a freshly kmalloc'd RGB buffer. Returns 0 on success. */
int bmp_decode(const u8 *d, u32 len, u32 **out, int *ow, int *oh) {
    if (len < 54 || d[0] != 'B' || d[1] != 'M') return -1;
    u32 dataoff = rd32(d + 10);
    u32 hdrsz = rd32(d + 14);
    int w = (int)rd32(d + 18), h = (int)rd32(d + 22);
    u16 bpp = rd16(d + 28);
    int flip = 1; if (h < 0) { h = -h; flip = 0; }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return -1;

    const u8 *pal = d + 14 + hdrsz;
    const u8 *bits = d + dataoff;
    if (dataoff >= len) return -1;

    u32 *px = (u32 *)kmalloc((u32)w * h * 4);
    if (!px) return -1;
    if (flip) {
        dib_decode(d + 14, pal, bits, w, h, bpp, px);
    } else {
        /* top-down: decode as bottom-up then it's already correct — re-run without flip */
        int rowsz = ((w * bpp + 31) / 32) * 4;
        for (int y = 0; y < h; y++) {
            const u8 *row = bits + y * rowsz;
            for (int x = 0; x < w; x++) {
                u32 c;
                if (bpp == 1)      { int b = (row[x >> 3] >> (7 - (x & 7))) & 1; c = pal_rgb(pal, b); }
                else if (bpp == 4) { int b = row[x >> 1]; int i = (x & 1) ? (b & 0xF) : (b >> 4); c = pal_rgb(pal, i); }
                else if (bpp == 8) { c = pal_rgb(pal, row[x]); }
                else if (bpp == 24){ c = ((u32)row[x*3+2] << 16) | ((u32)row[x*3+1] << 8) | row[x*3]; }
                else               { c = ((u32)row[x*4+2] << 16) | ((u32)row[x*4+1] << 8) | row[x*4]; }
                px[y * w + x] = c;
            }
        }
    }
    *out = px; *ow = w; *oh = h;
    return 0;
}
