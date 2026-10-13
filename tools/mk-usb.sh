#!/bin/bash
# Build a bootable USB/disk image for the Debian95 Desktop Edition: a GPT disk with
# a FAT EFI System Partition (GRUB EFI) + the ext4 root, so real UEFI firmware
# boots it - no QEMU -kernel shortcut. Uses the Microsoft-signed shim + Debian-
# signed GRUB from the rootfs, so it boots with Secure Boot ON or OFF. Flash the
# result to a USB stick with Rufus / balenaEtcher / dd. Fully unprivileged (mtools
# + sgdisk + mke2fs -d); no loop mounts, no sudo.
#   tools/mk-usb.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HOME/debian95/debian-$ARCH"
RD="$OUT/rootdir"
IMG="$OUT/debian95-usb.img"
ESP="$OUT/esp.img"
ROOTFS="$OUT/rootfs.ext4"
ESP_MB=48

[ -d "$RD" ] || { echo "no rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

echo "== ensuring the ext4 root image exists (mk-debian-image) =="
[ -f "$ROOTFS" ] || bash "$REPO/tools/mk-debian-image.sh" "$ARCH"

# the root filesystem UUID + type (GRUB finds the root by UUID; kernel mounts root=UUID=).
# blkid handles both ext4 and btrfs (System Restore ships a btrfs root by default).
UUID="$(blkid -s UUID -o value "$ROOTFS" 2>/dev/null)"
FSTYPE="$(blkid -s TYPE -o value "$ROOTFS" 2>/dev/null)"; FSTYPE="${FSTYPE:-ext4}"
[ -n "$UUID" ] || { echo "could not read rootfs UUID"; exit 1; }
echo "   root is $FSTYPE"
KVER="$(cd "$RD/boot" && ls vmlinuz-* | sed 's/vmlinuz-//' | sort -V | tail -1)"
[ -n "$KVER" ] || { echo "no kernel in $RD/boot"; exit 1; }
echo "   root UUID=$UUID  kernel=$KVER"

echo "== locating the Secure Boot chain (MS-signed shim + Debian-signed GRUB) =="
# shim is signed by Microsoft's UEFI CA (trusted out of the box on retail PCs); it
# chainloads grubx64.efi (Debian-signed) from its own directory, which then reads
# grub.cfg from its baked-in prefix /EFI/debian. Works with Secure Boot ON or OFF.
pick() { for p in "$@"; do [ -f "$p" ] && { echo "$p"; return 0; }; done; return 1; }
SHIM="$(pick "$RD"/usr/lib/shim/shimx64.efi.signed.latest "$RD"/usr/lib/shim/shimx64.efi.signed)" \
  || { echo "shim-signed not in rootfs - is BOOT= set in debian95.conf and the rootfs rebuilt?"; exit 1; }
MM="$(pick "$RD"/usr/lib/shim/mmx64.efi.signed.latest "$RD"/usr/lib/shim/mmx64.efi.signed)" || MM=""
GRUBEFI="$(pick "$RD"/usr/lib/grub/x86_64-efi-signed/grubx64.efi.signed)" \
  || { echo "grub-efi-amd64-signed not in rootfs"; exit 1; }
echo "   shim=$SHIM"
echo "   grub=$GRUBEFI"

echo "== writing grub.cfg (find root by UUID, boot the kernel; Plymouth draws the splash) =="
GCFG="$OUT/grub.cfg"
cat > "$GCFG" <<CFG
set timeout=${USB_GRUB_TIMEOUT:-5}
set timeout_style=${USB_GRUB_STYLE:-menu}
set default=0
insmod all_video
insmod btrfs
insmod ext2
# The menu is VISIBLE by default (like Windows' boot options) so that, on a first
# real-hardware boot, Verbose and Safe Mode are one keypress away instead of a
# hold-Esc guess. Every entry keeps console=tty0 last, so the physical screen is the
# primary console - a KMS failure shows kernel/login text there, never a silent black
# screen. (Set USB_GRUB_STYLE=hidden once the machine is known-good.)
menuentry "Debian95" {
    search --no-floppy --set=root --fs-uuid $UUID
    linux /boot/vmlinuz-$KVER root=UUID=$UUID rootfstype=$FSTYPE rw quiet splash plymouth.ignore-serial-consoles loglevel=3 console=ttyS0,115200 console=tty0 net.ifnames=0 vt.global_cursor_default=0
    initrd /boot/initrd.img-$KVER
}
# Verbose: KMS + desktop as normal, but every boot message on screen (no quiet/splash)
# - the first thing to try if the default boot looks stuck.
menuentry "Debian95 (Verbose boot - show all messages)" {
    search --no-floppy --set=root --fs-uuid $UUID
    linux /boot/vmlinuz-$KVER root=UUID=$UUID rootfstype=$FSTYPE rw loglevel=7 console=ttyS0,115200 console=tty0 net.ifnames=0 vt.global_cursor_default=0
    initrd /boot/initrd.img-$KVER
}
# Safe Mode: no KMS (nomodeset - generic framebuffer for a misbehaving GPU), desktop
# autostart suppressed (debian95.nostartx). Boots to a root text login ON THE SCREEN
# (tty1); run 'debian95-startx' to try the desktop by hand.
menuentry "Debian95 (Safe Mode - text console)" {
    search --no-floppy --set=root --fs-uuid $UUID
    linux /boot/vmlinuz-$KVER root=UUID=$UUID rootfstype=$FSTYPE rw nomodeset debian95.nostartx loglevel=4 console=ttyS0,115200 console=tty0 net.ifnames=0
    initrd /boot/initrd.img-$KVER
}
CFG

echo "== building the FAT EFI System Partition (${ESP_MB}M) =="
rm -f "$ESP"
dd if=/dev/zero of="$ESP" bs=1M count=$ESP_MB status=none
mkfs.vfat -F 32 -n DEBIAN95EFI "$ESP" >/dev/null
mmd   -i "$ESP" ::/EFI ::/EFI/BOOT ::/EFI/debian
# fallback boot path: firmware runs \EFI\BOOT\BOOTX64.EFI = shim; shim runs grubx64.efi beside it
mcopy -i "$ESP" "$SHIM"    ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP" "$GRUBEFI" ::/EFI/BOOT/grubx64.efi
[ -n "$MM" ] && mcopy -i "$ESP" "$MM" ::/EFI/BOOT/mmx64.efi
# signed GRUB's baked-in prefix is /EFI/debian -> put grub.cfg (and a copy of grub) there
mcopy -i "$ESP" "$GRUBEFI" ::/EFI/debian/grubx64.efi
mcopy -i "$ESP" "$GCFG"    ::/EFI/debian/grub.cfg
mcopy -i "$ESP" "$GCFG"    ::/EFI/BOOT/grub.cfg

echo "== assembling the GPT disk image =="
ROOT_MB=$(( $(stat -c %s "$ROOTFS") / 1048576 ))
DATA_MB=${USB_DATA_MB:-1024}                       # encrypted /data (LUKS, formatted on first boot)
TOTAL_MB=$(( 1 + ESP_MB + ROOT_MB + DATA_MB + 1 )) # 1M align + ESP + root + data + backup GPT
rm -f "$IMG"
truncate -s "${TOTAL_MB}M" "$IMG"
# p1 = ESP (EF00), p2 = Linux root (8300), p3 = DEBIAN95-DATA (8300, raw -> LUKS on first
# boot; the logon password is its disk key). findfs PARTLABEL=DEBIAN95-DATA locates it.
sgdisk -a 2048 \
  -n 1:2048:+${ESP_MB}M -t 1:EF00 -c 1:DEBIAN95-ESP \
  -n 2:0:+${ROOT_MB}M   -t 2:8300 -c 2:DEBIAN95-ROOT \
  -n 3:0:0              -t 3:8300 -c 3:DEBIAN95-DATA "$IMG" >/dev/null
# splice the filesystem images into their partitions (unprivileged, no loop mount).
# p3 (data) is left as raw zeros - the first-boot logon LUKS-formats it.
dd if="$ESP"    of="$IMG" bs=1M seek=1              conv=notrunc status=none
dd if="$ROOTFS" of="$IMG" bs=1M seek=$((1+ESP_MB)) conv=notrunc status=none

echo
echo "Bootable image ready:  $IMG   (${TOTAL_MB} MiB)"
echo "Verify under UEFI:     tools/run-usb.sh $ARCH"
echo "Flash to a USB stick (ERASES IT): balenaEtcher / Rufus, or  dd if=$IMG of=/dev/sdX bs=4M status=progress"
