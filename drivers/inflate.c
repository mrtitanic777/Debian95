/* Debian95 — DEFLATE (RFC 1951) inflater + zlib (RFC 1950) wrapper.
 *
 * A compact, original canonical-Huffman inflater used by the PNG decoder (and
 * later by other codecs). Output size is known ahead of time by the callers
 * (PNG knows exactly how many raw bytes a valid image expands to), so this
 * writes into a caller-provided buffer and never grows.
 */
#include <debian95/types.h>

typedef struct {
    const u8 *src; u32 len, pos;   /* input */
    u32 bitbuf; int bitcnt;        /* LSB-first bit accumulator */
    u8 *dst; u32 cap, out;         /* output */
} Inf;

static int bit(Inf *s) {
    if (s->bitcnt == 0) {
        if (s->pos >= s->len) return -1;
        s->bitbuf = s->src[s->pos++];
        s->bitcnt = 8;
    }
    int b = s->bitbuf & 1;
    s->bitbuf >>= 1; s->bitcnt--;
    return b;
}
static int bits(Inf *s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) { int b = bit(s); if (b < 0) return -1; v |= b << i; }
    return v;
}

/* canonical Huffman decode table */
typedef struct { u16 counts[16]; u16 symbol[288]; } Huff;

static void huff_build(Huff *h, const u8 *lengths, int n) {
    for (int i = 0; i < 16; i++) h->counts[i] = 0;
    for (int i = 0; i < n; i++) h->counts[lengths[i]]++;
    h->counts[0] = 0;
    u16 offs[16]; offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->counts[i];
    for (int i = 0; i < n; i++) if (lengths[i]) h->symbol[offs[lengths[i]]++] = (u16)i;
}
static int huff_decode(Inf *s, Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int b = bit(s); if (b < 0) return -1;
        code |= b;
        int count = h->counts[len];
        if (code - first < count) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}

static const u16 LEN_BASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const u8  LEN_EXTRA[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const u16 DIST_BASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const u8  DIST_EXTRA[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int inflate_block(Inf *s, Huff *lh, Huff *dh) {
    for (;;) {
        int sym = huff_decode(s, lh);
        if (sym < 0) return -1;
        if (sym == 256) return 0;                  /* end of block */
        if (sym < 256) {
            if (s->out >= s->cap) return -1;
            s->dst[s->out++] = (u8)sym;
        } else {
            sym -= 257; if (sym >= 29) return -1;
            int e = LEN_EXTRA[sym]; int length = LEN_BASE[sym] + (e ? bits(s, e) : 0);
            int dsym = huff_decode(s, dh); if (dsym < 0 || dsym >= 30) return -1;
            int de = DIST_EXTRA[dsym]; int dist = DIST_BASE[dsym] + (de ? bits(s, de) : 0);
            if ((u32)dist > s->out) return -1;
            u32 from = s->out - dist;
            for (int i = 0; i < length; i++) {
                if (s->out >= s->cap) return -1;
                s->dst[s->out] = s->dst[from + i];
                s->out++;
            }
        }
    }
}

static void fixed_tables(Huff *lh, Huff *dh) {
    u8 ll[288]; for (int i = 0; i < 144; i++) ll[i] = 8;
    for (int i = 144; i < 256; i++) ll[i] = 9;
    for (int i = 256; i < 280; i++) ll[i] = 7;
    for (int i = 280; i < 288; i++) ll[i] = 8;
    huff_build(lh, ll, 288);
    u8 dl[30]; for (int i = 0; i < 30; i++) dl[i] = 5;
    huff_build(dh, dl, 30);
}

static const u8 CLORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static int dynamic_tables(Inf *s, Huff *lh, Huff *dh) {
    int hlit = bits(s, 5) + 257;
    int hdist = bits(s, 5) + 1;
    int hclen = bits(s, 4) + 4;
    if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
    u8 cl[19]; for (int i = 0; i < 19; i++) cl[i] = 0;
    for (int i = 0; i < hclen; i++) { int v = bits(s, 3); if (v < 0) return -1; cl[CLORDER[i]] = (u8)v; }
    Huff ch; huff_build(&ch, cl, 19);
    u8 lens[288 + 32]; int n = 0, total = hlit + hdist;
    while (n < total) {
        int sym = huff_decode(s, &ch); if (sym < 0) return -1;
        if (sym < 16) lens[n++] = (u8)sym;
        else if (sym == 16) { if (n == 0) return -1; int r = bits(s, 2) + 3; while (r-- && n < total) { lens[n] = lens[n - 1]; n++; } }
        else if (sym == 17) { int r = bits(s, 3) + 3;  while (r-- && n < total) lens[n++] = 0; }
        else if (sym == 18) { int r = bits(s, 7) + 11; while (r-- && n < total) lens[n++] = 0; }
        else return -1;
    }
    huff_build(lh, lens, hlit);
    huff_build(dh, lens + hlit, hdist);
    return 0;
}

int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *written) {
    Inf s = { src, srclen, 0, 0, 0, dst, dstcap, 0 };
    int final = 0;
    while (!final) {
        final = bit(&s); if (final < 0) return -1;
        int type = bits(&s, 2); if (type < 0) return -1;
        if (type == 0) {                     /* stored */
            s.bitbuf = 0; s.bitcnt = 0;      /* align to byte */
            if (s.pos + 4 > s.len) return -1;
            u32 blen = s.src[s.pos] | (s.src[s.pos + 1] << 8); s.pos += 4;  /* len + ~len */
            for (u32 i = 0; i < blen; i++) {
                if (s.pos >= s.len || s.out >= s.cap) return -1;
                s.dst[s.out++] = s.src[s.pos++];
            }
        } else if (type == 1) {
            Huff lh, dh; fixed_tables(&lh, &dh);
            if (inflate_block(&s, &lh, &dh) != 0) return -1;
        } else if (type == 2) {
            Huff lh, dh;
            if (dynamic_tables(&s, &lh, &dh) != 0) return -1;
            if (inflate_block(&s, &lh, &dh) != 0) return -1;
        } else return -1;
    }
    if (written) *written = s.out;
    return 0;
}

int inflate_zlib(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *written) {
    if (srclen < 2) return -1;
    /* skip the 2-byte zlib header (CMF/FLG); ignore the trailing adler32 */
    return inflate_raw(src + 2, srclen - 2, dst, dstcap, written);
}
