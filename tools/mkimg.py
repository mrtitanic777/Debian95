#!/usr/bin/env python3
# Debian95 - image importer. Converts an image to the Debian95 .LGB format so the
# kernel can use it as the desktop wallpaper. Supports BMP (24-bit), PPM (P6),
# and PNG (8-bit RGB/RGBA). Also generates an original default wallpaper.
#
#   mkimg.py auto    <in> <out.lgb>
#   mkimg.py default <out.lgb> [W H]
import sys, struct, zlib

def write_lgb(path, w, h, pixels):     # pixels: list of 0x00RRGGBB
    out = bytearray(b"LGB1")
    out += struct.pack("<HH", w, h)
    out += struct.pack("<%dI" % (w * h), *pixels)
    open(path, "wb").write(out)
    print(f"{path}: {w}x{h} wallpaper")

def load_ppm(d):
    assert d[:2] == b"P6"
    i, toks = 2, []
    while len(toks) < 3:
        while d[i] in b" \t\r\n": i += 1
        if d[i:i+1] == b"#":
            while d[i] not in b"\r\n": i += 1
            continue
        s = i
        while d[i] not in b" \t\r\n": i += 1
        toks.append(int(d[s:i]))
    i += 1
    w, h, _ = toks
    px = [(d[i+3*k] << 16) | (d[i+3*k+1] << 8) | d[i+3*k+2] for k in range(w*h)]
    return w, h, px

def load_bmp(d):
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    assert bpp in (24, 32), "BMP must be 24- or 32-bit"
    bypp = bpp // 8
    rowsz = ((w * bypp + 3) // 4) * 4
    flip = h > 0
    h = abs(h)
    px = [0] * (w * h)
    for y in range(h):
        srcy = (h - 1 - y) if flip else y
        base = off + srcy * rowsz
        for x in range(w):
            b = d[base + x*bypp]; g = d[base + x*bypp+1]; r = d[base + x*bypp+2]
            px[y*w + x] = (r << 16) | (g << 8) | b
    return w, h, px

def load_png(d):
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    i = 8; w = h = bitd = ctype = None; idat = b""
    while i < len(d):
        ln = struct.unpack_from(">I", d, i)[0]; typ = d[i+4:i+8]; body = d[i+8:i+8+ln]
        if typ == b"IHDR":
            w, h, bitd, ctype = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        i += 12 + ln
    assert bitd == 8 and ctype in (2, 6), "PNG must be 8-bit RGB/RGBA"
    ch = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * ch
    px = [0] * (w * h)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        for x in range(stride):
            a = line[x-ch] if x >= ch else 0
            b = prev[x]
            c = prev[x-ch] if x >= ch else 0
            if f == 1: line[x] = (line[x] + a) & 0xFF
            elif f == 2: line[x] = (line[x] + b) & 0xFF
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c; pa = abs(p-a); pb = abs(p-b); pc = abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        for x in range(w):
            r = line[x*ch]; g = line[x*ch+1]; bb = line[x*ch+2]
            px[y*w + x] = (r << 16) | (g << 8) | bb
        prev = line
    return w, h, px

def do_default(path, w=1024, h=768):
    px = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            t = (x + y) / (w + h)                       # diagonal 0..1
            r = int(0x0A + 0x06 * (1 - t))
            g = int(0x5A * (1 - t) + 0x14 * t)
            b = int(0x64 * (1 - t) + 0x40 * t)
            if (x % 64 == 0) or (y % 64 == 0):          # faint grid
                r = max(0, r - 6); g = max(0, g - 10); b = max(0, b - 10)
            px[y*w + x] = (r << 16) | (g << 8) | b
    write_lgb(path, w, h, px)

if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "default":
        w = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
        h = int(sys.argv[4]) if len(sys.argv) > 4 else 768
        do_default(sys.argv[2], w, h)
    elif mode == "auto":
        d = open(sys.argv[2], "rb").read()
        if d[:2] == b"BM": w, h, px = load_bmp(d)
        elif d[:2] == b"P6": w, h, px = load_ppm(d)
        elif d[:8] == b"\x89PNG\r\n\x1a\n": w, h, px = load_png(d)
        else: sys.exit("unsupported image (use BMP/PPM/PNG)")
        write_lgb(sys.argv[3], w, h, px)
    else:
        sys.exit("usage: mkimg.py auto <in> <out.lgb> | default <out.lgb> [W H]")
