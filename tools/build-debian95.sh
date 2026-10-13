#!/bin/bash
# Debian95 — one-command build. Produces the bootable, signed USB image from source:
#   Debian rootfs (firmware + Secure Boot shim/grub)  ->  the Debian95 shell + WM
#   ->  system integration (Plymouth, audio, persistence, X session)  ->  ext4  ->
#   GPT/UEFI image.  Everything is defined by debian95.conf.
#
#   tools/build-debian95.sh [arch]        arch = amd64 (default) | arm64 | i386
#
# Output:  $HOME/debian95/debian-<arch>/debian95-usb.img   (flash with Rufus/dd/etc.)
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HOME/debian95/debian-$ARCH"
cd "$REPO"

echo "==> [1/8] Debian rootfs (firmware blobs + signed boot chain)"
bash tools/mk-debian.sh "$ARCH"
echo "==> [2/8] extract the rootfs (fresh)"
MKDEB_FRESH=1 bash tools/mk-debian-image.sh "$ARCH"
echo "==> [3/8] build the reparenting window manager"
bash tools/build-debian95-wm.sh "$ARCH"
echo "==> [4/8] build the Win95 desktop shell"
rm -f usermode/desktop/debian95-desktop-sdl usermode/desktop/debian95-desktop
bash tools/build-debian95-desktop.sh "$ARCH"
echo "==> [5/8] system integration (Plymouth splash / audio / USB persistence)"
bash tools/setup-plymouth.sh    "$ARCH" || echo "    (plymouth step skipped)"
bash tools/setup-audio.sh       "$ARCH" || echo "    (audio step skipped)"
bash tools/setup-persistence.sh "$ARCH" || echo "    (persistence step skipped)"
bash tools/stage-ai.sh          "$ARCH" || echo "    (AI/Clippy brain step skipped)"
echo "==> [6/8] install the X session (autologin + desktop, safe-mode + rescue TTYs)"
bash tools/debian-xsession.sh "$ARCH"
echo "==> [7/8] pack the ext4 root image"
rm -f "$OUT/rootfs.ext4"
bash tools/mk-debian-image.sh "$ARCH"
echo "==> [8/8] assemble the signed, bootable GPT/UEFI image"
bash tools/mk-usb.sh "$ARCH"

echo
echo "Debian95 build complete."
echo "  Image:  $OUT/debian95-usb.img"
echo "  Verify: tools/run-usb.sh $ARCH        (boots it under OVMF in QEMU)"
echo "  Flash:  Rufus / balenaEtcher, or  dd if=$OUT/debian95-usb.img of=/dev/sdX bs=4M status=progress"
