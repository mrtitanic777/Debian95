#!/usr/bin/env bash
# Debian95 — assemble the raw disk image from the boot chain + kernel.
# Layout must match boot/x86/layout.inc.
set -euo pipefail

BUILD=build
OUT=$BUILD/debian95.img
S1=$BUILD/stage1.bin
S2=$BUILD/stage2.bin
K=$BUILD/kernel.bin

STAGE2_LBA=1
STAGE2_SECTORS=32
KERNEL_LBA=33
KERNEL_SECTORS=512
IMG_SECTORS=2048          # 1 MiB image

sz() { stat -c%s "$1"; }

s1sz=$(sz "$S1"); s2sz=$(sz "$S2"); ksz=$(sz "$K")

[ "$s1sz" -eq 512 ] || { echo "ERROR: stage1.bin is $s1sz bytes (must be 512)"; exit 1; }
[ "$s2sz" -le $((STAGE2_SECTORS*512)) ] || { echo "ERROR: stage2.bin $s2sz > $((STAGE2_SECTORS*512))"; exit 1; }
[ "$ksz"  -le $((KERNEL_SECTORS*512)) ] || { echo "ERROR: kernel.bin $ksz > $((KERNEL_SECTORS*512)) (raise KERNEL_SECTORS)"; exit 1; }

dd if=/dev/zero of="$OUT" bs=512 count=$IMG_SECTORS status=none
dd if="$S1" of="$OUT" bs=512 conv=notrunc status=none
dd if="$S2" of="$OUT" bs=512 seek=$STAGE2_LBA conv=notrunc status=none
dd if="$K"  of="$OUT" bs=512 seek=$KERNEL_LBA conv=notrunc status=none

echo "built $OUT  (stage1=$s1sz  stage2=$s2sz  kernel=$ksz bytes)"
