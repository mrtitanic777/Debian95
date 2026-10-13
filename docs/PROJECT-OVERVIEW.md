# Debian95 — Project Overview

**Edition:** Debian Desktop Edition
**Version:** 1.0
**Status:** Feature-complete; preparing for real-hardware validation
**Document date:** 2026-10-01

---

## 1. Summary

Debian95 is a genuinely functional Windows-95-style desktop operating environment
built on Debian 12 (bookworm). It boots from a signed UEFI USB image into an
original Win95 shell — window manager, taskbar, Start menu, desktop icons, and
GDI/USER-style drawing — written as a single C codebase. Unlike a theme or a
mockup, the shell runs **real downloaded Linux applications** (VLC, Chromium,
PCManFM, and any other installed GUI app), reparented into Win95 window chrome
and placed on the taskbar.

The distinguishing idea is the **Windows Environment Layer**: the Control Panel
and shell are not decorative. They drive the real operating system underneath —
`apt` for Add/Remove Programs, `systemctl` for Services, `/proc` for Task
Manager, NetworkManager for Wi-Fi, PipeWire for audio, CUPS for printing, and
LUKS for an encrypted, password-gated data volume.

---

## 2. Architecture

Debian95 is composed of three original pillars layered over a standard Debian base.

### 2.1 The shell — `subsystems/win32/` (shared C)
A single-process compositing window manager that paints the Win95 desktop and
hosts every built-in application as a C function opening a window. It implements
its own GDI/USER-style primitives (3D chrome, MS Sans Serif text, dialogs,
common controls) on top of a linear framebuffer. It is built as
`debian95-desktop-sdl` and runs as the X session.

A notable sub-system is the **resource/dialog engine** (`dlgeng.c`): rather than
hand-placing pixels, dialogs are defined as resource templates with control
classes, Dialog Units, a dialog manager, and property sheets — the way the
original Win32 UI was constructed. Control Panel applets are built on it.

### 2.2 The window manager — `usermode/debian95-wm` (reparenting X11 WM)
A reparenting X11 window manager that wraps foreign Linux applications in Win95
chrome (title bar; minimize / maximize / close; drag; move/size; focus-aware,
themeable coloring; right-click system menu; Alt+Tab) and publishes them to the
shell's taskbar over EWMH. The desktop itself is recognized as the unframed root
client.

### 2.3 The Windows Environment Layer
The bridge that makes the UI operate the real system:

| Shell surface | Real subsystem |
|---|---|
| Add/Remove Programs | `apt` |
| Services | `systemctl` |
| Task Manager | `/proc` |
| Network applet (Wi-Fi scan/connect) | NetworkManager (`nmcli`) |
| Users / passwords | `useradd`, PAM |
| Volume / sound | PipeWire / ALSA |
| Printers | CUPS (`lp`) |
| Drives | `lsblk` / udisks2 |

Downloaded applications launch from a desktop icon or **Start → Programs** (never
a loose script), forked as a non-root user.

---

## 3. Component inventory

**Built-in applications:** Explorer/File Manager, Notepad/Editor, WordPad,
Paint, Calculator (standard + scientific), Character Map, Clipboard Viewer,
Clock, Sound Recorder, Volume, Media Player, Recycle Bin, Registry Editor, Task
Manager, Services, MS-DOS prompt, plus games (Minesweeper, Solitaire, FreeCell).

**Control Panel applets:** Display, Sounds, Mouse, Keyboard, Date/Time,
Regional, Network, Accessibility, Add/Remove Programs, Passwords, System — each
migrated onto the property-sheet dialog engine.

**System Tools:** ScanDisk and Disk Defragmenter (animated), the Setup
installer, Welcome tips, screen savers (Pipes, Flying Windows).

**In-tree media & device support (`drivers/`):** software decoders for PNG,
JPEG, GIF, QOI, and MPEG video; DEFLATE/inflate; the framebuffer; and low-level
PS/2, ATA, PCI, and RTL8139 support carried from the shared codebase.

---

## 4. Boot, security & deployment

- **Signed Secure Boot chain:** Microsoft-signed shim → Debian-signed GRUB →
  kernel. The image boots on UEFI firmware with Secure Boot enabled.
- **Encrypted `/data` (real security):** a LUKS2 volume (AES-256-XTS) whose
  passphrase *is* the logon. Unlocking the volume authenticates the user; the
  writable user data and settings persist encrypted at rest via an overlay.
- **Setup installer:** installs Debian95 from the live USB onto an internal disk —
  block-copies the pristine ESP and root, assigns a fresh filesystem UUID,
  repoints GRUB, and lays down the encrypted data partition. Verified to cleanly
  wipe and replace disks that already contain partitions and filesystems.
- **Safe Mode + rescue consoles:** a `nomodeset` text-console boot path and
  additional TTYs for first-boot recovery on unfamiliar hardware.
- **Firmware included:** non-free firmware blobs (Wi-Fi, GPU, microcode) so real
  hardware works out of the box. No telemetry; no forced background updates.

### Build pipeline
A single command (`tools/build-debian95.sh`) produces the bootable image from
source, driven entirely by `debian95.conf` (the single source of truth):

```
Debian rootfs (firmware + signed boot chain)
  → reparenting WM  →  Win95 desktop shell
  → system integration (Plymouth splash / audio / USB persistence / X session)
  → ext4 root image  →  signed GPT/UEFI image
```

Output: `~/debian95/debian-amd64/debian95-usb.img`.

---

## 5. Engineering practices

- **Verification by headless QEMU screenshot.** Features are confirmed by booting
  the real image under QEMU/OVMF (software-rendered virtio-gpu) and capturing the
  actual scanout, driving the UI through QMP keystroke/pointer injection. Login,
  encryption, the installer, and rendering have all been validated this way
  rather than assumed.
- **Failure-path hardening.** Login, installer, and compositor paths have been
  audited and fixed against crash and lockout modes (e.g. SIGPIPE on helper
  failure, stale dm-crypt mappings blocking a correct-password retry,
  partition-size rounding in the installer).
- **Performance.** The compositor uploads only the damaged screen region per
  present, so routine interaction (notably cursor motion) costs a small rectangle
  rather than a full-frame GPU upload.

---

## 6. Project statistics

| Metric | Value |
|---|---|
| Commits | 395 |
| Active development window | 2026-09-17 → 2026-10-01 |
| Source files (C/H/ASM/shell) | 143 |
| C/header lines | ~26,000 |
| Shell + applet components | 50 `.c` modules in `subsystems/win32/` |
| Base | Debian 12 (bookworm), amd64 (arm64 / i386 selectable) |
| Repository | `C:\Users\mrtit\Documents\GitHub\Debian95` (branch `main`, private) |

---

## 7. Scope boundary

This repository is the **Debian Desktop Edition** — the active, sole edition. Two
earlier directions have been retired and are documented only for history:

- The **Appliance (Buildroot/musl) edition** was removed; the SDL/X11 desktop is
  the only build.
- A separate **from-scratch NT-style kernel** (own bootloader → higher-half
  kernel → memory manager → scheduler → object manager → ring 3 → FAT32 →
  framebuffer WM, with a Linux-syscall personality) lives in its own repository
  (`../Debian95-FromScratch`) and is not part of this project.

---

## 8. Status & next steps

The desktop is feature-saturated and the recent focus has shifted to
daily-driver robustness and release engineering. The one remaining untested link
is a **boot on physical hardware** — the signed image is ready to flash and
validate. Likely follow-ups after that: boot-time and app-launch latency
profiling, and continued robustness testing of first-run and recovery paths.
