#!/usr/bin/env python3
# Debian95 - generate a FAT32 image with files (multi-cluster). Extra assets can
# be embedded from the command line so you can import any files you want:
#   mkfat32.py build/fat32.img FONT.LGF=build/FONT.LGF WALL.LGB=build/wall.lgb
import struct, sys

BPS   = 512
SPC   = 1
RSVD  = 32
NFATS = 2
FATSZ = 1100
CLUS  = 140000
TOTAL = CLUS + RSVD + NFATS * FATSZ
CLUSTER = SPC * BPS
EOC = 0x0FFFFFFF

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/fat32.img"

# base files + any name=path pairs on the command line
files = [
    ("HELLO.TXT",  b"Hello from the Debian95 FAT32 driver!\n"),
    ("README.TXT", b"Debian95 P6: Io device model + ATA PIO + FAT32 reader.\n"),
]
for arg in sys.argv[2:]:
    name, _, path = arg.partition("=")
    with open(path, "rb") as f:
        files.append((name.upper(), f.read()))

img = bytearray(TOTAL * BPS)
fat0 = RSVD * BPS
data_lba = RSVD + NFATS * FATSZ

def set_fat(cluster, value):
    struct.pack_into("<I", img, fat0 + cluster * 4, value & 0x0FFFFFFF)

def cluster_lba(c):
    return data_lba + (c - 2) * SPC

# ---- boot sector / BPB ----
bs = img
bs[0:3]   = b"\xEB\x58\x90"
bs[3:11]  = b"DEBIAN95 "
struct.pack_into("<H", bs, 11, BPS)
bs[13]    = SPC
struct.pack_into("<H", bs, 14, RSVD)
bs[16]    = NFATS
bs[21]    = 0xF8
struct.pack_into("<H", bs, 24, 32)
struct.pack_into("<H", bs, 26, 8)
struct.pack_into("<I", bs, 32, TOTAL)
struct.pack_into("<I", bs, 36, FATSZ)
struct.pack_into("<I", bs, 44, 2)          # root cluster
struct.pack_into("<H", bs, 48, 1)
struct.pack_into("<H", bs, 50, 6)
bs[64]    = 0x80
bs[66]    = 0x29
struct.pack_into("<I", bs, 67, 0x10A0105)
bs[71:82] = b"DEBIAN95    "
bs[82:90] = b"FAT32   "
struct.pack_into("<H", bs, 510, 0xAA55)

# FSInfo
fsi = 1 * BPS
struct.pack_into("<I", img, fsi + 0,   0x41615252)
struct.pack_into("<I", img, fsi + 484, 0x61417272)
struct.pack_into("<I", img, fsi + 488, 0xFFFFFFFF)
struct.pack_into("<I", img, fsi + 492, 0xFFFFFFFF)
struct.pack_into("<I", img, fsi + 508, 0xAA550000)
img[6 * BPS:7 * BPS] = img[0:BPS]

set_fat(0, 0x0FFFFFF8)
set_fat(1, EOC)

# root directory may span several clusters (16 entries per 512-byte cluster);
# reserve + chain them so a large root doesn't collide with file data.
root_entries = 1 + len(files)                        # volume label + one dirent per file
ROOT_NCLU = max(1, (root_entries * 32 + CLUSTER - 1) // CLUSTER)
for k in range(ROOT_NCLU):
    set_fat(2 + k, EOC if k == ROOT_NCLU - 1 else 2 + k + 1)

def dirent(name8_3, attr, cluster, size):
    e = bytearray(32)
    base, _, ext = name8_3.partition(".")
    e[0:11] = (base.upper().ljust(8) + ext.upper().ljust(3)).encode()[:11]
    e[11]   = attr
    struct.pack_into("<H", e, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return e

root = bytearray()
label = bytearray(32); label[0:11] = b"DEBIAN95    "; label[11] = 0x08
root += label

next_cluster = 2 + ROOT_NCLU        # file data starts after the root-dir clusters
for name, content in files:
    nclu = max(1, (len(content) + CLUSTER - 1) // CLUSTER)
    first = next_cluster
    for k in range(nclu):
        c = next_cluster; next_cluster += 1
        set_fat(c, EOC if k == nclu - 1 else c + 1)
        off = cluster_lba(c) * BPS
        chunk = content[k * CLUSTER:(k + 1) * CLUSTER]
        img[off:off + len(chunk)] = chunk
    root += dirent(name, 0x20, first, len(content))

# root-dir clusters (2 .. 2+ROOT_NCLU-1) are contiguous, so write the buffer across them
img[cluster_lba(2) * BPS:cluster_lba(2) * BPS + len(root)] = root

with open(OUT, "wb") as f:
    f.write(img)
print(f"wrote {OUT}: {TOTAL} sectors, {len(files)} files: " +
      ", ".join(n for n, _ in files))
