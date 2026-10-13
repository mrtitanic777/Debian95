# Booting Debian95 on real hardware

Everything to date is verified under QEMU/OVMF. This is the checklist for the one
milestone a VM can't cover: booting the signed USB on a physical machine and
validating the hardware paths. Work top to bottom; if the desktop never appears,
jump to **If it doesn't boot**.

## 0. Before you flash

- **Build is current:** `tools/build-debian95.sh amd64` → `~/debian95/debian-amd64/debian95-usb.img`
  (the copy at `Downloads\debian95-usb.img` is the one to flash).
- **A spare USB stick, 8 GB+.** Flashing **erases it completely**.
- **Back up anything on the target machine** if you'll let Debian95 install to its disk.
  Running live off the USB touches nothing internal.

## 1. Flash the USB

Pick one. The image is a full GPT disk, so write it raw — do **not** copy the file onto
an existing filesystem.

- **balenaEtcher** (simplest, any OS): select `debian95-usb.img`, select the stick, Flash.
- **Rufus** (Windows): select the image, **DD Image mode** (Rufus will prompt — choose DD, not ISO), Start.
- **dd** (Linux/macOS, double-check the device):
  ```
  dd if=debian95-usb.img of=/dev/sdX bs=4M status=progress conv=fsync
  ```

## 2. BIOS / firmware setup

- Boot the target to its firmware menu (usually **F2 / F10 / F12 / Del / Esc** at power-on)
  and pick the USB stick as the boot device, or set it first in the boot order.
- **UEFI mode**, not Legacy/CSM. The image is UEFI-only.
- **Secure Boot** can stay **ON** — the chain is Microsoft-signed shim + Debian-signed
  GRUB + Debian-signed kernel. If the firmware refuses it, turn Secure Boot off and retry;
  that isolates whether it's a signing issue.

## 3. First boot — what you should see

1. **GRUB menu** (now visible for 5 s): `Debian95` / `Debian95 (Verbose boot)` /
   `Debian95 (Safe Mode)`. Let it boot the default, or pick **Verbose** the first time to
   watch the kernel messages on screen.
2. **Plymouth splash** — the Debian95 logo + spinner.
3. **"Set Debian95 Password"** (first run only) — this LUKS-formats the encrypted data
   partition; the password you set is the disk key. Later boots show **Log On** instead.
4. The **Win95 desktop**.

## 4. Hardware validation checklist

Tick these off on the real machine. The first stop for the hardware readout is
**Start → Programs → Accessories → System Tools → Device Manager** — it lists the real
GPU, audio, network and disks it found.

- [ ] **Display / GPU** — desktop fills the screen at native resolution, not a stretched
      640×480. (If it fell back to fbdev/vesa it'll look plain but still work — note that.)
- [ ] **Resolution** — Control Panel → Display → Settings lists the panel's real modes.
- [ ] **Mouse + keyboard** — both work (USB and built-in/PS2).
- [ ] **Audio** — Control Panel → Multimedia → Audio shows a real **Preferred device**
      (not "(none)"); the tray volume slider changes volume.
- [ ] **Audio output switching** — if there are multiple outputs (HDMI + speakers), the
      Preferred-device dropdown switches between them.
- [ ] **Wi-Fi** — Control Panel → Network → Wireless lists nearby SSIDs and can join one.
- [ ] **Wired network** — if plugged in, it gets an address (check in a browser / ping).
- [ ] **Brightness** (laptop) — Display → Settings shows a real Brightness slider (not
      "(n/a)") and it dims the panel.
- [ ] **Battery** (laptop) — a battery gauge appears in the tray and tracks charge.
- [ ] **Bluetooth** — Control Panel → Bluetooth toggles the adapter and scans for devices.
- [ ] **USB stick hot-plug** — insert a second USB drive: it auto-mounts, appears in My
      Computer, and a **Safely Remove Hardware** icon shows in the tray (click → Eject).
- [ ] **Suspend / resume** — Start → Shut Down → Stand By, then wake it.
- [ ] **SMB** (if you have a file server) — Network Neighborhood lists computers, opens a
      share, mounts it.
- [ ] **Shutdown** — Start → Shut Down powers the machine off cleanly.

## 5. Persistence check (second boot)

- Reboot. You should get the **Log On** screen (the data partition is now encrypted).
- Your password unlocks it; wallpaper/theme/settings you changed persist.
- On a bigger stick the root partition auto-grows to fill it on first boot.

## 6. Grab the diagnostics (always useful, essential if something breaks)

Every boot writes a hardware snapshot + the Xorg log to the **FAT EFI partition** as
`DEBIAN95-BOOT.LOG`. It's readable from any computer:

- Plug the Debian95 stick into another machine. The small FAT partition
  (volume label **DEBIAN95EFI**) mounts on Windows/macOS/Linux.
- Copy **`DEBIAN95-BOOT.LOG`** off it and send it over. It contains the GPU/DRM/audio/net/
  backlight/battery/Bluetooth probe and which X driver attempt succeeded — enough to
  diagnose most first-boot problems.

## If it doesn't boot

- **Black screen after GRUB** → reboot, pick **Safe Mode** (text console, no KMS). You get
  a root login (password `debian95`). From there run `debian95-startx` to try the desktop by
  hand, and `cat /var/log/debian95/boot.log`.
- **Desktop never appears, text login instead** → the resilient launcher exhausted its
  fallbacks; it prints where the logs are. Send `DEBIAN95-BOOT.LOG`.
- **Rescue consoles** → **Ctrl+Alt+F2 / F3** switch to a plain root shell at any time.
- **Firmware won't boot the stick** → confirm UEFI mode (not Legacy), try Secure Boot off,
  reflash in raw/DD mode (not ISO/file-copy).

## After a clean first boot

Report back which checklist items passed and attach `DEBIAN95-BOOT.LOG`; whatever fell back
or failed is the next thing to fix. That closes the last gap between "QEMU-verified" and
"runs on real hardware."
