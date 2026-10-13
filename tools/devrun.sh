#!/usr/bin/env bash
# Dev-host runner: build the SDL desktop and run it against a data dir staged
# IDENTICALLY to the real target (via tools/stage-debian95-share.sh), so what you
# see on the dev host is exactly what the device boots — no "old version" doubt.
#
#   tools/devrun.sh                       # build + stage + open a live SDL window
#   tools/devrun.sh --shot out.png        # headless: build + stage + save one PNG
#   tools/devrun.sh --shot out.png --media TESTVID.MPG   # open a file first
#   tools/devrun.sh --shot out.png --open 12,20          # auto-open apps (Start ids)
#   tools/devrun.sh --shot out.png --applet 2            # open a Control Panel applet
#
# Runs in WSL/Linux. The staged data dir is build/dev-share (gitignored).
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$REPO/build/dev-share"
SHOT=""; MEDIA=""; OPEN=""; APPLET=""; NOLOGON=1
while [ $# -gt 0 ]; do
	case "$1" in
		--shot)   SHOT="$2"; shift 2 ;;
		--media)  MEDIA="$2"; shift 2 ;;
		--open)   OPEN="$2"; shift 2 ;;
		--applet) APPLET="$2"; shift 2 ;;
		--logon)  NOLOGON=0; shift ;;
		*) echo "unknown arg: $1" >&2; exit 2 ;;
	esac
done

# 1. stage the data dir exactly like the target package does
"$REPO/tools/stage-debian95-share.sh" "$REPO" "$DATA"
# 2. build the SDL desktop
make -C "$REPO/usermode/desktop" debian95-desktop-sdl >/dev/null
BIN="$REPO/usermode/desktop/debian95-desktop-sdl"

env=( "DEBIAN95_DATA=$DATA" )
[ "$NOLOGON" = 1 ] && env+=( "DEBIAN95_NOLOGON=1" )
[ -n "$MEDIA" ]  && env+=( "DEBIAN95_MEDIA=$MEDIA" )
[ -n "$OPEN" ]   && env+=( "DEBIAN95_OPEN=$OPEN" )
[ -n "$APPLET" ] && env+=( "DEBIAN95_APPLET=$APPLET" )

if [ -n "$SHOT" ]; then
	# headless one-shot: render to an offscreen buffer and dump a PPM at ~frame 90
	ppm="/tmp/debian95-shot.ppm"
	env+=( "SDL_VIDEODRIVER=offscreen" "SDL_AUDIODRIVER=dummy" "DEBIAN95_SHOT=$ppm" )
	env "${env[@]}" timeout 6 "$BIN" >/tmp/debian95-dev.log 2>&1 || true
	case "$SHOT" in
		*.png) python3 -c "from PIL import Image; Image.open('$ppm').save('$SHOT')" ;;
		*)     cp -f "$ppm" "$SHOT" ;;
	esac
	echo "shot: $SHOT"
	grep -i "font\|desktop up" /tmp/debian95-dev.log || true
else
	# interactive window (needs a display: WSLg / X11)
	exec env "${env[@]}" "$BIN"
fi
