#!/usr/bin/env python3
# Debian95 - enumerate every RT_ICON in a set of Windows PE/NE modules and lay
# them out as labelled contact sheets, so we have a browsable catalogue of the
# real Win95 icons we can pull into the shell.
#
#   icon_catalog.py <out_dir> <module.dll> [<module2> ...]
#
# Handles both 32-bit PE (.dll/.exe/.cpl) and 16-bit NE (.exe/.dll) containers.
# Each RT_ICON is a DIB (BITMAPINFOHEADER + colour XOR plane + 1bpp AND mask);
# we decode 1/4/8/24/32 bpp and pick the ~32px colour strike per icon id.
import sys, struct, os

def u16(b,o): return b[o] | (b[o+1]<<8)
def u32(b,o): return b[o] | (b[o+1]<<8) | (b[o+2]<<16) | (b[o+3]<<24)

# ---- locate RT_ICON resource blobs (returns {id: (fileoffset, size)}) ----
def pe_icons(d):
    e=u32(d,0x3c)
    nsec=u16(d,e+6); szopt=u16(d,e+20); opt=e+24
    rrva=u32(d,opt+96+2*8); sec=opt+szopt; secs=[]
    for i in range(nsec):
        o=sec+i*40; va=u32(d,o+12); vs=u32(d,o+8); raw=u32(d,o+20); rs=u32(d,o+16)
        secs.append((va,max(vs,rs),raw))
    def r2f(rva):
        for va,sz,raw in secs:
            if va<=rva<va+sz: return raw+(rva-va)
        return None
    base=r2f(rrva)
    def entries(off):
        nn,ni=struct.unpack_from('<HH',d,base+off+12)
        return [struct.unpack_from('<II',d,base+off+16+i*8) for i in range(nn+ni)]
    out={}
    for nid,ptr in entries(0):
        if nid!=3: continue
        for iid,p2 in entries(ptr&0x7fffffff):
            o=p2&0x7fffffff
            _,p3=entries(o)[0]; o3=p3&0x7fffffff
            rva,size=struct.unpack_from('<II',d,base+o3)
            out[iid]=(r2f(rva),size)
    return out

def ne_icons(d):
    e=u32(d,0x3c); rsrc=e+u16(d,e+0x24)
    shift=u16(d,rsrc); o=rsrc+2; out={}
    while True:
        rtype=u16(d,o)
        if rtype==0: break
        count=u16(d,o+2); o+=8
        for i in range(count):
            off=u16(d,o)<<shift; ln=u16(d,o+2)<<shift; rid=u16(d,o+6)
            if rtype==0x8003:                    # RT_ICON
                out[rid & 0x7fff]=(off,ln)
            o+=12
    return out

def icons_of(d):
    if d[:2]!=b'MZ': return {}
    sig=d[u32(d,0x3c):u32(d,0x3c)+2]
    if sig==b'PE': return pe_icons(d)
    if sig==b'NE': return ne_icons(d)
    return {}

# ---- decode a DIB icon image at file offset `o` -> (w,h,[(r,g,b,a)]) ----
def decode(d,o):
    hs=u32(d,o); w=struct.unpack_from('<i',d,o+4)[0]; h=struct.unpack_from('<i',d,o+8)[0]//2
    bpp=u16(d,o+14)
    ncol=u32(d,o+32) or (1<<bpp if bpp<=8 else 0)
    pal=o+hs; px=pal+ncol*4
    xrb=((w*bpp+31)//32)*4; arb=((w+31)//32)*4
    img=[(0,0,0,0)]*(w*h)
    for y in range(h):
        ro=px+(h-1-y)*xrb; ao=px+h*xrb+(h-1-y)*arb
        for x in range(w):
            if bpp==8: idx=d[ro+x]; b,g,r=d[pal+idx*4],d[pal+idx*4+1],d[pal+idx*4+2]
            elif bpp==4: bb=d[ro+x//2]; idx=(bb>>4) if x%2==0 else (bb&0xf); b,g,r=d[pal+idx*4],d[pal+idx*4+1],d[pal+idx*4+2]
            elif bpp==1: bb=d[ro+x//8]; idx=(bb>>(7-x%8))&1; b,g,r=d[pal+idx*4],d[pal+idx*4+1],d[pal+idx*4+2]
            elif bpp==24: b,g,r=d[ro+x*3],d[ro+x*3+1],d[ro+x*3+2]
            elif bpp==32: b,g,r=d[ro+x*4],d[ro+x*4+1],d[ro+x*4+2]
            else: b=g=r=0
            am=(d[ao+x//8]>>(7-x%8))&1
            img[y*w+x]=(r,g,b,0 if am else 255)
    return w,h,img

def sheet(cells, cols, cell, pad, label_h):
    rows=(len(cells)+cols-1)//cols
    W=cols*(cell+pad)+pad; H=rows*(cell+pad+label_h)+pad
    S=[(150,150,150)]*(W*H)
    def put(px,py,c):
        if 0<=px<W and 0<=py<H: S[py*W+px]=c
    # tiny 3x5 digit font for id labels
    F={'0':["111","101","101","101","111"],'1':["010","110","010","010","111"],
       '2':["111","001","111","100","111"],'3':["111","001","111","001","111"],
       '4':["101","101","111","001","001"],'5':["111","100","111","001","111"],
       '6':["111","100","111","101","111"],'7':["111","001","010","010","010"],
       '8':["111","101","111","101","111"],'9':["111","101","111","001","111"],
       ':':["0","1","0","1","0"],'x':["000","101","010","101","000"],' ':["0","0","0","0","0"],
       'a':["000","011","101","101","011"],'m':["000","110","111","101","101"],
       'p':["000","110","101","110","100"],'i':["1","0","1","1","1"],'f':["011","100","110","100","100"],
       'g':["011","101","011","001","110"],'r':["000","101","110","100","100"],'u':["000","101","101","101","011"],
       's':["011","100","010","001","110"],'h':["100","100","110","101","101"],'e':["010","101","111","100","011"],
       'l':["110","010","010","010","111"],'o':["010","101","101","101","010"],'n':["000","110","101","101","101"],
       'c':["011","100","100","100","011"],'.':["0","0","0","0","1"],'t':["010","111","010","010","011"],
       'w':["000","101","101","111","101"],'d':["001","001","011","101","011"],'v':["000","101","101","101","010"]}
    def text(x,y,s,col=(0,0,0)):
        for ch in s:
            g=F.get(ch,F[' ']); gw=len(g[0])
            for r in range(5):
                for c in range(gw):
                    if g[r][c]=='1': put(x+c,y+r,col)
            x+=gw+1
    for k,(lbl,img) in enumerate(cells):
        r=k//cols; c=k%cols
        ox=pad+c*(cell+pad); oy=pad+r*(cell+pad+label_h)
        if img:
            w,h,pixels=img; sc=cell//max(w,h) or 1
            xo=ox+(cell-w*sc)//2; yo=oy+(cell-h*sc)//2
            for y in range(h):
                for x in range(w):
                    rr,gg,bb,a=pixels[y*w+x]
                    if a>127:
                        for dy in range(sc):
                            for dx in range(sc): put(xo+x*sc+dx,yo+y*sc+dy,(rr,gg,bb))
        text(ox+1,oy+cell+2,lbl)
    return W,H,S

def write_ppm(path,W,H,S):
    with open(path,'wb') as f:
        f.write(b'P6\n%d %d\n255\n'%(W,H))
        for c in S: f.write(bytes(c))

if __name__=="__main__":
    out=sys.argv[1]; os.makedirs(out,exist_ok=True)
    grand=[]
    for path in sys.argv[2:]:
        d=open(path,'rb').read(); ics=icons_of(d)
        name=os.path.basename(path).lower()
        cells=[]
        for iid in sorted(ics):
            o,sz=ics[iid]
            try:
                w,h,img=decode(d,o)
                # prefer 32px colour: NE/PE store several sizes under distinct ids already
                cells.append((str(iid), (w,h,img)))
            except Exception:
                cells.append((str(iid), None))
        if not cells:
            print(f"{name}: no icons"); continue
        W,H,S=sheet(cells,16,34,3,7)
        pp=os.path.join(out,name.replace('.','_')+'.ppm')
        write_ppm(pp,W,H,S)
        print(f"{name}: {len(cells)} icons -> {pp}")
        grand.append((name,len(cells)))
    print("TOTAL", sum(n for _,n in grand), "icons across", len(grand), "modules")
