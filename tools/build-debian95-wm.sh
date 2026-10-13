#!/bin/bash
# Build the Debian95 Desktop-Edition window manager (usermode/debian95-wm) INTO an
# extracted Debian rootfs, unprivileged. Installs gcc + libx11-dev inside the
# rootfs via an unshare user-namespace chroot (fakechroot can't be used: the
# Ubuntu host's glibc is newer than the Debian target's, so its preloaded chroot
# won't run in the target - a real chroot inside a userns execs the TARGET's own
# binaries instead). apt runs as mapped-root, so its download sandbox is disabled.
#   tools/build-debian95-wm.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"
[ -d "$RD" ] || { echo "no extracted rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

cp "$REPO/usermode/debian95-wm/debian95-wm.c" "$RD/root/debian95-wm.c"
cp "$REPO/usermode/debian95-wm/wmtypes-test.c" "$RD/root/wmtypes-test.c" 2>/dev/null || true
printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$RD/etc/resolv.conf"
echo 'APT::Sandbox::User "root";' > "$RD/etc/apt/apt.conf.d/00sandbox-off"

unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c '
  set -e
  export DEBIAN_FRONTEND=noninteractive PATH=/usr/sbin:/usr/bin:/sbin:/bin
  apt-get update -o APT::Sandbox::User=root -qq
  apt-get install -y --no-install-recommends -o APT::Sandbox::User=root gcc libc6-dev libx11-dev
  gcc -O2 -Wall -o /usr/local/bin/debian95-wm /root/debian95-wm.c -lX11
  [ -f /root/wmtypes-test.c ] && gcc -O2 -o /usr/local/bin/wmtypes-test /root/wmtypes-test.c -lX11 2>/dev/null || true
  echo "debian95-wm built: $(ls -l /usr/local/bin/debian95-wm)"
'
echo "Done. Set the X session to run debian95-wm (tools/debian-xsession.sh $ARCH \"debian95-wm\") and rebuild the image."
