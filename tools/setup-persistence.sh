#!/bin/bash
# Persistence for the Debian95 USB: the ext4 root is mounted rw, so files and
# settings already survive reboots. This adds a first-boot service that grows the
# root partition + filesystem to fill the whole stick (the image is only ~3 GB, so
# without this the extra space on a bigger USB is wasted). Runs once, then flags
# itself done. Unprivileged (unshare userns chroot). Run once after the rootfs exists.
#   tools/setup-persistence.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"
[ -d "$RD" ] || { echo "no rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

echo "== install growpart (cloud-guest-utils) =="
unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c '
  set -e; export DEBIAN_FRONTEND=noninteractive PATH=/usr/sbin:/usr/bin:/sbin:/bin
  printf "nameserver 1.1.1.1\nnameserver 8.8.8.8\n" > /etc/resolv.conf
  apt-get update -o APT::Sandbox::User=root -qq
  apt-get install -y --no-install-recommends -o APT::Sandbox::User=root cloud-guest-utils gdisk 2>&1 | tail -1
'

echo "== install the first-boot grow-root service =="
cat > "$RD/usr/local/sbin/debian95-growroot.sh" <<'SH'
#!/bin/sh
# grow the root partition + filesystem to fill the disk, once
root=$(findmnt -no SOURCE / 2>/dev/null) || exit 0
case "$root" in /dev/*) ;; *) exit 0 ;; esac
disk=$(lsblk -no PKNAME "$root" 2>/dev/null)
part=$(printf '%s' "$root" | grep -o '[0-9]*$')
[ -n "$disk" ] && [ -n "$part" ] || exit 0
# the image is smaller than the stick: relocate the GPT backup header to the true
# disk end so the free space is claimable, then grow the partition + filesystem
sgdisk -e "/dev/$disk" 2>/dev/null || true
partprobe "/dev/$disk" 2>/dev/null || true
growpart "/dev/$disk" "$part" 2>/dev/null || true
partprobe "/dev/$disk" 2>/dev/null || true
# grow the filesystem to fill the enlarged partition - resize tool depends on type
fstype=$(findmnt -no FSTYPE / 2>/dev/null)
case "$fstype" in
  btrfs) btrfs filesystem resize max / 2>/dev/null || true ;;
  *)     resize2fs "$root" 2>/dev/null || true ;;
esac
mkdir -p /var/lib && : > /var/lib/debian95-grown
SH
chmod +x "$RD/usr/local/sbin/debian95-growroot.sh"
cat > "$RD/etc/systemd/system/debian95-growroot.service" <<'UNIT'
[Unit]
Description=Grow the Debian95 root to fill the disk (first boot)
DefaultDependencies=no
After=systemd-remount-fs.service
Before=sysinit.target
ConditionPathExists=!/var/lib/debian95-grown
[Service]
Type=oneshot
ExecStart=/usr/local/sbin/debian95-growroot.sh
RemainAfterExit=yes
[Install]
WantedBy=sysinit.target
UNIT
# enable it (static symlink - systemctl enable needs a running systemd)
mkdir -p "$RD/etc/systemd/system/sysinit.target.wants"
ln -sf ../debian95-growroot.service "$RD/etc/systemd/system/sysinit.target.wants/debian95-growroot.service"
echo "Done. Root is rw (persistent); it grows to fill the stick on first boot."
