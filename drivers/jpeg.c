/* Debian95 — baseline JPEG decode via picojpeg (public domain, integer-only).
 * Wraps picojpeg's MCU-at-a-time decoder into the same RGB-buffer API as the
 * other image codecs. picojpeg pulls input through a callback and needs no
 * libc / malloc / FPU, so it drops straight into the freestanding kernel. */
#include <debian95/types.h>
#include <debian95/mm.h>
#include "picojpeg.h"

static const u8 *g_src; static u32 g_len, g_pos;

static unsigned char need_bytes(unsigned char *pBuf, unsigned char buf_size,
                                unsigned char *pBytes_read, void *cb) {
    (void)cb;
    u32 n = buf_size;
    if (g_pos + n > g_len) n = g_len - g_pos;
    for (u32 i = 0; i < n; i++) pBuf[i] = g_src[g_pos + i];
    g_pos += n;
    *pBytes_read = (unsigned char)n;
    return 0;
}

int jpeg_decode(const u8 *data, u32 len, u32 **out, int *ow, int *oh) {
    g_src = data; g_len = len; g_pos = 0;
    pjpeg_image_info_t info;
    if (pjpeg_decode_init(&info, need_bytes, 0, 0) != 0) return -1;
    int w = info.m_width, h = info.m_height;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return -1;

    u32 *rgb = (u32 *)kmalloc((u32)w * h * 4);
    if (!rgb) return -1;
    for (int i = 0; i < w * h; i++) rgb[i] = 0;

    int bpr = info.m_MCUWidth / 8, bpc = info.m_MCUHeight / 8;   /* blocks across / down per MCU */
    int mcu_x = 0, mcu_y = 0;
    for (;;) {
        unsigned char rc = pjpeg_decode_mcu();
        if (rc != 0) break;                    /* PJPG_NO_MORE_BLOCKS or an error: stop */
        for (int by = 0; by < bpc; by++)
            for (int bx = 0; bx < bpr; bx++) {
                int boff = by * 128 + bx * 64;             /* picojpeg block layout */
                int px0 = mcu_x * info.m_MCUWidth + bx * 8;
                int py0 = mcu_y * info.m_MCUHeight + by * 8;
                for (int row = 0; row < 8; row++)
                    for (int col = 0; col < 8; col++) {
                        int dx = px0 + col, dy = py0 + row;
                        if (dx >= w || dy >= h) continue;
                        int bi = boff + row * 8 + col;
                        u32 r, g, b;
                        if (info.m_comps == 1) { r = g = b = info.m_pMCUBufR[bi]; }
                        else { r = info.m_pMCUBufR[bi]; g = info.m_pMCUBufG[bi]; b = info.m_pMCUBufB[bi]; }
                        rgb[dy * w + dx] = (r << 16) | (g << 8) | b;
                    }
            }
        if (++mcu_x == info.m_MCUSPerRow) { mcu_x = 0; if (++mcu_y == info.m_MCUSPerCol) break; }
    }
    *out = rgb; *ow = w; *oh = h;
    return 0;
}
