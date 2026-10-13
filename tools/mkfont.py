#!/usr/bin/env python3
# Debian95 - font importer. Converts a font into the Debian95 .LGF format so the
# kernel can load it from disk. You supply the font; this just repackages it.
#
#   mkfont.py builtin  <out.lgf>          # proportional font from the built-in glyphs
#   mkfont.py bdf <in.bdf> <out.lgf>      # X11 BDF (proportional bitmap font)
#   mkfont.py psf <in.psf> <out.lgf>      # PC Screen Font (fixed-width console font)
#
# BDF and PSF fonts are openly available in abundance; drop one in and convert.
import sys, struct, re, math

FIRST = 0x20
LAST  = 0x7E                       # inclusive

def write_lgf(path, height, maxw, proportional, glyphs):
    # glyphs: dict code -> (advance, [row_bits ...]) for FIRST..LAST
    count = LAST - FIRST + 1
    out = bytearray(b"LGF1")
    out += struct.pack("<HHHH", height, FIRST, count, maxw)
    out += struct.pack("<BBH", 1 if proportional else 0, 0, 0)   # 16-byte header
    for code in range(FIRST, LAST + 1):
        adv, rows = glyphs.get(code, (maxw // 2, []))
        out += struct.pack("<BB", adv & 0xFF, 0)
        for r in range(height):
            v = rows[r] if r < len(rows) else 0
            out += struct.pack("<H", v & 0xFFFF)
    open(path, "wb").write(out)
    print(f"{path}: {count} glyphs, {maxw}x{height}, {'proportional' if proportional else 'fixed'}")

# ---- built-in 8x8 -> proportional ----
def do_builtin(out):
    src = open("lib/rtl/font8x8.c").read()
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)   # strip block comments
    src = re.sub(r"//.*", "", src)                     # and line comments
    bytes_ = [int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", src)]
    if len(bytes_) < 96 * 8:
        sys.exit("could not parse font8x8.c")
    glyphs = {}
    for i in range(96):
        rows = bytes_[i * 8:i * 8 + 8]
        width = 0
        for b in rows:
            for c in range(8):
                if b & (1 << c):
                    width = max(width, c + 1)
        adv = 4 if width == 0 else width + 1
        glyphs[FIRST + i] = (adv, rows)
    write_lgf(out, 8, 8, True, glyphs)

# ---- BDF (proportional) ----
def do_bdf(inp, out):
    lines = open(inp, encoding="latin-1").read().splitlines()
    ascent = descent = None
    fbb = None
    for l in lines:
        if l.startswith("FONT_ASCENT"):  ascent = int(l.split()[1])
        elif l.startswith("FONT_DESCENT"): descent = int(l.split()[1])
        elif l.startswith("FONTBOUNDINGBOX"): fbb = list(map(int, l.split()[1:5]))
    if ascent is None or descent is None:
        if not fbb: sys.exit("BDF missing metrics")
        ascent = fbb[1] + fbb[3]; descent = -fbb[3]
    height = ascent + descent
    if height > 32: sys.exit(f"font too tall ({height} > 32)")
    maxw = min(16, fbb[0]) if fbb else 16

    glyphs = {}
    i = 0
    while i < len(lines):
        if lines[i].startswith("STARTCHAR"):
            enc = dw = bbx = None; bitmap = []
            i += 1
            while i < len(lines) and not lines[i].startswith("ENDCHAR"):
                t = lines[i].split()
                if not t: i += 1; continue
                if t[0] == "ENCODING": enc = int(t[1])
                elif t[0] == "DWIDTH": dw = int(t[1])
                elif t[0] == "BBX": bbx = list(map(int, t[1:5]))
                elif t[0] == "BITMAP":
                    i += 1
                    while i < len(lines) and not lines[i].startswith("ENDCHAR"):
                        bitmap.append(lines[i].strip()); i += 1
                    break
                i += 1
            if enc is not None and FIRST <= enc <= LAST and bbx:
                bw, bh, bxoff, byoff = bbx
                bpr = max(1, (bw + 7) // 8)
                rows = [0] * height
                for j, hexrow in enumerate(bitmap[:bh]):
                    val = int(hexrow, 16) if hexrow else 0
                    cell_row = (ascent - (byoff + bh)) + j
                    if 0 <= cell_row < height:
                        for col in range(bw):
                            if val & (1 << (bpr * 8 - 1 - col)):
                                bit = bxoff + col
                                if 0 <= bit < 16:
                                    rows[cell_row] |= (1 << bit)
                glyphs[enc] = (dw if dw is not None else bw, rows)
        i += 1
    write_lgf(out, height, maxw, True, glyphs)

# ---- PSF (fixed) ----
def do_psf(inp, out):
    d = open(inp, "rb").read()
    if d[0] == 0x36 and d[1] == 0x04:                 # PSF1
        mode = d[2]; height = d[3]; width = 8; off = 4; charsize = height
    elif d[:4] == b"\x72\xb5\x4a\x86":                # PSF2
        ver, hdr, flags, length, charsize, height, width = struct.unpack("<IIIIIII", d[4:32])
        off = hdr
    else:
        sys.exit("not a PSF font")
    if height > 32: sys.exit(f"font too tall ({height})")
    maxw = min(16, width); bpr = max(1, (width + 7) // 8)
    glyphs = {}
    for code in range(FIRST, LAST + 1):
        base = off + code * charsize
        rows = [0] * height
        for r in range(height):
            val = 0
            for b in range(bpr):
                val = (val << 8) | d[base + r * bpr + b]
            for col in range(min(width, 16)):
                if val & (1 << (bpr * 8 - 1 - col)):
                    rows[r] |= (1 << col)
        glyphs[code] = (maxw, rows)
    write_lgf(out, height, maxw, False, glyphs)

# ---- TrueType/OpenType (rendered to a 1-bit bitmap font via Pillow) ----
def do_ttf(inp, out, px):
    from PIL import ImageFont, Image, ImageDraw
    px = int(px)
    font = ImageFont.truetype(inp, px)
    asc, desc = font.getmetrics()
    height = asc + desc
    if height > 24:                      # keep the row table compact
        height = 24
    glyphs = {}; maxw = 0
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        try: adv = round(font.getlength(ch))
        except Exception: adv = px // 2
        img = Image.new("L", (24, height), 0)
        ImageDraw.Draw(img).text((0, 0), ch, fill=255, font=font)
        rows = [0] * height; w = 0
        px_img = img.load()
        for y in range(height):
            bits = 0
            for x in range(16):
                if px_img[x, y] >= 128:
                    bits |= (1 << x); w = max(w, x + 1)
            rows[y] = bits
        maxw = max(maxw, w)
        if adv <= 0: adv = max(4, w)
        if adv > 16: adv = 16
        glyphs[code] = (adv, rows)
    write_lgf(out, height, min(16, maxw), True, glyphs)

def sniff(path):
    with open(path, "rb") as f:
        head = f.read(16)
    if head[:2] == b"\x36\x04" or head[:4] == b"\x72\xb5\x4a\x86":
        return "psf"
    if head[:9] == b"STARTFONT" or path.lower().endswith(".bdf"):
        return "bdf"
    if path.lower().endswith(".psf"):
        return "psf"
    if path.lower().endswith((".ttf", ".otf")):
        return "ttf"
    return None

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mode = sys.argv[1]
    if mode == "builtin":
        do_builtin(sys.argv[2])
    elif mode == "auto":
        kind = sniff(sys.argv[2])
        if kind == "bdf":   do_bdf(sys.argv[2], sys.argv[3])
        elif kind == "psf": do_psf(sys.argv[2], sys.argv[3])
        elif kind == "ttf": do_ttf(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else 13)
        else:
            sys.exit("unrecognized font format: " + sys.argv[2])
    elif mode == "bdf": do_bdf(sys.argv[2], sys.argv[3])
    elif mode == "psf": do_psf(sys.argv[2], sys.argv[3])
    elif mode == "ttf": do_ttf(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else 13)
    else: sys.exit("unknown mode: " + mode)
