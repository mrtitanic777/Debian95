#!/usr/bin/env python3
# Minimal ISO9660 (level 1, 8.3 names) image builder - dependency-free, matching
# the reader in kernel/io/iso9660.c. Files go in the root directory only, each as
# one contiguous 2048-byte-sector extent. Enough to be mounted by a real OS too.
#
#   mkiso.py out.iso NAME.EXT=path [NAME2.EXT=path2 ...]
import sys

SECTOR = 2048

def both16(v):  # both-endian 16-bit (LE then BE)
    return v.to_bytes(2, "little") + v.to_bytes(2, "big")
def both32(v):
    return v.to_bytes(4, "little") + v.to_bytes(4, "big")

def dtime():    # 7-byte directory record timestamp (years since 1900, ...)
    return bytes([126, 1, 1, 0, 0, 0, 0])

def dir_record(name_bytes, lba, size, is_dir):
    # assemble one directory record; length is padded to even
    body = bytearray()
    body.append(0)                 # [0] length (filled below)
    body.append(0)                 # [1] ext attr length
    body += both32(lba)            # [2..9] extent location
    body += both32(size)           # [10..17] data length
    body += dtime()                # [18..24]
    body.append(0x02 if is_dir else 0x00)  # [25] flags
    body.append(0)                 # [26] file unit size
    body.append(0)                 # [27] interleave gap
    body += both16(1)              # [28..31] volume sequence number
    body.append(len(name_bytes))   # [32] name length
    body += name_bytes             # [33..] name
    if len(body) % 2:              # pad name to even
        body.append(0)
    body[0] = len(body)
    return bytes(body)

def main():
    if len(sys.argv) < 2:
        print("usage: mkiso.py out.iso NAME=path ..."); sys.exit(1)
    out = sys.argv[1]
    files = []
    for a in sys.argv[2:]:
        name, path = a.split("=", 1)
        with open(path, "rb") as f:
            files.append((name.upper(), f.read()))

    # layout: 0-15 system area, 16 PVD, 17 terminator, 18 L-path, 19 M-path,
    # 20 root dir, 21+ file data (each file sector-aligned)
    root_lba = 20
    data_lba = 21
    layout = []
    lba = data_lba
    for name, data in files:
        nsec = max(1, (len(data) + SECTOR - 1) // SECTOR)
        layout.append((name, data, lba, len(data)))
        lba += nsec
    total_sectors = lba

    # ---- root directory extent (sector 20) ----
    root = bytearray()
    root += dir_record(b"\x00", root_lba, SECTOR, True)   # "."  (self)
    root += dir_record(b"\x01", root_lba, SECTOR, True)   # ".." (parent = self)
    for name, data, flba, fsize in layout:
        idn = (name + ";1").encode("ascii")
        rec = dir_record(idn, flba, fsize, False)
        if len(root) % SECTOR + len(rec) > SECTOR:        # records never cross a sector
            root += b"\x00" * (SECTOR - len(root) % SECTOR)
        root += rec
    root += b"\x00" * (SECTOR - len(root) % SECTOR)
    root_size = len(root)

    # ---- Primary Volume Descriptor (sector 16) ----
    pvd = bytearray(b"\x00" * SECTOR)
    pvd[0] = 1                                            # type = primary
    pvd[1:6] = b"CD001"; pvd[6] = 1                       # id + version
    pvd[8:40] = b" " * 32                                 # system id
    vid = b"DEBIAN95_CD"
    pvd[40:72] = (vid + b" " * 32)[:32]                   # volume id
    pvd[80:88] = both32(total_sectors)                   # volume space size
    pvd[120:124] = both16(1)                             # volume set size
    pvd[124:128] = both16(1)                             # volume sequence number
    pvd[128:132] = both16(SECTOR)                        # logical block size
    pvd[132:140] = both32(SECTOR)                        # path table size (1 sector)
    pvd[140:144] = (18).to_bytes(4, "little")            # L path table location
    pvd[148:152] = (19).to_bytes(4, "big")              # M path table location
    pvd[156:156 + len(dir_record(b"\x00", root_lba, root_size, True))] = \
        dir_record(b"\x00", root_lba, root_size, True)   # root directory record
    pvd[881] = 1                                          # file structure version

    # ---- Volume Descriptor Set Terminator (sector 17) ----
    term = bytearray(b"\x00" * SECTOR)
    term[0] = 255; term[1:6] = b"CD001"; term[6] = 1

    # ---- path tables (minimal: one entry for the root) ----
    def path_table(be):
        pt = bytearray()
        pt.append(1)                                     # dir id length (root = 1 byte, 0x00)
        pt.append(0)                                     # ext attr length
        pt += root_lba.to_bytes(4, "big" if be else "little")  # extent
        pt += (1).to_bytes(2, "big" if be else "little")       # parent dir number
        pt += b"\x00"                                    # dir id (root)
        pt.append(0)                                     # pad to even
        return bytes(pt) + b"\x00" * (SECTOR - len(pt))

    # ---- assemble the image ----
    img = bytearray()
    img += b"\x00" * (SECTOR * 16)                        # system area
    img += pvd
    img += term
    img += path_table(False)                             # L (little-endian)
    img += path_table(True)                              # M (big-endian)
    img += root                                          # already sector-padded
    assert len(img) == SECTOR * data_lba, (len(img), SECTOR * data_lba)
    for name, data, flba, fsize in layout:
        pad = (-len(data)) % SECTOR
        img += data + b"\x00" * pad

    with open(out, "wb") as f:
        f.write(img)
    print(f"wrote {out}: {total_sectors} sectors ({total_sectors*SECTOR} bytes), "
          f"{len(files)} files: " + ", ".join(n for n, _ in files))

if __name__ == "__main__":
    main()
