/* Debian95 — native decoders for Windows image formats (BMP, ICO/CUR DIBs). */
#ifndef DEBIAN95_IMG_H
#define DEBIAN95_IMG_H

#include <debian95/types.h>
#include <debian95/cursor.h>

/* Decode a .BMP into a kmalloc'd RGB (0x00RRGGBB) buffer. 0 = ok. */
int bmp_decode(const u8 *data, u32 len, u32 **out, int *w, int *h);

/* Decode a PNG / QOI into a kmalloc'd RGB (0x00RRGGBB) buffer. 0 = ok. */
int png_decode(const u8 *data, u32 len, u32 **out, int *w, int *h);
int qoi_decode(const u8 *data, u32 len, u32 **out, int *w, int *h);
int gif_decode(const u8 *data, u32 len, u32 **out, int *w, int *h);
int jpeg_decode(const u8 *data, u32 len, u32 **out, int *w, int *h);
/* all frames of an animated GIF: frames[count] RGB buffers, delays[] in centiseconds */
int gif_decode_all(const u8 *data, u32 len, u32 ***frames, int **delays, int *count, int *w, int *h);

/* DEFLATE / zlib inflate into a caller buffer (returns 0, sets *written). */
int inflate_raw(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *written);
int inflate_zlib(const u8 *src, u32 srclen, u8 *dst, u32 dstcap, u32 *written);

/* Decode a .CUR cursor (with hotspot + transparency) into `out`. 0 = ok. */
int cur_decode(const u8 *data, u32 len, Debian95Cursor *out);

/* Decode a .ICO (largest image) into a kmalloc'd RGB buffer; transparent pixels
 * are filled with `bg`. 0 = ok. */
int ico_decode(const u8 *data, u32 len, u32 bg, u32 **out, int *w, int *h);

/* Extract RT_ICON `res_id` from a real PE .DLL/.EXE into a kmalloc'd RGB buffer
 * (transparent -> bg). 0 = ok. */
int pe_icon(const u8 *data, u32 len, u16 res_id, u32 bg, u32 **out, int *w, int *h);
/* Same, from a 16-bit NE module (user.exe/pifmgr.dll/moricons.dll). */
int ne_icon(const u8 *data, u32 len, u16 res_id, u32 bg, u32 **out, int *w, int *h);
/* Dispatch on the container: PE or NE. 0 = ok. */
int res_icon(const u8 *data, u32 len, u16 res_id, u32 bg, u32 **out, int *w, int *h);
/* Extract an RT_BITMAP (e.g. the COMCTL32 toolbar strip); pixels == transp -> bg. */
int pe_bitmap(const u8 *data, u32 len, u16 res_id, u32 transp, u32 bg, u32 **out, int *w, int *h);

/* Decode a DIB body (BITMAPINFOHEADER + palette + bits) of `rows` scanlines,
 * bottom-up, into `out` (row 0 = top). 0 = ok. */
int dib_decode(const u8 *hdr, const u8 *pal, const u8 *bits, int w, int rows,
               int bpp, u32 *out);

#endif /* DEBIAN95_IMG_H */
