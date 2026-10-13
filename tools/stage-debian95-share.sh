#!/usr/bin/env bash
# The SINGLE source of truth for what goes into the desktop's data directory
# (/usr/share/debian95 on the target, build/dev-share on the dev host). Both the
# Buildroot package (linux/package/debian95-desktop/debian95-desktop.mk) and the
# dev runner (tools/devrun.sh) call this, so the dev host and the real device are
# byte-for-byte identical — a screenshot on one always matches the other.
#
#   stage-debian95-share.sh <REPO> <DEST>
#
# Keep this in lock-step with debian95-desktop.mk's file list: change it here only.
set -e
REPO="${1:?usage: stage-debian95-share.sh <REPO> <DEST>}"
DEST="${2:?usage: stage-debian95-share.sh <REPO> <DEST>}"
mkdir -p "$DEST"

# 1. UI font: FONT.LGF is decoded from assets/ui.fon by tools/buildassets.py.
#    Generate it once if missing (buildassets is idempotent; it also writes a
#    build/fat32.img we don't use here — harmless).
if [ ! -f "$REPO/build/FONT.LGF" ]; then
	( cd "$REPO" && python3 tools/buildassets.py >/dev/null 2>&1 ) || true
fi
[ -f "$REPO/build/FONT.LGF" ] && cp -f "$REPO/build/FONT.LGF" "$DEST/FONT.LGF" || true

# 2. Wallpapers + Win95 icon modules + test images — upper-cased 8.3 names to
#    match the FAT-era Fs* callers.
for a in "$REPO"/assets/*.bmp "$REPO"/assets/*.BMP \
         "$REPO"/assets/shell32.dll "$REPO"/assets/user.exe \
         "$REPO"/assets/pifmgr.dll "$REPO"/assets/moricons.dll \
         "$REPO"/assets/mmsys.cpl "$REPO"/assets/main.cpl "$REPO"/assets/comctl32.dll \
         "$REPO"/assets/mspaint.exe \
         "$REPO"/assets/test.png "$REPO"/assets/test.jpg "$REPO"/assets/test.gif \
         "$REPO"/assets/testvid.mpg "$REPO"/assets/testvid.gif "$REPO"/assets/testaud.mpg; do
	[ -f "$a" ] && cp -f "$a" "$DEST/$(basename "$a" | tr a-z A-Z)" || true
done

# 3. Win95 event sounds from the RE'd Windows95 corpus, if checked out beside the repo.
if [ -d "$REPO/../Windows95/design/corpus/files/WINDOWS/MEDIA" ]; then
	for w in "$REPO"/../Windows95/design/corpus/files/WINDOWS/MEDIA/*.wav; do
		[ -f "$w" ] && cp -f "$w" "$DEST/$(basename "$w")" || true
	done
fi
# 4. Any sounds bundled in the repo assets add to / override the above.
for w in "$REPO"/assets/*.wav; do
	[ -f "$w" ] && cp -f "$w" "$DEST/$(basename "$w")" || true
done

# 5. Shipped default settings (e.g. the preferred wallpaper) — a REAL persisted
#    setting the user can still change in Display Properties, not a hard-coded force.
[ -f "$REPO/assets/SETTINGS.INI" ] && cp -f "$REPO/assets/SETTINGS.INI" "$DEST/SETTINGS.INI" || true
