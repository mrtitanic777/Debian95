# Debian95 assets — drop files here

Anything you put in this folder is placed on the Debian95 data disk the next time
you run `make`. No commands to remember.

- **A font** — drop a `.bdf` (proportional bitmap) or `.psf` (fixed console)
  font here. It is auto-detected and becomes the system font. Name it `font.bdf`
  / `font.psf` to make it the chosen one when several exist. If no font is
  present, Debian95 uses its built-in default.
- **Any other file** — text, data, or a Debian95-format asset — is copied onto
  the disk under an 8.3 name and can be read with `FsReadAlloc("NAME", ...)`.

Then just:

```bash
make
```

(TTF/OTF aren't converted here because Pillow isn't installed; convert to BDF
first with a tool like `otf2bdf`, then drop the `.bdf` in.)
