#!/bin/bash
# Boot the Debian95 Desktop-Edition Debian image in QEMU (WSL/Linux).
#   tools/run-debian.sh [arch] [--serial]
#     --serial : headless serial console (no graphics) - for boot debugging
# Graphics use virtio-gpu (KMS/DRM) so Xorg's modesetting driver has a real GPU.
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
OUT="$HOME/debian95/debian-$ARCH"
K="$OUT/vmlinuz"; I="$OUT/initrd"; R="$OUT/rootfs.ext4"
[ -f "$K" ] && [ -f "$R" ] || { echo "no image (run tools/mk-debian-image.sh $ARCH)"; exit 1; }
APPEND="root=/dev/vda rw console=ttyS0 net.ifnames=0 vt.global_cursor_default=0"

ACCEL=tcg; qemu-system-x86_64 -accel help 2>/dev/null | grep -qi kvm && [ -w /dev/kvm ] && ACCEL=kvm
ARGS=(-M q35 -m 2048 -smp 2 -accel "$ACCEL"
  -kernel "$K" -initrd "$I" -append "$APPEND"
  -drive "file=$R,format=raw,if=none,id=hd0" -device virtio-blk-pci,drive=hd0
  -device virtio-rng-pci
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0)

case "${2:-}" in
  --serial) exec qemu-system-x86_64 "${ARGS[@]}" -display none -serial stdio ;;
  *) exec qemu-system-x86_64 "${ARGS[@]}" -vga none -device virtio-gpu-pci \
        -device virtio-tablet-pci -device virtio-keyboard-pci -serial stdio ;;
esac
