#!/usr/bin/env python3
# ext2 image builder (rev0, 1 KiB blocks, one block group) that lays out a real
# directory TREE with subdirectories, files and symlinks - enough to hold a small
# Linux rootfs. Files use direct + single- + double-indirect blocks (so a ~900 KiB
# busybox fits). Matches the reader in kernel/io/ext2.c.
#
#   mkext2.py out.img /abs/path=hostfile ... /abs/link='>target' ...
# A "/abs/path=>target" arg (value starts with '>') makes a symlink to <target>.
# Parent directories are created implicitly.
import sys, struct, time

BS = 1024
INODE_SZ = 128
DIRECT = 12
PPB = BS // 4                     # 256 pointers per indirect block
ROOT_INO = 2
FIRST_INO = 11

S_IFDIR = 0x4000; S_IFREG = 0x8000; S_IFLNK = 0xA000

class Node:
    def __init__(self, mode):
        self.mode = mode
        self.data = b""           # file contents / symlink target
        self.children = {}        # name -> Node (dirs)
        self.ino = 0
        self.blocks = []          # data block numbers
        self.ind = 0              # single-indirect block
        self.dind = 0             # double-indirect block
        self.iblocks = []         # blocks used by the (d)indirect metadata

def main():
    if len(sys.argv) < 2:
        print("usage: mkext2.py out.img /path=host ... /link=>target ..."); sys.exit(1)
    out = sys.argv[1]
    root = Node(S_IFDIR)

    def ensure_dir(parts):
        n = root
        for p in parts:
            if p not in n.children:
                n.children[p] = Node(S_IFDIR)
            n = n.children[p]
        return n

    for a in sys.argv[2:]:
        path, val = a.split("=", 1)
        parts = [p for p in path.split("/") if p]
        parent = ensure_dir(parts[:-1])
        name = parts[-1]
        if val.startswith(">"):
            node = Node(S_IFLNK); node.data = val[1:].encode()
        else:
            node = Node(S_IFREG); node.data = open(val, "rb").read()
        parent.children[name] = node

    # ---- collect nodes, assign inode numbers (root=2, rest from 11) ----
    dirs = []; nodes = []
    def collect(n):
        nodes.append(n)
        if n.mode == S_IFDIR: dirs.append(n)
        for _, c in sorted(n.children.items()):
            collect(c)
    root.ino = ROOT_INO
    collect(root)
    nxt = FIRST_INO
    for n in nodes:
        if n is root: continue
        n.ino = nxt; nxt += 1
    ninodes = max(64, ((nxt + 63)//64)*64)
    it_blocks = ninodes * INODE_SZ // BS

    # ---- block allocation ----
    itable_start = 5
    next_block = itable_start + it_blocks
    def alloc(n=1):
        nonlocal next_block
        f = next_block; next_block += n; return f

    # directory contents (entries) - assign one+ block(s) per dir
    def dir_bytes(n, self_ino, parent_ino):
        ents = [(self_ino, "."), (parent_ino, "..")]
        for nm, c in sorted(n.children.items()):
            ents.append((c.ino, nm))
        # pack into 1024-byte blocks; each rec_len rounds to 4, last fills block
        blocks = []; cur = bytearray();
        def ftype(node):
            return 2 if node.mode==S_IFDIR else (7 if node.mode==S_IFLNK else 1)
        i = 0
        while i < len(ents):
            ino, nm = ents[i]
            raw = nm.encode(); need = (8+len(raw)+3)&~3
            if len(cur)+need > BS:
                # pad last record of cur to end
                blocks.append(cur); cur = bytearray()
            ino_node = root if nm=="." or nm==".." else n.children.get(nm)
            ft = 2 if nm in(".","..") else ftype(n.children[nm])
            cur += struct.pack("<IHBB", ino, need, len(raw), ft) + raw + b"\x00"*(need-8-len(raw))
            i += 1
        blocks.append(cur)
        # extend each block's last record to fill the block
        out=[]
        for b in blocks:
            b=bytearray(b)
            # walk to last record, extend its rec_len
            off=0; last=0
            while off < len(b):
                rl=struct.unpack_from("<H",b,off+4)[0]; last=off; off+=rl
            struct.pack_into("<H",b,last+4, BS-last)
            b += b"\x00"*(BS-len(b))
            out.append(bytes(b))
        return out

    # parent tracking for ".."
    parent_of = {id(root): root}
    def setparents(n):
        for _,c in n.children.items():
            parent_of[id(c)] = n; setparents(c)
    setparents(root)

    img_dir_blocks = {}  # id(node) -> list of block numbers
    for n in dirs:
        pino = parent_of[id(n)].ino if n is not root else ROOT_INO
        content = dir_bytes(n, n.ino, pino)
        bns = [alloc() for _ in content]
        img_dir_blocks[id(n)] = (bns, content)

    # file data blocks (+ indirect metadata)
    for n in nodes:
        if n.mode != S_IFREG: continue
        nblk = (len(n.data)+BS-1)//BS
        n.blocks = [alloc() for _ in range(nblk)]
        if nblk > DIRECT:
            n.ind = alloc()
        if nblk > DIRECT+PPB:
            n.dind = alloc()
            n1 = (nblk - DIRECT - PPB + PPB - 1)//PPB   # number of 2nd-level blocks
            n.iblocks = [alloc() for _ in range(n1)]

    total_blocks = next_block
    img = bytearray(BS*total_blocks)
    def put(bn, b): img[bn*BS:bn*BS+len(b)] = b

    # write dir blocks
    for n in dirs:
        bns, content = img_dir_blocks[id(n)]
        for bn, c in zip(bns, content): put(bn, c)
        n.blocks = bns

    # write file data + indirect blocks
    for n in nodes:
        if n.mode != S_IFREG: continue
        for i, bn in enumerate(n.blocks):
            put(bn, n.data[i*BS:(i+1)*BS])
        if n.ind:
            ptrs = n.blocks[DIRECT:DIRECT+PPB]
            put(n.ind, b"".join(struct.pack("<I", b) for b in ptrs))
        if n.dind:
            rest = n.blocks[DIRECT+PPB:]
            # split rest into groups of PPB, each pointed to by a 2nd-level block
            l1 = []
            for gi, second in enumerate(n.iblocks):
                grp = rest[gi*PPB:(gi+1)*PPB]
                put(second, b"".join(struct.pack("<I", b) for b in grp))
                l1.append(second)
            put(n.dind, b"".join(struct.pack("<I", b) for b in l1))

    # ---- inode table ----
    def write_inode(n, links):
        idx = n.ino - 1
        pos = itable_start*BS + idx*INODE_SZ
        node = bytearray(INODE_SZ)
        size = len(n.data) if n.mode!=S_IFDIR else len(n.blocks)*BS
        i_block=[0]*15
        if n.mode==S_IFLNK and len(n.data) < 60:
            # fast symlink: target stored inline in i_block area
            tb = n.data + b"\x00"*(60-len(n.data))
            for i in range(15): i_block[i]=struct.unpack_from("<I",tb,i*4)[0]
            size = len(n.data)
        else:
            for i,b in enumerate(n.blocks[:DIRECT]): i_block[i]=b
            if n.ind: i_block[12]=n.ind
            if n.dind: i_block[13]=n.dind
        used512 = 0 if (n.mode==S_IFLNK and len(n.data)<60) else (((size+BS-1)//BS)*(BS//512))
        struct.pack_into("<HHIIIIIHHI", node, 0,
            n.mode|(0o755 if n.mode==S_IFDIR else 0o777 if n.mode==S_IFLNK else 0o755),
            0, size, int(time.time()), int(time.time()), int(time.time()), 0, 0, links, used512)
        for i in range(15): struct.pack_into("<I", node, 40+i*4, i_block[i])
        img[pos:pos+INODE_SZ]=node

    for n in nodes:
        if n.mode==S_IFDIR:
            links = 2 + sum(1 for c in n.children.values() if c.mode==S_IFDIR)
            write_inode(n, links)
        else:
            write_inode(n, 1)

    # ---- block bitmap (3), inode bitmap (4) ----
    bbmp=bytearray(BS)
    for b in range(total_blocks): bbmp[b>>3]|=1<<(b&7)
    for b in range(total_blocks, BS*8): bbmp[b>>3]|=1<<(b&7)
    put(3, bytes(bbmp))
    ibmp=bytearray(BS)
    used=set(range(1,FIRST_INO))|{n.ino for n in nodes}
    for i in used: ibmp[(i-1)>>3]|=1<<((i-1)&7)
    for i in range(ninodes, BS*8): ibmp[i>>3]|=1<<(i&7)
    put(4, bytes(ibmp))

    free_inodes = ninodes - len(used)
    gd = struct.pack("<IIIHHH", 3,4,itable_start, 0, free_inodes, len(dirs))
    put(2, gd + b"\x00"*(32-len(gd)))

    sb=bytearray(BS)
    struct.pack_into("<IIIIIIIiII", sb, 0, ninodes, total_blocks, 0, 0, free_inodes, 1, 0, 0, total_blocks, ninodes)
    struct.pack_into("<I", sb, 40, ninodes)
    struct.pack_into("<H", sb, 56, 0xEF53)
    struct.pack_into("<H", sb, 58, 1)
    struct.pack_into("<I", sb, 76, 0)
    put(1, bytes(sb))

    open(out,"wb").write(img)
    print(f"wrote {out}: {total_blocks} blocks ({total_blocks*BS} bytes), "
          f"{len(nodes)} nodes ({len(dirs)} dirs)")

if __name__ == "__main__":
    main()
