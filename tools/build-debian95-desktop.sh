#!/bin/bash
# Build the full Debian95 Win95 desktop (the SDL build of subsystems/win32 - taskbar,
# Start menu, wallpaper, icons, all the apps) INTO an extracted Debian rootfs, and
# stage its assets. Unprivileged, via an unshare userns chroot (see build-debian95-wm.sh
# for why fakechroot can't be used here). Run mk-debian-image.sh afterwards to re-image.
#   tools/build-debian95-desktop.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"; SRC="$RD/root/debian95-src"
[ -d "$RD" ] || { echo "no extracted rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

echo "== copying Debian95 desktop source into the rootfs =="
rm -rf "$SRC"; mkdir -p "$SRC"
for d in subsystems drivers lib third_party sdk usermode/desktop tools; do
  mkdir -p "$SRC/$d"; cp -r "$REPO/$d/." "$SRC/$d/" 2>/dev/null || true
done

echo "== staging desktop assets (fonts / wallpaper / icons / sounds) into /usr/share/debian95 =="
if [ -d "$REPO/build/dev-share" ]; then
  mkdir -p "$RD/usr/share/debian95"; cp -r "$REPO/build/dev-share/." "$RD/usr/share/debian95/"
else
  # assemble a fresh data dir with the shared stager
  bash "$REPO/tools/stage-debian95-share.sh" "$REPO" "$RD/usr/share/debian95"
fi
# Always ship the AUTHORITATIVE default settings from assets/ - the dev-share
# shortcut above can carry stale dev runtime state (e.g. a wallpaper picked while
# testing), and that must never leak into a shipped image.
[ -f "$REPO/assets/SETTINGS.INI" ] && cp -f "$REPO/assets/SETTINGS.INI" "$RD/usr/share/debian95/SETTINGS.INI" || true

# Stage 32x32 desktop icons for any downloaded apps present in the rootfs (VLC,
# Chromium), as keyed BMPs (magenta = the shell's transparent key) so their real
# artwork shows on the desktop / in Start > Programs. Skipped if PIL or the source
# icon is missing (the shell then draws a fallback tile). Needs python3-pil.
if python3 -c 'import PIL' 2>/dev/null; then
  python3 - "$RD" <<'PY'
import sys, glob, os
from PIL import Image
RD = sys.argv[1]; dst = os.path.join(RD, 'usr/share/debian95'); os.makedirs(dst, exist_ok=True)
def stage(pats, out):
    cands = []
    for pat in pats: cands += glob.glob(os.path.join(RD, pat))
    order = {'48x48':0,'64x64':1,'32x32':2,'128x128':3,'256x256':4}
    def score(p):
        for s in order:
            if s in p: return order[s]
        return 9
    for p in sorted(set(cands), key=score):
        try: im = Image.open(p).convert('RGBA')
        except Exception: continue
        im = im.resize((32, 32), Image.LANCZOS)
        # Key on the source ALPHA, not by flattening onto magenta: a soft (anti-
        # aliased) edge pixel blended over the magenta key becomes a near-magenta
        # pink that is not EXACTLY the key colour, so the shell's blit_key draws it
        # as a pink halo. Threshold alpha to a hard mask instead - every pixel is
        # either fully kept or fully transparent (the magenta key), no fringe.
        sp = im.load()
        bg = Image.new('RGB', (32, 32), (255, 0, 255)); bp = bg.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = sp[x, y]
                if a >= 128:
                    if (r, g, b) == (255, 0, 255): b = 254   # don't let a real magenta pixel key out
                    bp[x, y] = (r, g, b)
        bg.save(os.path.join(dst, out), 'BMP'); print('  icon', out, '<-', os.path.relpath(p, RD)); return
stage(['usr/share/icons/hicolor/*/apps/vlc.png', 'usr/share/pixmaps/vlc*.png'], 'vlc.bmp')
stage(['usr/share/icons/hicolor/*/apps/chromium.png', 'usr/share/pixmaps/chromium*.png'], 'chrome.bmp')
stage(['usr/share/icons/hicolor/*/apps/pcmanfm.png', 'usr/share/icons/*/*/apps/system-file-manager.png'], 'pcmanfm.bmp')
stage(['usr/share/icons/hicolor/*/apps/gpicview.png', 'usr/share/pixmaps/gpicview*.png'], 'gpicview.bmp')
stage(['usr/share/icons/hicolor/*/apps/org.xfce.mousepad.png', 'usr/share/icons/hicolor/*/apps/mousepad.png', 'usr/share/pixmaps/mousepad*.png'], 'mousepad.bmp')
stage(['usr/share/icons/hicolor/*/apps/xarchiver.png', 'usr/share/pixmaps/xarchiver*.png'], 'xarchiver.bmp')
PY
else echo "  (python3-pil not found: skipping app-icon staging)"; fi

printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$RD/etc/resolv.conf"
echo "== install SDL2 + compile debian95-desktop-sdl (unshare chroot) =="
unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c '
  set -e
  export DEBIAN_FRONTEND=noninteractive PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
  apt-get update -o APT::Sandbox::User=root -qq
  apt-get install -y --no-install-recommends -o APT::Sandbox::User=root libsdl2-dev pkg-config make gcc libc6-dev
  cd /root/debian95-src/usermode/desktop
  # drop any binary copied in from the host source tree - a native make there links the
  # newer host glibc; keeping it makes make skip the rebuild and install the wrong
  # binary (a GLIBC_2.xx-not-found crash-loop). Force a fresh chroot build.
  rm -f debian95-desktop-sdl debian95-desktop plugins/*.so
  make debian95-desktop-sdl REPO=/root/debian95-src CC=gcc
  install -m755 debian95-desktop-sdl /usr/local/bin/debian95-desktop-sdl
  echo "built: $(ls -l /usr/local/bin/debian95-desktop-sdl)"
  # hot-swappable app modules -> /usr/lib/debian95/apps (the shell scans this dir)
  make plugins REPO=/root/debian95-src CC=gcc
  install -d /usr/lib/debian95/apps
  install -m755 plugins/*.so /usr/lib/debian95/apps/
  echo "modules: $(ls -1 /usr/lib/debian95/apps/ | tr "\n" " ")"
'
echo "Done. Set the session (tools/debian-xsession.sh $ARCH) and re-image (tools/mk-debian-image.sh $ARCH)."
