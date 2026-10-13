#!/bin/bash
# Install + brand Plymouth in the Debian95 Debian rootfs: an animated boot splash
# (a centered "Debian95" logo + spinner) shown during the kernel/systemd boot, so
# startup looks like an OS booting, not a text scroll. Run once after the rootfs
# exists; re-run mk-usb.sh afterwards (its cmdline has `splash
# plymouth.ignore-serial-consoles` and no console=tty0, so Plymouth owns the GPU).
# Unprivileged (unshare userns chroot). Needs python3-pil on the host for the logo.
#   tools/setup-plymouth.sh [arch]
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"
[ -d "$RD" ] || { echo "no rootdir ($RD) - run tools/mk-debian-image.sh $ARCH first"; exit 1; }

echo "== generating the Debian95 Plymouth logo =="
if python3 -c 'import PIL' 2>/dev/null; then
  FONT=""; [ -f "$RD/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" ] && FONT="$RD/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
  python3 - "$RD/root/debian95-logo.png" "$FONT" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont
out, font = sys.argv[1], sys.argv[2]
img = Image.new('RGBA', (420, 130), (0,0,0,0)); d = ImageDraw.Draw(img)
try: f = ImageFont.truetype(font, 72)
except Exception: f = ImageFont.load_default()
t = "Debian95"; w = d.textbbox((0,0), t, font=f)[2]
d.text(((420-w)//2, 20), t, font=f, fill=(255,255,255,255)); img.save(out); print("logo ok")
PY
else echo "  (python3-pil missing: Plymouth will show the plain spinner)"; fi

echo "== install plymouth + spinner theme, brand it, regenerate the initrd (chroot) =="
printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$RD/etc/resolv.conf" 2>/dev/null || true
unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c '
  set -e; export DEBIAN_FRONTEND=noninteractive PATH=/usr/sbin:/usr/bin:/sbin:/bin
  printf "nameserver 1.1.1.1\nnameserver 8.8.8.8\n" > /etc/resolv.conf
  apt-get update -o APT::Sandbox::User=root -qq
  apt-get install -y --no-install-recommends -o APT::Sandbox::User=root plymouth plymouth-themes 2>&1 | tail -1
  T=/usr/share/plymouth/themes/spinner
  if [ -d "$T" ]; then
    [ -f /root/debian95-logo.png ] && cp /root/debian95-logo.png "$T/watermark.png"
    if [ -f "$T/spinner.plymouth" ]; then
      sed -i "s/^WatermarkHorizontalAlignment=.*/WatermarkHorizontalAlignment=.5/; s/^WatermarkVerticalAlignment=.*/WatermarkVerticalAlignment=.5/" "$T/spinner.plymouth"
      grep -q WatermarkHorizontalAlignment "$T/spinner.plymouth" || printf "WatermarkHorizontalAlignment=.5\nWatermarkVerticalAlignment=.5\n" >> "$T/spinner.plymouth"
    fi
  fi
  plymouth-set-default-theme spinner
  KVER=$(ls /boot/vmlinuz-* | sed "s|.*/vmlinuz-||" | sort -V | tail -1)
  update-initramfs -u -k "$KVER" 2>&1 | tail -1
  echo "plymouth default theme: $(plymouth-set-default-theme)"
'
echo "Done. Rebuild the image:  tools/mk-usb.sh $ARCH"
