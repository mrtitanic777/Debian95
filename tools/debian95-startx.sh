#!/bin/sh
# Debian95 resilient X launcher. Installed as /usr/local/bin/debian95-startx and
# called from root's .bash_profile on tty1. It:
#   1. writes a hardware/boot snapshot to a log (and publishes it to the FAT ESP,
#      which is readable from any OS - so a failed first boot can be diagnosed),
#   2. starts Xorg, falling back KMS modesetting -> fbdev -> vesa on failure,
#   3. on total failure drops to a readable text console instead of a black-screen
#      respawn loop.
# It is deliberately NOT `set -e`: every probe/mount may fail harmlessly.
set -u
LOGDIR=/var/log/debian95
FBCONF=/etc/X11/xorg.conf.d/99-debian95-fallback.conf
mkdir -p "$LOGDIR" /etc/X11/xorg.conf.d
stamp() { date "+%Y-%m-%d %H:%M:%S" 2>/dev/null || echo "?"; }

# --- publish the log to the ESP (FAT32, readable from Windows/macOS/Linux) ---
publish_log() {
    espdev=$(readlink -f /dev/disk/by-partlabel/DEBIAN95-ESP 2>/dev/null)
    [ -z "$espdev" ] && return 0
    mkdir -p /run/debian95-esp
    mountpoint -q /run/debian95-esp || mount -t vfat "$espdev" /run/debian95-esp 2>/dev/null || return 0
    cp -f "$LOGDIR/boot.log" /run/debian95-esp/DEBIAN95-BOOT.LOG 2>/dev/null
    sync 2>/dev/null
}

# --- one hardware snapshot per boot: what the real machine actually exposes ---
hw_probe() {
    {
        echo "============================================================"
        echo "Debian95 boot  $(stamp)   kernel $(uname -r 2>/dev/null)"
        echo "-- cmdline --";    cat /proc/cmdline 2>/dev/null
        echo "-- GPU (PCI) --";  lspci -nnk 2>/dev/null | grep -iEA3 'vga|3d|display'
        echo "-- DRM nodes --";  ls -1 /dev/dri 2>/dev/null
        echo "-- framebuffer --";cat /sys/class/graphics/fb0/name 2>/dev/null
        echo "-- audio --";      ls -1 /dev/snd 2>/dev/null
        echo "-- network --";    ip -o link 2>/dev/null | awk -F': ' '{print $2}'
        echo "-- backlight --";  ls -1 /sys/class/backlight 2>/dev/null
        echo "-- battery --";    ls -1 /sys/class/power_supply 2>/dev/null
        echo "-- bluetooth --";  ls -1 /sys/class/bluetooth 2>/dev/null
        echo "------------------------------------------------------------"
    } >> "$LOGDIR/boot.log" 2>&1
}

save_diag() {   # $1 = driver label, $2 = startx exit code
    echo "=== $(stamp)  X attempt [$1] exit=$2 ===" >> "$LOGDIR/boot.log"
    [ -f /var/log/Xorg.0.log ] && cp -f /var/log/Xorg.0.log "$LOGDIR/Xorg.$1.log" 2>/dev/null
    grep -iE 'no screens|no devices detected|fatal|EE\)' /var/log/Xorg.0.log 2>/dev/null >> "$LOGDIR/boot.log"
    publish_log
}

try_x() {       # $1 = driver label
    startx -- -nolisten tcp -ac vt1 >> "$LOGDIR/boot.log" 2>&1
    rc=$?
    save_diag "$1" "$rc"
    return $rc
}

hw_probe
publish_log

# Attempt 1: default - let Xorg auto-detect (KMS modesetting on real GPUs).
rm -f "$FBCONF"
try_x modesetting && exit 0

# Attempt 2: generic framebuffer (UEFI GOP / efifb - works on almost anything).
cat > "$FBCONF" <<CONF
Section "Device"
    Identifier "Debian95 Fallback"
    Driver     "fbdev"
EndSection
CONF
try_x fbdev && exit 0

# Attempt 3: VESA BIOS framebuffer.
sed -i 's/"fbdev"/"vesa"/' "$FBCONF" 2>/dev/null
try_x vesa && exit 0

# Everything failed - do NOT let getty respawn into an instant retry loop.
rm -f "$FBCONF"
chvt 1 2>/dev/null
cat > /dev/tty1 <<MSG

  Debian95 could not start the graphical desktop on this display adapter.
  Tried: KMS modesetting, fbdev, vesa - all failed.

  Diagnostics were saved to $LOGDIR and to DEBIAN95-BOOT.LOG on the
  USB's EFI partition (readable from any computer).

  You are now at a text console as root (password: debian95).
  To try again:            debian95-startx
  To boot without X:       reboot and pick "Safe Mode" in the Debian95 menu.

MSG
exit 1
