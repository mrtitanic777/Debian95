#!/usr/bin/env python3
# Minimal binary-PPM (P6) -> PNG converter (stdlib only: zlib + struct).
import sys, zlib, struct

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", "not a P6 PPM"
    # parse header tokens (magic, width, height, maxval)
    idx = 2
    toks = []
    while len(toks) < 3:
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if data[idx:idx+1] == b"#":
            while idx < len(data) and data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n":
            idx += 1
        toks.append(int(data[start:idx]))
    idx += 1  # single whitespace after maxval
    w, h, _ = toks
    return w, h, data[idx:idx + w * h * 3]

def write_png(path, w, h, rgb):
    def chunk(tag, body):
        c = tag + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # filter: none
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

src, dst = sys.argv[1], sys.argv[2]
w, h, rgb = read_ppm(src)
write_png(dst, w, h, rgb)
print(f"{dst}: {w}x{h}")
