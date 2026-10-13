/* Debian95 — shared icon service. Loads Win95 icon modules (SHELL32.DLL and, on
 * demand, others like USER.EXE / PIFMGR.DLL / MORICONS.DLL) once, resident, and
 * extracts RT_ICON resources scaled to a requested size (nearest-neighbour). */
#include <debian95/icons.h>
#include <debian95/img.h>
#include <debian95/nt.h>
#include <debian95/mm.h>

/* small resident cache of loaded modules, keyed by upper-case FAT file name */
#define MAX_MODS 8
static struct { const char *name; u8 *buf; u32 len; int tried; } g_mod[MAX_MODS];
static int g_nmod;

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; } return *a == *b;
}

static u8 *module(const char *name, u32 *len) {
    for (int i = 0; i < g_nmod; i++)
        if (streq(g_mod[i].name, name)) {
            if (!g_mod[i].buf && !g_mod[i].tried) {           /* retry a failed load once FS is up */
                g_mod[i].tried = 1;
                if (FsReadAlloc(name, &g_mod[i].buf, &g_mod[i].len) != 0) g_mod[i].buf = 0;
            }
            *len = g_mod[i].len; return g_mod[i].buf;
        }
    if (g_nmod >= MAX_MODS) return 0;
    int s = g_nmod++; g_mod[s].name = name; g_mod[s].tried = 1;
    if (FsReadAlloc(name, &g_mod[s].buf, &g_mod[s].len) != 0) g_mod[s].buf = 0;
    *len = g_mod[s].len; return g_mod[s].buf;
}

/* Fill out[size*size] with icon `id` from `mod`, scaled to size; transparent
 * pixels get `bg`. 0 = ok. */
int icon_get_from(const char *mod, u16 id, int size, u32 bg, u32 *out) {
    u32 n; u8 *m = module(mod, &n);
    if (!m) return -1;
    u32 *px; int w, h;
    if (res_icon(m, n, id, bg, &px, &w, &h) != 0) return -1;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            out[y * size + x] = px[(y * h / size) * w + (x * w / size)];
    kfree(px);
    return 0;
}

/* Back-compat: icons from SHELL32.DLL. */
int icon_get(u16 id, int size, u32 bg, u32 *out) {
    return icon_get_from("SHELL32.DLL", id, size, bg, out);
}

/* A 16x16 image from any COMCTL32 toolbar strip RT_BITMAP `res` (each 16 wide):
 * 120 = standard (Cut/Copy/Paste/.../New/Open/Save), 124 = view (Parent=8, ...),
 * 130 = history. Each strip is decoded once and cached; sub-image `index` scaled. */
int toolbar_img(u16 res, int index, int size, u32 bg, u32 *out) {
    static struct { u16 res; u32 *strip; int sw, sh, tried; } c[3]; static int nc;
    int slot = -1; for (int i = 0; i < nc; i++) if (c[i].res == res) slot = i;
    if (slot < 0) { if (nc >= 3) return -1; slot = nc++; c[slot].res = res; c[slot].strip = 0; c[slot].tried = 0; }
    if (!c[slot].strip && !c[slot].tried) { c[slot].tried = 1; u32 n; u8 *m = module("COMCTL32.DLL", &n);
        if (m) pe_bitmap(m, n, res, 0x00C0C0C0u, bg, &c[slot].strip, &c[slot].sw, &c[slot].sh); }
    if (!c[slot].strip || c[slot].sh < 16) return -1;
    int sw = c[slot].sw, count = sw / 16; if (index < 0 || index >= count) return -1;
    int x0 = index * 16;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            out[y * size + x] = c[slot].strip[(y * 16 / size) * sw + x0 + (x * 16 / size)];
    return 0;
}
int toolbar_std(int index, int size, u32 bg, u32 *out) { return toolbar_img(120, index, size, bg, out); }
