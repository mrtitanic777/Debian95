#!/bin/bash
# Install the PipeWire audio stack into the Debian95 Debian rootfs so the downloaded
# apps (VLC, Chromium) can actually play sound. The X session (debian-xsession.sh)
# starts pipewire + wireplumber + pipewire-pulse as the 'debian95' user; the apps run
# as debian95 too and find the socket via XDG_RUNTIME_DIR=/tmp/debian95-home.
# Unprivileged (unshare userns chroot). Run once after the rootfs exists.
#   tools/setup-audio.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"
[ -d "$RD" ] || { echo "no rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

echo "== install PipeWire + WirePlumber (with the audio SPA plugins) =="
unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c '
  set -e; export DEBIAN_FRONTEND=noninteractive PATH=/usr/sbin:/usr/bin:/sbin:/bin
  printf "nameserver 1.1.1.1\nnameserver 8.8.8.8\n" > /etc/resolv.conf
  apt-get update -o APT::Sandbox::User=root -qq
  # WITH recommends here: they pull the ALSA SPA plugin PipeWire needs for a real sink
  apt-get install -y -o APT::Sandbox::User=root pipewire pipewire-pulse wireplumber pipewire-alsa 2>&1 | tail -1
  echo "pipewire=$(command -v pipewire) wireplumber=$(command -v wireplumber) pw-pulse=$(command -v pipewire-pulse)"
'
echo "Done. The session auto-starts the stack; boot the guest with an audio device"
echo "(run-usb.sh already adds intel-hda). Rebuild the image: tools/mk-usb.sh $ARCH"
