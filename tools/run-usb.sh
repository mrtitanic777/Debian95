#!/bin/bash
# Boot the Debian95 USB/disk image (tools/mk-usb.sh) under real UEFI firmware (OVMF)
# in QEMU, as a USB stick - the same path real hardware takes. Opens a window.
#   tools/run-usb.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
OUT="$HOME/debian95/debian-$ARCH"
IMG="$OUT/debian95-usb.img"
[ -f "$IMG" ] || { echo "no image: $IMG (run tools/mk-usb.sh $ARCH)"; exit 1; }
CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$OUT/OVMF_VARS.fd"
ACCEL=tcg; [ -w /dev/kvm ] && ACCEL=kvm
exec qemu-system-x86_64 -M q35 -m 2048 -smp 2 -accel "$ACCEL" \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,file="$OUT/OVMF_VARS.fd" \
  -drive file="$IMG",format=raw,if=none,id=usb0 \
  -device qemu-xhci -device usb-storage,drive=usb0,bootindex=0 \
  -device virtio-rng-pci -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -audiodev none,id=snd0 -device intel-hda -device hda-output,audiodev=snd0 \
  -vga none -device virtio-gpu-pci -device virtio-tablet-pci -device virtio-keyboard-pci
