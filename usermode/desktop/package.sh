#!/usr/bin/env bash
# Package the Debian95 desktop as a portable download for any Linux desktop.
# Builds the SDL binary, bundles the UI assets + a launcher + a .desktop entry +
# an installer, and produces dist/debian95-desktop-<arch>.tar.gz.
#
#   usermode/desktop/package.sh
# (run on Linux; needs SDL2 dev to build - the target machine only needs libsdl2)
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ARCH="$(uname -m)"
VER="1.0"
NAME="debian95-desktop-$VER-$ARCH"
STAGE="$HERE/dist/$NAME"
DIST="$HERE/dist"

echo "[1/4] building SDL desktop ($ARCH)"
make -C "$HERE" CC="${CC:-cc}" REPO="$REPO" debian95-desktop-sdl >/dev/null

echo "[2/4] staging $STAGE"
rm -rf "$STAGE"; mkdir -p "$STAGE/bin" "$STAGE/share/debian95" "$STAGE/share/applications"
cp "$HERE/debian95-desktop-sdl" "$STAGE/bin/debian95-desktop"
# UI assets (upper-case names for the FAT-era Fs* callers): the font, every
# wallpaper, the shell32 icon library, and the codec test images.
[ -f "$REPO/build/FONT.LGF" ] && cp "$REPO/build/FONT.LGF" "$STAGE/share/debian95/FONT.LGF" || true
for a in "$REPO"/assets/*.bmp "$REPO"/assets/*.BMP \
         "$REPO/assets/shell32.dll" "$REPO/assets/user.exe" \
         "$REPO/assets/pifmgr.dll" "$REPO/assets/moricons.dll" "$REPO/assets/mmsys.cpl" "$REPO/assets/mspaint.exe" \
         "$REPO/assets/test.png" "$REPO/assets/test.jpg" "$REPO/assets/test.gif"; do
  [ -f "$a" ] && cp "$a" "$STAGE/share/debian95/$(basename "$a" | tr a-z A-Z)" || true
done

# launcher: sets the data dir relative to install location, runs the binary
cat > "$STAGE/bin/debian95" <<'LAUNCH'
#!/bin/sh
DIR="$(cd "$(dirname "$0")" && pwd)"
export DEBIAN95_DATA="${DEBIAN95_DATA:-$DIR/../share/debian95}"
exec "$DIR/debian95-desktop" "$@"
LAUNCH
chmod +x "$STAGE/bin/debian95" "$STAGE/bin/debian95-desktop"

cat > "$STAGE/share/applications/debian95.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=Debian95 Desktop
Comment=A Win95-style desktop, window manager and apps
Exec=debian95
Terminal=false
Categories=System;Utility;
DESK

# installer (into a PREFIX, default /usr/local)
cat > "$STAGE/install.sh" <<'INST'
#!/bin/sh
set -e
PREFIX="${PREFIX:-/usr/local}"
DIR="$(cd "$(dirname "$0")" && pwd)"
echo "Installing Debian95 desktop into $PREFIX ..."
install -Dm755 "$DIR/bin/debian95-desktop" "$PREFIX/bin/debian95-desktop"
mkdir -p "$PREFIX/share/debian95"; cp -f "$DIR"/share/debian95/* "$PREFIX/share/debian95/" 2>/dev/null || true
install -Dm644 "$DIR/share/applications/debian95.desktop" "$PREFIX/share/applications/debian95.desktop"
cat > "$PREFIX/bin/debian95" <<EOF
#!/bin/sh
export DEBIAN95_DATA="\${DEBIAN95_DATA:-$PREFIX/share/debian95}"
exec "$PREFIX/bin/debian95-desktop" "\$@"
EOF
chmod +x "$PREFIX/bin/debian95"
echo "Done. Run:  debian95     (needs libsdl2 - your distro's SDL2 runtime)"
INST
chmod +x "$STAGE/install.sh"

cat > "$STAGE/README.txt" <<EOF
Debian95 Desktop $VER ($ARCH)

A Win95-style desktop, window manager, terminal and applications, as a single
portable Linux program. Renders in an SDL2 window on any Linux desktop
(X11/Wayland); the embedded/framebuffer build ships in the Debian95 OS image.

Run without installing:
    ./bin/debian95

Install system-wide:
    sudo ./install.sh          # into /usr/local  (PREFIX=... to change)
    debian95

Requires the SDL2 runtime (libsdl2-2.0), available in every distro's package
manager (apt install libsdl2-2.0-0 / dnf install SDL2 / pacman -S sdl2).
EOF

echo "[3/4] tarring"
mkdir -p "$DIST"
tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
echo "[4/4] done"
ls -la "$DIST/$NAME.tar.gz"
