#!/bin/bash
# Debian95 installer — copy the running live system onto an internal disk and make
# it bootable. Block-copies the pristine ESP + root partitions from the live
# device (so it is independent of the live session's encrypted-/data overlay),
# gives the installed root a fresh UUID, repoints GRUB at it, and lays down a raw
# DEBIAN95-DATA partition the first boot will LUKS-encrypt.
#
#   debian95-install [/dev/TARGET]      (auto-detects the target if omitted)
#
# DESTRUCTIVE: every partition on the target disk is erased.
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
say() { echo ">> $*"; }
die() { echo "!! $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run as root"

# ---- identify the live disk (do not install onto ourselves) ----
LIVE_ROOT=$(findmnt -no SOURCE /) || die "cannot find the live root"
LIVE_DISK=/dev/$(lsblk -no pkname "$LIVE_ROOT")
LIVE_ESP=$(lsblk -lno NAME,PARTLABEL "$LIVE_DISK" | awk '$2=="DEBIAN95-ESP"{print "/dev/"$1}')
[ -b "$LIVE_ESP" ] || die "live ESP (DEBIAN95-ESP) not found"
say "live system: root=$LIVE_ROOT esp=$LIVE_ESP disk=$LIVE_DISK"

# ---- pick the target disk ----
TARGET="${1:-}"
if [ -z "$TARGET" ]; then
    TARGET=/dev/$(lsblk -dno NAME,TYPE | awk '$2=="disk"{print $1}' | grep -vx "$(basename "$LIVE_DISK")" | head -1)
fi
[ -b "$TARGET" ] || die "no target disk found (pass one, e.g. /dev/sda)"
[ "$TARGET" != "$LIVE_DISK" ] || die "refusing to install onto the live disk"
say "target disk: $TARGET ($(lsblk -dno SIZE "$TARGET"))"

# destructive confirmation (skip only for an automated test: DEBIAN95_INSTALL_YES=1)
if [ "${DEBIAN95_INSTALL_YES:-}" != 1 ]; then
    echo ""; echo "   WARNING: this ERASES everything on $TARGET."
    printf "   Type YES to install Debian95 there: "; read -r ans
    [ "$ans" = YES ] || die "cancelled"
fi

# partition-name helper (sda2 vs nvme0n1p2)
part() { case "$1" in *[0-9]) echo "${1}p${2}";; *) echo "${1}${2}";; esac; }
# Round partition sizes UP to the next MiB so the target is never a sliver
# smaller than the source (which would make the dd copy fail with ENOSPC).
ESP_MB=$(( ( $(blockdev --getsize64 "$LIVE_ESP") + 1048575 ) / 1048576 ))
ROOT_B=$(blockdev --getsize64 "$LIVE_ROOT")
ROOT_MB=$(( ( ROOT_B + 1048575 ) / 1048576 ))

say "ERASING $TARGET and writing a fresh GPT..."
wipefs -a "$TARGET" >/dev/null 2>&1 || true
sgdisk --zap-all "$TARGET" >/dev/null
sgdisk -a 2048 \
  -n 1:0:+${ESP_MB}M  -t 1:EF00 -c 1:DEBIAN95-ESP \
  -n 2:0:+${ROOT_MB}M -t 2:8300 -c 2:DEBIAN95-ROOT \
  -n 3:0:0            -t 3:8300 -c 3:DEBIAN95-DATA "$TARGET" >/dev/null
partprobe "$TARGET"; sleep 1
T_ESP=$(part "$TARGET" 1); T_ROOT=$(part "$TARGET" 2)

say "copying the EFI system partition..."
dd if="$LIVE_ESP" of="$T_ESP" bs=4M conv=fsync status=none
say "copying the root filesystem ($ROOT_MB MB)..."
dd if="$LIVE_ROOT" of="$T_ROOT" bs=4M conv=fsync status=none

say "finalising the installed root (fresh UUID, fsck)..."
e2fsck -fy "$T_ROOT" >/dev/null 2>&1 || true
NEWUUID=$(cat /proc/sys/kernel/random/uuid)   # always present (uuidgen needs uuid-runtime)
tune2fs -U "$NEWUUID" "$T_ROOT" >/dev/null
resize2fs "$T_ROOT" >/dev/null 2>&1 || true        # fill the (same-size) partition

say "repointing GRUB at the installed root ($NEWUUID)..."
OLDUUID=$(blkid -s UUID -o value "$LIVE_ROOT")
M=$(mktemp -d)
mount "$T_ESP" "$M"
for g in "$M/EFI/debian/grub.cfg" "$M/EFI/BOOT/grub.cfg"; do
    [ -f "$g" ] && sed -i "s/$OLDUUID/$NEWUUID/g" "$g"
done
umount "$M"; rmdir "$M"

sync
say "done. Debian95 is installed on $TARGET."
say "Remove the USB stick and reboot; first boot will set your password and"
say "encrypt the data partition on the internal disk."
