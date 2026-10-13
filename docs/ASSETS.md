# Importing assets into Debian95

## The easy way: the `assets/` folder

Drop files into the `assets/` folder and run `make`. That's it.

- A `.bdf` or `.psf` font is auto-detected and becomes the system font.
- Any other file is copied onto the data disk under an 8.3 name.

```bash
cp ~/Downloads/myfont.bdf assets/     # or drag it in from your file manager
make                                  # font is converted + embedded automatically
```

No converter flags to remember. If `assets/` has no font, Debian95 uses its
built-in default. The rest of this file documents the underlying tools, which
you only need if you want manual control.

## Fonts (`.LGF`)

The kernel loads `FONT.LGF` from the root of the data volume at boot and uses it
as the system font (falling back to the built-in 8×8 if it is absent).

`.LGF` is Debian95's own simple bitmap-font format (see `sdk/include/debian95/font.h`
for the layout). Convert a font to it with `tools/mkfont.py`:

```bash
# proportional font generated from Debian95's own built-in glyphs (the default)
python tools/mkfont.py builtin build/FONT.LGF

# an X11 BDF proportional bitmap font (open fonts are widely available)
python tools/mkfont.py bdf  myfont.bdf  build/FONT.LGF

# a PC Screen Font console font (fixed width)
python tools/mkfont.py psf  myfont.psf  build/FONT.LGF
```

Then rebuild the disk so it is embedded:

```bash
python tools/mkfat32.py build/fat32.img FONT.LGF=build/FONT.LGF
```

Or just `make` — the Makefile regenerates the default `FONT.LGF` and embeds it.
To use your own font permanently, point the `FONT.LGF` rule at your converter
output.

- **BDF** gives proportional bitmap fonts (the "UI font" look). Many openly
  licensed BDF fonts exist (e.g. the X11 bitmap font collections).
- **PSF** gives crisp fixed-width console fonts (many openly licensed ones exist,
  e.g. Terminus).
- Constraints: height ≤ 32 px, glyph width ≤ 16 px, ASCII range 0x20–0x7E.

## Embedding any file

`mkfat32.py` takes `NAME=path` pairs, so any file can be placed on the disk:

```bash
python tools/mkfat32.py build/fat32.img FONT.LGF=build/FONT.LGF NOTES.TXT=notes.txt
```

Read it from the kernel with `FsReadAlloc("NAME", &buf, &size)`.

> Bring your own assets. Debian95 ships only original defaults; the import path is
> here so you can load whatever fonts/files you have the rights to use.
