/* Debian95 — QOI decoder (the "Quite OK Image" format). Small, fast, lossless. */
#include <debian95/types.h>
#include <debian95/mm.h>

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

int qoi_decode(const u8 *data, u32 len, u32 **out, int *ow, int *oh) {
    if (len < 14 || data[0] != 'q' || data[1] != 'o' || data[2] != 'i' || data[3] != 'f') return -1;
    int w = (int)be32(data + 4), h = (int)be32(data + 8);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return -1;

    u32 *px = (u32 *)kmalloc((u32)w * h * 4);
    if (!px) return -1;
    u8 idx_r[64] = {0}, idx_g[64] = {0}, idx_b[64] = {0}, idx_a[64] = {0};
    int r = 0, g = 0, b = 0, a = 255;
    u32 pos = 14, n = (u32)w * h, i = 0, run = 0;
    while (i < n) {
        if (run > 0) { run--; }
        else if (pos < len) {
            int op = data[pos++];
            if (op == 0xFE) { if (pos + 3 > len) break; r = data[pos]; g = data[pos+1]; b = data[pos+2]; pos += 3; }
            else if (op == 0xFF) { if (pos + 4 > len) break; r = data[pos]; g = data[pos+1]; b = data[pos+2]; a = data[pos+3]; pos += 4; }
            else if ((op & 0xC0) == 0x00) { int ix = op & 0x3F; r = idx_r[ix]; g = idx_g[ix]; b = idx_b[ix]; a = idx_a[ix]; }
            else if ((op & 0xC0) == 0x40) { r += ((op >> 4) & 3) - 2; g += ((op >> 2) & 3) - 2; b += (op & 3) - 2; }
            else if ((op & 0xC0) == 0x80) { if (pos >= len) break; int b2 = data[pos++]; int dg = (op & 0x3F) - 32;
                r += dg + ((b2 >> 4) & 0x0F) - 8; g += dg; b += dg + (b2 & 0x0F) - 8; }
            else { run = op & 0x3F; }   /* QOI_OP_RUN: run+1 pixels, current one included */
            r &= 0xFF; g &= 0xFF; b &= 0xFF; a &= 0xFF;
            int hpos = (r * 3 + g * 5 + b * 7 + a * 11) & 63;
            idx_r[hpos] = (u8)r; idx_g[hpos] = (u8)g; idx_b[hpos] = (u8)b; idx_a[hpos] = (u8)a;
        } else break;
        px[i++] = ((u32)r << 16) | ((u32)g << 8) | b;
    }
    *out = px; *ow = w; *oh = h;
    return 0;
}
