/* Debian95 — pluggable font system. Two slots: slot 0 = the system UI font,
 * slot 1 = the console font. The built-in 8x8 is the default for a slot with no
 * .LGF loaded. font_select() picks the slot for subsequent text drawing. */
#include <debian95/font.h>
#include <debian95/krt.h>     /* kmalloc; portable across arches */

extern const u8 font8x8[96][8];

typedef struct {
    int loaded;                 /* 0 = built-in, 1 = LGF */
    u16 height, first, count, maxw;
    const u8 *glyphs;           /* LGF glyph table */
    u16 stride;                 /* LGF per-glyph stride */
} FontState;

static FontState fs[2];          /* [0]=UI, [1]=console */
static int cur;                  /* active slot */

static u16 g_builtin[96][8];
static int g_builtin_ready;

static inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static void ensure_builtin(void) {
    if (!g_builtin_ready) {
        for (int i = 0; i < 96; i++)
            for (int r = 0; r < 8; r++)
                g_builtin[i][r] = font8x8[i][r];
        g_builtin_ready = 1;
    }
}

void font_select(int slot) { cur = (slot == 1) ? 1 : 0; }

void font_use_builtin(void) {
    ensure_builtin();
    fs[cur].loaded = 0;
    fs[cur].height = 8; fs[cur].first = 0x20; fs[cur].count = 96; fs[cur].maxw = 8;
}

static int load_into(int slot, const void *buf, u32 len) {
    const u8 *p = (const u8 *)buf;
    if (len < 16 || p[0] != 'L' || p[1] != 'G' || p[2] != 'F' || p[3] != '1')
        return -1;
    u16 height = rd16(p + 4), first = rd16(p + 6), count = rd16(p + 8), maxw = rd16(p + 10);
    if (height == 0 || height > 32 || maxw == 0 || maxw > 16 || count == 0)
        return -1;
    u16 stride = (u16)(2 + height * 2);
    if ((u32)16 + (u32)count * stride > len) return -1;
    fs[slot].loaded = 1;
    fs[slot].height = height; fs[slot].first = first; fs[slot].count = count; fs[slot].maxw = maxw;
    fs[slot].glyphs = p + 16;
    fs[slot].stride = stride;
    return 0;
}

int font_load_lgf(const void *buf, u32 len)     { return load_into(0, buf, len); }
int font_load_console(const void *buf, u32 len) { return load_into(1, buf, len); }

/* ---- native Windows .FON loader (NE resource container of FNT strikes) ---- */
static inline u32 rd32(const u8 *p) { return (u32)(p[0]|(p[1]<<8)|(p[2]<<16)|((u32)p[3]<<24)); }

int font_load_fon(const void *buf, u32 len, int slot, int target_h) {
    const u8 *d = (const u8 *)buf;
    if (len < 0x40 || d[0] != 'M' || d[1] != 'Z') return -1;
    u32 ne = rd32(d + 0x3C);
    if (ne + 0x28 > len || d[ne] != 'N' || d[ne + 1] != 'E') return -1;
    u32 rsrctab = ne + rd16(d + ne + 0x24);
    u16 shift = rd16(d + rsrctab);

    /* pick the FNT (RT_FONT = 0x8008) strike closest to target_h */
    u32 best = 0; int best_h = 0, best_diff = 1 << 30;
    u32 o = rsrctab + 2;
    while (o + 8 <= len) {
        u16 rtype = rd16(d + o); if (rtype == 0) break;
        u16 count = rd16(d + o + 2); o += 8;
        for (int i = 0; i < count && o + 12 <= len; i++, o += 12) {
            if (rtype == 0x8008) {
                u32 foff = (u32)rd16(d + o) << shift;
                if (foff + 0x60 < len) {
                    int ph = rd16(d + foff + 0x58);
                    int diff = ph > target_h ? ph - target_h : target_h - ph;
                    if (diff < best_diff) { best_diff = diff; best = foff; best_h = ph; }
                }
            }
        }
    }
    if (!best) return -1;

    u32 baseo = best; u16 ver = rd16(d + baseo); u16 H = (u16)best_h;
    if (H == 0 || H > 32) return -1;
    u8 first = d[baseo + 0x5F], last = d[baseo + 0x60];
    u32 ctab = baseo + (ver >= 0x300 ? 0x94 : 0x76);
    int entsz = ver >= 0x300 ? 6 : 4;
    int FIRST = 0x20, LAST = 0x7E, count = LAST - FIRST + 1;
    u16 stride = (u16)(2 + H * 2);

    u8 *gb = (u8 *)kmalloc((u32)count * stride);
    if (!gb) return -1;
    int maxw = 0;
    for (int code = FIRST; code <= LAST; code++) {
        u8 *g = gb + (code - FIRST) * stride;
        u16 *rows = (u16 *)(g + 2);
        int cw = 0;
        if (code >= first && code <= last) {
            u32 e = ctab + (code - first) * entsz;
            cw = rd16(d + e);
            u32 coff = entsz == 6 ? rd32(d + e + 2) : rd16(d + e + 2);
            for (int r = 0; r < H; r++) {
                u16 v = 0;
                for (int x = 0; x < cw && x < 16; x++)
                    if (d[baseo + coff + (x >> 3) * H + r] & (0x80 >> (x & 7))) v |= (1 << x);
                rows[r] = v;
            }
            if (cw > maxw) maxw = cw;
        } else {
            for (int r = 0; r < H; r++) rows[r] = 0;
        }
        g[0] = (u8)(cw > 16 ? 16 : (cw ? cw : 6)); g[1] = 0;
    }
    fs[slot].loaded = 1; fs[slot].height = H; fs[slot].first = (u16)FIRST;
    fs[slot].count = (u16)count; fs[slot].maxw = (u16)(maxw > 16 ? 16 : maxw);
    fs[slot].glyphs = gb; fs[slot].stride = stride;
    return 0;
}

u16 font_height(void)    { return fs[cur].height ? fs[cur].height : 8; }
u16 font_max_width(void) { return fs[cur].maxw ? fs[cur].maxw : 8; }

const u16 *font_glyph(char c, u8 *advance) {
    u8 ch = (u8)c;
    FontState *A = &fs[cur];
    if (A->height == 0) { font_use_builtin(); }
    if (ch < A->first || ch >= A->first + A->count) { *advance = (u8)A->maxw; return NULL; }
    u16 idx = ch - A->first;
    if (!A->loaded) { *advance = 8; return g_builtin[idx]; }
    const u8 *base = A->glyphs + (u32)idx * A->stride;
    *advance = base[0] ? base[0] : (u8)A->maxw;
    return (const u16 *)(base + 2);
}

/* The built-in 8x8 glyph for c, regardless of the active font (brand wordmark). */
const u16 *font_builtin_glyph(char c, u8 *advance) {
    ensure_builtin();
    u8 ch = (u8)c; *advance = 8;
    if (ch < 0x20 || ch >= 0x20 + 96) return 0;
    return g_builtin[ch - 0x20];
}

u32 font_measure(const char *s) {
    u32 w = 0;
    for (; *s; s++) { u8 adv; font_glyph(*s, &adv); w += adv; }
    return w;
}
