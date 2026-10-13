#!/bin/bash
# Configure a Debian95 Desktop-Edition Debian rootfs to autologin on tty1 and start
# the full Debian95 Win95 desktop (debian95-desktop-sdl) as the X session. Operates on
# the extracted rootdir; re-run mk-debian-image.sh afterwards to fold it into the image.
#   tools/debian-xsession.sh [arch] ["session command"]
# The default session is the full desktop; pass a command to override (e.g. "xterm").
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
SESSION_CMD="${2:-/usr/local/bin/debian95-desktop-sdl}"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"
[ -d "$RD" ] || { echo "no extracted rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

# autologin root on tty1 (the Debian95 desktop runs fine as root)
mkdir -p "$RD/etc/systemd/system/getty@tty1.service.d"
cat > "$RD/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'X'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
X
# start X automatically on tty1 login (-ac: allow the debian95 user's apps, which the
# shell spawns with dropped privileges, to connect to root's X display). SAFE MODE:
# the GRUB "Safe Mode" entry adds debian95.nostartx to the kernel cmdline, which drops
# the autologin to a plain root shell instead of the desktop - so a first boot on
# real hardware whose GPU/X misbehaves is recoverable, not a black screen.
cat > "$RD/root/.bash_profile" <<'X'
if [ "$(tty)" = "/dev/tty1" ] && [ -z "${DISPLAY:-}" ] && ! grep -qw debian95.nostartx /proc/cmdline; then
  debian95-startx        # resilient launcher: KMS -> fbdev -> vesa, diagnostics, no black-screen loop
  # X exited (logout/shutdown, or every display fallback failed). Fall through to an
  # interactive root shell rather than letting getty respawn into another attempt.
fi
[ "$(tty)" = "/dev/tty1" ] && grep -qw debian95.nostartx /proc/cmdline && \
  echo "Debian95 safe mode: desktop not started. Run 'debian95-startx' to try the desktop, or diagnose here."
X
# Guarantee a reachable text login on tty2/tty3 (Ctrl+Alt+F2/F3) even when the
# desktop owns tty1 - a rescue console for a real-hardware first boot. Root's
# password is set by mk-debian (root:debian95). These are symlinks in an existing
# dir, so the image builder keeps them.
mkdir -p "$RD/etc/systemd/system/getty.target.wants"
for v in tty2 tty3; do
  ln -sf /lib/systemd/system/getty@.service "$RD/etc/systemd/system/getty.target.wants/getty@$v.service"
done
# the X session = the Debian95 desktop ENVIRONMENT: the reparenting WM + the Win95
# shell. Downloaded apps (VLC, Chromium) are NOT started here - the user launches
# them from their desktop icon / Start > Programs entry, which forks them as the
# 'debian95' user (GUI apps refuse root). LOG -> serial in QEMU, else a file.
cat > "$RD/root/.xinitrc" <<X
export DEBIAN95_DATA=/usr/share/debian95 SDL_AUDIODRIVER=dummy
# The "Welcome to Windows" logon is ON: first boot sets a password (which LUKS-
# formats the encrypted DEBIAN95-DATA partition - the password IS the disk key),
# later boots unlock it. Set DEBIAN95_NOLOGON=1 here only for a no-login appliance.
id debian95 >/dev/null 2>&1 || useradd -m -s /bin/sh debian95
mkdir -p /tmp/debian95-home; chown debian95:debian95 /tmp/debian95-home 2>/dev/null; chmod 700 /tmp/debian95-home
LOG=/dev/ttyS0; [ -w "\$LOG" ] || LOG=/var/log/debian95.log
# audio: run the PipeWire stack as the debian95 user under one dbus session (WirePlumber
# needs a session bus). The apps run as debian95 too and find the socket via
# XDG_RUNTIME_DIR=/tmp/debian95-home. No-op if not installed.
if [ -x /usr/bin/pipewire ]; then
  runuser -u debian95 -- env XDG_RUNTIME_DIR=/tmp/debian95-home dbus-run-session -- \
    sh -c 'pipewire & sleep 1; wireplumber & sleep 1; pipewire-pulse & wait' >>"\$LOG" 2>&1 &
fi
/usr/local/bin/debian95-wm >>"\$LOG" 2>&1 &
exec ${SESSION_CMD} >>"\$LOG" 2>&1
X
# the resilient X launcher (KMS->fbdev->vesa fallback + boot diagnostics to the ESP)
install -Dm755 "$(dirname "$0")/debian95-startx.sh" "$RD/usr/local/bin/debian95-startx"
# persistent journald: without this dir the journal is volatile and a failed first
# boot leaves nothing to inspect. With it, `journalctl -b -1` survives a reboot.
mkdir -p "$RD/var/log/journal"
# the installer ("Setup" / Start > Programs): copies the live system to an internal disk
install -Dm755 "$(dirname "$0")/debian95-install.sh" "$RD/usr/local/sbin/debian95-install"

echo "X session set to: $SESSION_CMD  (re-run tools/mk-debian-image.sh $ARCH to rebuild the ext4)."
