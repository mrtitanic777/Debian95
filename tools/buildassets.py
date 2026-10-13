#!/usr/bin/env python3
# Debian95 - build the data disk from whatever is in assets/. Just drop files in
# that folder and run `make`; fonts are auto-converted, everything else is
# embedded verbatim. No converter commands to remember.
import os, glob, subprocess, sys

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
BUILD  = os.path.join(ROOT, "build")
PY     = sys.executable
os.makedirs(BUILD, exist_ok=True)

def sh(*args):
    subprocess.check_call([PY] + list(args), cwd=ROOT)

def to83(base):
    name, _, ext = base.rpartition(".")
    if not name:
        name, ext = ext, ""
    name = "".join(c for c in name.upper() if c.isalnum())[:8] or "FILE"
    ext = "".join(c for c in ext.upper() if c.isalnum())[:3]
    return name + ("." + ext if ext else "")

lgf = os.path.join(BUILD, "FONT.LGF")

# font selection order:
#   1. a native Windows assets/ui.fon (decoded by the OS at boot)   <- preferred
#   2. a prebuilt assets/font.lgf, or a font source (bdf/psf/ttf/otf) converted
#   3. the built-in 8x8 glyphs
import shutil
cands = glob.glob(os.path.join(ASSETS, "*"))
ui_fon = os.path.join(ASSETS, "ui.fon")
con_fon = os.path.join(ASSETS, "console.fon")
have_ui_fon = os.path.exists(ui_fon)
have_con_fon = os.path.exists(con_fon)

if not have_ui_fon:                       # only build the .LGF fallback if no .fon
    ready_lgf = None; font_src = None
    for c in sorted(cands):
        b = os.path.basename(c).lower()
        if b == "font.lgf": ready_lgf = c
        elif b in ("font.bdf", "font.psf", "font.ttf", "font.otf") and not font_src: font_src = c
    if ready_lgf:
        print("assets: using prebuilt font", os.path.basename(ready_lgf)); shutil.copyfile(ready_lgf, lgf)
    elif font_src:
        print("assets: converting font", os.path.basename(font_src)); sh("tools/mkfont.py", "auto", font_src, lgf)
    else:
        sh("tools/mkfont.py", "builtin", lgf)

con_lgf = os.path.join(BUILD, "CONSOLE.LGF")
con_src = os.path.join(ASSETS, "console.lgf")
have_console = (not have_con_fon) and os.path.exists(con_src)
if have_console:
    print("assets: using prebuilt console font console.lgf"); shutil.copyfile(con_src, con_lgf)

# wallpaper: ship assets/wall.bmp RAW (decoded natively by the OS); else the
# default generated .LGB
wall_bmp = os.path.join(ASSETS, "wall.bmp")
lgb = os.path.join(BUILD, "WALL.LGB")
have_wall_bmp = os.path.exists(wall_bmp)
if not have_wall_bmp:
    sh("tools/mkimg.py", "default", lgb)

# embed the fonts, wallpaper, built programs, plus every other file in assets/
pairs = []
if have_ui_fon:
    pairs.append("UIFONT.FON=" + ui_fon); print("assets: shipping ui.fon for native decode")
else:
    pairs.append("FONT.LGF=" + lgf)
if have_con_fon:
    pairs.append("CONFONT.FON=" + con_fon); print("assets: shipping console.fon for native decode")
elif have_console:
    pairs.append("CONSOLE.LGF=" + con_lgf)
if have_wall_bmp:
    pairs.append("WALL.BMP=" + wall_bmp); print("assets: shipping wall.bmp for native decode")
else:
    pairs.append("WALL.LGB=" + lgb)
for prog in ("INIT.ELF", "READER.ELF", "SPIN.ELF", "TEST.EXE"):
    p = os.path.join(BUILD, prog)
    if os.path.exists(p):
        pairs.append(prog + "=" + p)
used = set()
for c in sorted(cands):
    if os.path.isdir(c):
        continue
    b = os.path.basename(c)
    if b.lower().endswith((".bdf", ".psf", ".ttf", ".otf", ".bmp", ".ppm", ".png", ".lgf")) \
       or b.lower() in ("ui.fon", "console.fon", "wall.bmp"):
        continue                      # handled explicitly above / sources not shipped raw
    if b.lower() in ("readme.md", ".gitkeep"):
        continue
    name = to83(b)
    if name in used:
        continue
    used.add(name)
    pairs.append(name + "=" + c)

img = os.path.join(BUILD, "fat32.img")
sh("tools/mkfat32.py", img, *pairs)
