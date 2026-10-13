#!/usr/bin/env bash
# Debian95 — launch the disk image in QEMU.
#   tools/run.sh            interactive (serial on stdio, VGA window)
#   tools/run.sh --headless serial only, no display (for capture/CI)
set -euo pipefail

QEMU="${QEMU:-/c/Program Files/qemu/qemu-system-x86_64.exe}"
IMG=build/debian95.img

FAT=build/fat32.img
ARGS=(-cpu max -drive "format=raw,file=$IMG,index=0,if=ide" -no-reboot -no-shutdown -m 256)
[ -f "$FAT" ] && ARGS+=(-drive "format=raw,file=$FAT,index=1,if=ide")
# RTL8139 NIC on QEMU user-mode networking
ARGS+=(-netdev user,id=n0 -device rtl8139,netdev=n0)

if [ "${1:-}" = "--headless" ]; then
    shift
    ARGS+=(-serial stdio -display none)
else
    ARGS+=(-serial stdio)
fi

exec "$QEMU" "${ARGS[@]}" "$@"
