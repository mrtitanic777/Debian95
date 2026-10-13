#!/bin/bash
# Debian95 Desktop Edition - build a minimal, fully-controlled Debian rootfs that
# can DOWNLOAD AND RUN official Linux binaries (VLC, Chromium/Chrome), with the
# Debian95 Win95 shell as the X11 window manager on top.
#
# Build-UP philosophy: start from Debian minbase (~300 MB, glibc + apt + nothing
# else) and add ONLY the explicit manifest below - no "recommends", no Snap, no
# telemetry. The whole system is this reproducible recipe; you own every package.
#
# Multi-arch: Debian is first-class on amd64/arm64/i386/... This script cross-builds
# any of them from one host using mmdebstrap's unshare mode (no root) + qemu-user
# for the foreign-arch package configure step.
#
#   tools/mk-debian.sh [arch] [suite] [outdir]
#     arch  : amd64 (default) | arm64 | i386
#     suite : trixie (default; Debian 13 - fresh Mesa/glibc for modern apps)
#
# Prereqs (install once, needs root): mmdebstrap qemu-user-static binfmt-support uidmap
# Run in WSL/Linux. Produces <outdir>/rootfs-<arch>.tar.zst (a real Debian rootfs).
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

REPO="$(cd "$(dirname "$0")/.." && pwd)"
# The whole build is declared in debian95.conf (the single-file OS config). CLI args
# still override, so `mk-debian.sh arm64` works for a quick arch change.
FIRMWARE_BLOBS=yes; FORCED_UPDATES=no; HOSTNAME=debian95
SERVICES=""; APPS=""; DESKTOP=""
CONF="${DEBIAN95_CONF:-$REPO/debian95.conf}"
# shellcheck disable=SC1090
[ -f "$CONF" ] && { echo "== reading config: $CONF ==" >&2; . "$CONF"; }
ARCH="${1:-${ARCH:-amd64}}"
SUITE="${2:-${SUITE:-bookworm}}"
OUT="${3:-$HOME/debian95/debian-$ARCH}"
MIRROR="http://deb.debian.org/debian"

command -v mmdebstrap >/dev/null || { echo "mmdebstrap missing - see the header (sudo apt-get install mmdebstrap qemu-user-static binfmt-support uidmap)"; exit 1; }

mkdir -p "$OUT"
# Debian archive keyring: mmdebstrap must verify the Debian repo, but an Ubuntu
# host doesn't ship Debian's keyring. Rather than another root install, fetch the
# keyring .deb from Debian's pool and extract it locally (unprivileged). The
# packages are then cryptographically verified against it.
# A keyring is only usable if it actually carries the SUITE's signing keys - a
# stale host copy (bullseye-era) will pass a bare -f test but then apt-get update
# fails with NO_PUBKEY. This helper accepts a keyring only when gpg finds a key
# whose uid mentions the suite (e.g. "bookworm").
kr_has_suite() { [ -f "$1" ] && gpg --show-keys "$1" 2>/dev/null | grep -qi "$SUITE"; }

KEYRING=""
for c in /usr/share/keyrings/debian-archive-keyring.pgp /usr/share/keyrings/debian-archive-keyring.gpg; do
  kr_has_suite "$c" && KEYRING="$c" && break
done
if [ -z "$KEYRING" ]; then
  KR="$OUT/.keyring"
  # Always fetch fresh - a cached keyring can predate a key rotation and still say
  # "$SUITE", passing kr_has_suite yet lacking the CURRENT signing keys apt needs
  # (the exact NO_PUBKEY trap). It is a ~170 KB download, so refetch every build.
  echo "== fetching a fresh debian-archive-keyring for $SUITE =="
  rm -rf "$KR"; mkdir -p "$KR"
  # Fetch from DEBIAN's own pool: its keyring tracks the current archive signing
  # keys. (An Ubuntu host's debian-archive-keyring lags a release or two and can
  # be missing a key that signs InRelease - apt-get update --error-on=any then
  # turns that one NO_PUBKEY into a fatal error even though another sig is valid.)
  POOL="$MIRROR/pool/main/d/debian-archive-keyring"
  # NB: fetch the index to a FILE then grep it. `wget -qO- | grep` returns empty
  # in some environments (observed under WSL), which silently drops us onto the
  # fallback and a wrong keyring.
  wget -q "$POOL/" -O "$KR/pool.html" || true
  DEB=$(grep -oE 'debian-archive-keyring_[0-9][^"<> ]*_all\.deb' "$KR/pool.html" 2>/dev/null | sort -V | uniq | tail -1)
  URI=""; [ -n "$DEB" ] && URI="$POOL/$DEB"
  # fallback: let apt resolve it (may be the host distro's older package)
  [ -z "$URI" ] && URI=$(apt-get download --print-uris debian-archive-keyring 2>/dev/null | sed -E "s/^'([^']+)'.*/\1/" | head -1)
  [ -n "$URI" ] || { echo "could not resolve a debian-archive-keyring URL"; exit 1; }
  echo "   keyring package: $URI"
  wget -q "$URI" -O "$KR/kr.deb" || { echo "keyring download failed: $URI"; exit 1; }
  dpkg-deb -x "$KR/kr.deb" "$KR"
  for c in "$KR/usr/share/keyrings/debian-archive-keyring.pgp" "$KR/usr/share/keyrings/debian-archive-keyring.gpg"; do
    kr_has_suite "$c" && KEYRING="$c" && break
  done
fi
[ -n "$KEYRING" ] && kr_has_suite "$KEYRING" || { echo "no debian keyring with $SUITE signing keys available" >&2; exit 1; }
# CRITICAL: hand mmdebstrap the keyring as a real, dereferenced *.gpg file staged
# under /tmp. Two traps, both of which silently produce apt-get update NO_PUBKEY on
# a keyring gpgv itself accepts:
#   1. Extension/name: the real debian-archive-keyring file is now .pgp (its .gpg is
#      just a symlink); apt only reads *.gpg/*.asc, so pass a .gpg-named copy.
#   2. Location: under --mode=unshare mmdebstrap only makes its /tmp tempdir tree
#      reachable to the chrooted apt - a keyring under $OUT (or any other $HOME path)
#      is not visible inside the namespace and is read as empty. Proven: byte-
#      identical keyring works from /tmp, fails from $OUT. So stage it in /tmp.
KR_GPG="/tmp/debian95-dak.gpg"
cp -Lf "$KEYRING" "$KR_GPG" && KEYRING="$KR_GPG"
# Self-check: verify the STAGED keyring actually validates the live InRelease (this
# is exactly what mmdebstrap's apt-get update will do). Abort with a clear message
# rather than failing deep inside mmdebstrap with a bare NO_PUBKEY.
if command -v gpgv >/dev/null && wget -q "$MIRROR/dists/$SUITE/InRelease" -O /tmp/debian95-ir.chk 2>/dev/null; then
  gpgv --keyring "$KEYRING" /tmp/debian95-ir.chk >/dev/null 2>&1 \
    || { echo "FATAL: staged keyring $KEYRING does not validate $SUITE InRelease" >&2; exit 1; }
  echo "== keyring self-check: validates $SUITE InRelease ==" >&2
fi
echo "== using keyring: $KEYRING ==" >&2

# Debian kernel package differs per arch (gives ALL real-hardware drivers + GL).
case "$ARCH" in
  amd64) KPKG=linux-image-amd64 ;;
  arm64) KPKG=linux-image-arm64 ;;
  i386)  KPKG=linux-image-686-pae ;;
  *) echo "unsupported arch: $ARCH (amd64|arm64|i386)"; exit 1 ;;
esac

# The manifest is assembled from debian95.conf: kernel + core + the desktop runtime
# + the Windows-Environment-Layer services (network/sound/print/media/etc.) the
# Win95 shell fronts + preinstalled apps. Falls back to sane defaults if the conf
# left a group empty.
CORE="$KPKG,init,systemd-sysv,udev,dbus,iproute2,isc-dhcp-client,xterm,nano,less,pciutils,usbutils"
DESKTOP="${DESKTOP:-xserver-xorg-core xinit libgl1-mesa-dri fontconfig ca-certificates wget}"
INCLUDE="$CORE,$(echo "$DESKTOP $BOOT $SERVICES $APPS" | tr ' ' ',' | sed 's/,,*/,/g;s/^,//;s/,$//')"

# policy: firmware blobs (real wifi/GPU) unless the conf asks for purity. Enabling
# the component is not enough - the firmware PACKAGES must be in the manifest, or
# bare-metal wifi/GPU stay dark. FIRMWARE (from the conf) lists them.
if [ "${FIRMWARE_BLOBS:-yes}" = "yes" ]; then
  COMPONENTS="main,contrib,non-free,non-free-firmware"
  FW="$(echo "${FIRMWARE:-firmware-linux firmware-misc-nonfree}" | tr ' ' ',' | sed 's/,,*/,/g;s/^,//;s/,$//')"
  [ -n "$FW" ] && INCLUDE="$INCLUDE,$FW"
else COMPONENTS="main"; fi
# policy: no forced updates -> mask the apt-daily timers so the user chooses
UPD_HOOK=""
[ "${FORCED_UPDATES:-no}" = "no" ] && UPD_HOOK='chroot "$1" sh -c "systemctl mask apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true"'

mkdir -p "$OUT"
TAR="$OUT/rootfs-$ARCH.tar.gz"    # gzip is always present (zstd would need an extra install)
echo "== mmdebstrap: Debian $SUITE/$ARCH minbase + manifest -> $TAR =="
mmdebstrap \
  --mode=unshare \
  --variant=minbase \
  --arch="$ARCH" \
  --keyring="$KEYRING" \
  --include="$INCLUDE" \
  --components="$COMPONENTS" \
  --aptopt='Apt::Install-Recommends "false";' \
  --customize-hook="chroot \"\$1\" sh -c 'echo ${HOSTNAME:-debian95} > /etc/hostname; echo root:debian95 | chpasswd 2>/dev/null || true'" \
  ${UPD_HOOK:+--customize-hook="$UPD_HOOK"} \
  "$SUITE" "$TAR" "$MIRROR"

echo
echo "Debian $ARCH rootfs built: $TAR"
echo "  size: $(du -h "$TAR" | cut -f1)"
echo "Next: install the Debian95 shell (as the X11 WM) into it, then wrap it in a"
echo "bootable image (tools/mk-debian-image.sh, reusing the EFI-stub/GPT approach)."
