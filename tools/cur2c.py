#!/usr/bin/env python3
# Debian95 - convert Windows .CUR cursors into a compact C source of 2-bit
# cursor bitmaps (0=transparent 1=black 2=white 3=gray), cropped to the used
# area with the hotspot preserved.  Usage:
#   cur2c.py out.c name1=path1.cur name2=path2.cur ...
import sys, struct

def load_cur(path):
    d = open(path, 'rb').read()
    _, _, _ = struct.unpack('<HHH', d[:6])
    w, h, ncol, r, hx, hy, size, off = struct.unpack('<BBBBHHII', d[6:22])
    bsz, bw, bh, planes, bpp = struct.unpack('<IiiHH', d[off:off+16])
    ah, aw = bh // 2, bw
    paloff = off + bsz
    ncolors = (1 << bpp) if bpp <= 8 else 0
    pal = [tuple(d[paloff+i*4+2-k] for k in range(3)) for i in range(ncolors)]
    xoroff = paloff + ncolors*4
    rowbytes = ((bw*bpp + 31)//32)*4
    androw = ((bw + 31)//32)*4
    andoff = xoroff + rowbytes*ah
    pix = [[0]*aw for _ in range(ah)]
    for y in range(ah):
        sy = ah-1-y
        for x in range(aw):
            amask = (d[andoff + sy*androw + (x>>3)] >> (7-(x&7))) & 1
            if bpp == 1:
                v = (d[xoroff + sy*rowbytes + (x>>3)] >> (7-(x&7))) & 1
                col = pal[v] if pal else ((255,255,255) if v else (0,0,0))
            elif bpp == 4:
                xb = d[xoroff + sy*rowbytes + (x>>1)]
                col = pal[(xb>>4) if (x&1)==0 else (xb&0xF)]
            elif bpp == 8:
                col = pal[d[xoroff + sy*rowbytes + x]]
            else:
                b0 = xoroff + sy*rowbytes + x*(bpp//8); col = (d[b0+2], d[b0+1], d[b0])
            if amask == 0:
                lum = (col[0]+col[1]+col[2])//3
                pix[y][x] = 1 if lum < 64 else (2 if lum > 192 else 3)
            else:
                pix[y][x] = 0
    return pix, aw, ah, hx, hy

def crop(pix, aw, ah, hx, hy):
    x0, y0, x1, y1 = aw, ah, 0, 0
    for y in range(ah):
        for x in range(aw):
            if pix[y][x]:
                x0 = min(x0,x); y0 = min(y0,y); x1 = max(x1,x); y1 = max(y1,y)
    if x1 < x0: return [[0]], 1, 1, 0, 0
    out = [[pix[y][x] for x in range(x0, x1+1)] for y in range(y0, y1+1)]
    return out, x1-x0+1, y1-y0+1, hx-x0, hy-y0

if __name__ == '__main__':
    items = []
    for arg in sys.argv[2:]:
        name, path = arg.split('=', 1)
        pix, aw, ah, hx, hy = load_cur(path)
        pix, w, h, hx, hy = crop(pix, aw, ah, hx, hy)
        items.append((name, pix, w, h, hx, hy))
    with open(sys.argv[1], 'w') as f:
        f.write('/* Debian95 - cursor bitmaps converted from Windows .CUR by tools/cur2c.py.\n')
        f.write(' * pix: 2-bit codes 0=transparent 1=black 2=white 3=gray, row-major. */\n')
        f.write('#include <debian95/cursor.h>\n\n')
        for name, pix, w, h, hx, hy in items:
            f.write(f'static const unsigned char {name}_pix[] = {{\n')
            for row in pix:
                f.write('  ' + ','.join(str(v) for v in row) + ',\n')
            f.write('};\n')
            f.write(f'const Debian95Cursor cursor_{name} = {{ {w}, {h}, {hx}, {hy}, {name}_pix }};\n\n')
    print('wrote', sys.argv[1], 'with', ', '.join(i[0] for i in items))
