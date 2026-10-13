#!/bin/bash
# Turn a Debian95 Desktop-Edition Debian rootfs (from mk-debian.sh) into a bootable
# ext4 image + extracted kernel/initrd for QEMU. Fully unprivileged: fakeroot
# preserves root ownership through extract + mke2fs -d (the Buildroot way).
#   tools/mk-debian-image.sh [arch] [outdir] [size]
#     MKDEB_FRESH=1  force re-extract (otherwise reuse an existing rootdir so
#                    binaries added by build-debian95-*.sh survive re-imaging)
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
OUT="${2:-$HOME/debian95/debian-$ARCH}"
SIZE="${3:-auto}"   # auto = size the root image from the rootdir + headroom (never overflow)
FSTYPE="${FSTYPE:-btrfs}"   # btrfs (default; enables System Restore snapshots) or ext4
TAR="$OUT/rootfs-$ARCH.tar.gz"
RD="$OUT/rootdir"
ENVF="$OUT/.fakeroot.env"

# Extract once (or on MKDEB_FRESH). Reusing the rootdir lets build-debian95-wm.sh /
# build-debian95-desktop.sh add binaries that survive a re-image.
if [ ! -d "$RD" ] || [ "${MKDEB_FRESH:-0}" = "1" ]; then
  [ -f "$TAR" ] || { echo "no rootfs tarball: $TAR (run tools/mk-debian.sh $ARCH first)"; exit 1; }
  echo "== extracting rootfs under fakeroot (preserves root ownership) =="
  rm -rf "$RD"; mkdir -p "$RD"
  fakeroot -s "$ENVF" -- tar -C "$RD" --numeric-owner -xzf "$TAR"
  # hostname must resolve or startx's xauth fails ("bad display name debian95:0")
  cat > "$RD/etc/hosts" <<'H'
127.0.0.1   localhost
127.0.1.1   debian95
::1         localhost ip6-localhost ip6-loopback
H
fi

# btrfs root needs the btrfs driver (+ crc helpers) in the initrd, or the kernel
# can't mount it ("ALERT! UUID does not exist"). Force them in and regenerate once.
if [ "$FSTYPE" = "btrfs" ]; then
  echo "== ensuring btrfs drivers are in the initrd =="
  for m in btrfs crc32c_generic libcrc32c xxhash_generic; do
    grep -qx "$m" "$RD/etc/initramfs-tools/modules" 2>/dev/null || echo "$m" >> "$RD/etc/initramfs-tools/modules"
  done
  KVER="$(ls "$RD"/boot/vmlinuz-* | sed 's|.*/vmlinuz-||' | sort -V | tail -1)"
  printf 'nameserver 1.1.1.1\n' > "$RD/etc/resolv.conf" 2>/dev/null || true
  unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c \
    "export PATH=/usr/sbin:/usr/bin:/sbin:/bin; update-initramfs -u -k $KVER >/dev/null 2>&1" || \
    echo "   (initramfs regen warning - verify btrfs.ko is present)"
fi

echo "== pulling kernel + initrd out for QEMU -kernel/-initrd boot =="
cp "$RD"/boot/vmlinuz-*    "$OUT/vmlinuz"
cp "$RD"/boot/initrd.img-* "$OUT/initrd"

# Auto-size the image to the actual content + 1.2 GB of runtime/overlay headroom,
# so growing packages (retroarch, the Clippy model, ...) can never overflow it.
if [ "$SIZE" = "auto" ]; then
  used_mb="$(du -sm "$RD" | cut -f1)"
  SIZE="$(( used_mb + 1200 ))M"
fi
echo "== building $FSTYPE root image ($SIZE) =="
rm -f "$OUT/rootfs.ext4"
# btrfs needs the target file pre-sized; mke2fs takes the size as an argument.
[ "$FSTYPE" = "btrfs" ] && truncate -s "$SIZE" "$OUT/rootfs.ext4"
# We must NOT reuse the extract-time $ENVF for mke2fs. That db is keyed by inode,
# and files/dirs created AFTER the snapshot (our binaries, and crucially the
# getty@tty1.service.d autologin drop-in) reuse freed inode numbers and inherit a
# STALE mode from the db - including the wrong FILE TYPE, so mke2fs -d writes the
# drop-in DIR as a regular-file inode and the desktop never autostarts (also the
# old exec-bit-dropped quirk). Instead: capture the intended non-root ownership
# from $ENVF, then build the image in ONE fresh fakeroot session that reads real
# file types/modes, flattens ownership to root, and re-applies just those few
# non-root owners. Result: correct types + exec bits + ownership, no debugfs.
OWNMAP="$OUT/.ownmap"
fakeroot -i "$ENVF" -- find "$RD" \( ! -uid 0 -o ! -gid 0 \) -printf '%U:%G %p\n' > "$OWNMAP" 2>/dev/null || : > "$OWNMAP"
echo "   preserving $(wc -l < "$OWNMAP") non-root ownership entries from the base system"
fakeroot -- sh -c '
  RD="$1"; IMG="$2"; SIZE="$3"; MAP="$4"; FSTYPE="$5"
  chown -R 0:0 "$RD"
  while read -r og p; do [ -e "$p" ] && chown "$og" "$p"; done < "$MAP"
  if [ "$FSTYPE" = "btrfs" ]; then
    # --shrink: size the fs to the content (not the truncated device), else mkfs -r
    # grows the file to ~6G and the USB image balloons. The first-boot grow-root
    # service (btrfs filesystem resize max) expands it to fill the stick.
    mkfs.btrfs -q -f -L DEBIAN95-ROOT --shrink -r "$RD" "$IMG"
  else
    mke2fs -q -F -t ext4 -b 4096 -d "$RD" "$IMG" "$SIZE"
  fi
' _ "$RD" "$OUT/rootfs.ext4" "$SIZE" "$OWNMAP" "$FSTYPE"

echo
echo "Bootable Debian image ready in $OUT ($FSTYPE):  vmlinuz  initrd  rootfs.ext4"
echo "Boot it:  tools/run-debian.sh $ARCH"
