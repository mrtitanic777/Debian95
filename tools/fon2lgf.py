#!/usr/bin/env python3
# Debian95 - extract a bitmap strike from a Windows .FON font (an NE/PE resource
# container of FNT glyph bitmaps) into the Debian95 .LGF format.
#
#   fon2lgf.py <in.fon> <out.lgf> [target_pixel_height]
#
# .FON files hold the original pixel-perfect bitmaps (e.g. MS Sans Serif). We
# parse the NE resource table, read each RT_FONT (FNT) strike, pick the one
# closest to the requested height, and repack its glyphs row-major for .LGF.
import sys, struct

FIRST, LAST = 0x20, 0x7E

def u16(b, o): return b[o] | (b[o+1] << 8)
def u32(b, o): return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | (b[o+3] << 24)

def find_fnt_resources(d):
    """Return list of (offset, length) of RT_FONT resources in an NE .FON."""
    if d[:2] != b'MZ': sys.exit("not an MZ/NE file")
    ne = u32(d, 0x3C)
    if d[ne:ne+2] != b'NE': sys.exit("not an NE executable (.FON expected)")
    rsrctab = ne + u16(d, ne + 0x24)
    shift = u16(d, rsrctab)
    o = rsrctab + 2
    fonts = []
    while True:
        rtype = u16(d, o)
        if rtype == 0: break
        count = u16(d, o + 2)
        o += 8                                   # rtTypeID,count,reserved(4)
        for i in range(count):
            rnOffset = u16(d, o); rnLength = u16(d, o + 2)
            if rtype == 0x8008:                  # RT_FONT
                fonts.append((rnOffset << shift, rnLength << shift))
            o += 12                              # NAMEINFO
    return fonts

def parse_fnt(d, base):
    ver = u16(d, base + 0x00)
    pix_h = u16(d, base + 0x58)
    first = d[base + 0x5F]; last = d[base + 0x60]
    ctab = base + (0x94 if ver >= 0x300 else 0x76)
    entsz = 6 if ver >= 0x300 else 4
    glyphs = {}
    for code in range(FIRST, LAST + 1):
        if code < first or code > last: continue
        e = ctab + (code - first) * entsz
        cw = u16(d, e)
        coff = u32(d, e + 2) if entsz == 6 else u16(d, e + 2)
        rows = [0] * pix_h
        # Windows FNT glyph: ceil(cw/8) byte-planes, each plane H bytes (one per
        # scanline, row-major within a plane); plane p holds pixels x in
        # [p*8..p*8+7], MSB = leftmost. byte(r,x) = coff + (x>>3)*H + r.
        for r in range(pix_h):
            for x in range(min(cw, 16)):
                if d[base + coff + (x >> 3) * pix_h + r] & (0x80 >> (x & 7)):
                    rows[r] |= (1 << x)
        glyphs[code] = (cw, rows)
    return pix_h, glyphs, ver

def write_lgf(path, height, glyphs):
    maxw = 0
    for _, (cw, _) in glyphs.items(): maxw = max(maxw, min(cw, 16))
    out = bytearray(b"LGF1")
    out += struct.pack("<HHHH", height, FIRST, LAST - FIRST + 1, maxw)
    out += struct.pack("<BBH", 1, 0, 0)
    for code in range(FIRST, LAST + 1):
        cw, rows = glyphs.get(code, (maxw // 2, [0] * height))
        out += struct.pack("<BB", min(cw, 16) & 0xFF, 0)
        for r in range(height):
            out += struct.pack("<H", (rows[r] if r < len(rows) else 0) & 0xFFFF)
    open(path, "wb").write(out)
    print(f"{path}: {LAST-FIRST+1} glyphs, {maxw}x{height}")

if __name__ == "__main__":
    if len(sys.argv) < 3: sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    target = int(sys.argv[3]) if len(sys.argv) > 3 else 13
    strikes = []
    for off, ln in find_fnt_resources(d):
        try:
            h, g, ver = parse_fnt(d, off)
            strikes.append((h, g, ver))
        except Exception as ex:
            pass
    if not strikes: sys.exit("no FNT strikes found")
    strikes.sort(key=lambda s: abs(s[0] - target))
    heights = sorted(s[0] for s in strikes)
    print("available heights:", heights, "-> picking", strikes[0][0])
    h, g, ver = strikes[0]
    write_lgf(sys.argv[2], h, g)
