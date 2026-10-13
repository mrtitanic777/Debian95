#!/usr/bin/env bash
# Fetch the static aarch64 FFmpeg used by the ARM64 rootfs. The binary itself is
# large (~51 MB) and redistributable, so it is not committed - run this once to
# stage it, then `make -f Makefile.arm64` picks it up.
set -euo pipefail
DEST="$(dirname "$0")/../assets/rootfs"
URL="https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-arm64-static.tar.xz"
mkdir -p "$DEST"
tmp="$(mktemp -d)"
echo "downloading $URL ..."
curl -fsSL -o "$tmp/ffmpeg.tar.xz" "$URL"
dir="$(tar tJf "$tmp/ffmpeg.tar.xz" | head -1)"
tar xJf "$tmp/ffmpeg.tar.xz" -C "$tmp" "${dir}ffmpeg" "${dir}ffprobe"
cp "$tmp/${dir}ffmpeg"  "$DEST/ffmpeg"
cp "$tmp/${dir}ffprobe" "$DEST/ffprobe"
rm -rf "$tmp"
echo "staged $DEST/ffmpeg ($(stat -c%s "$DEST/ffmpeg" 2>/dev/null || echo ?) bytes)"
