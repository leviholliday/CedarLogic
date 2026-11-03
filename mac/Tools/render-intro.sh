#!/bin/bash
# The intro (App/Intro.swift), drawn headless and made into a video:
#
#   mac/build.sh && mac/Tools/render-intro.sh <out-dir> [first|update]
#
# The app draws every frame at 60 fps to <out-dir>/frames (PNGs at 2x,
# 2560x1600, over a picture of a new circuit's window, the launch screen's
# panel drawn from its own view) and quits; nothing opens on screen. Also
# <out-dir>/wire-compare.png: a real canvas wire (top) over the intro's at
# rest (bottom), at the same scale. Then, with ffmpeg: <out-dir>/intro-<trigger>.mp4 (1280x800,
# H.264) and a small GIF beside it; without ffmpeg but with ImageMagick, just
# the GIF; with neither, the frames alone. Six key frames are copied out as
# <out-dir>/key-1.png ... key-6.png either way. KEEP_FRAMES=0 deletes the
# frames afterwards (they're about 150 MB).
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:?usage: mac/Tools/render-intro.sh <out-dir> [first|update]}
TRIGGER=${2:-first}
APP=mac/build/CedarLogic.app
[ -x "$APP/Contents/MacOS/CedarLogic" ] || { echo "Build first: mac/build.sh"; exit 1; }
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
FRAMES="$OUT/frames-$TRIGGER"
rm -rf "$FRAMES"
"$APP/Contents/MacOS/CedarLogic" --render-intro "$FRAMES" "$TRIGGER"

# The key moments: wires tracing through the C, the D snapping in, the
# pulse landing in the LED, the LED and the version lit as it blooms, the
# icon flying into the launch screen's panel, the panel's sweep.
cp "$FRAMES/wire-compare.png" "$OUT/wire-compare.png"
i=1
for t in 0.55 1.00 1.90 2.02 2.25 2.60; do
	f=$(printf "%04d" "$(echo "$t * 60 / 1" | bc)")
	cp "$FRAMES/frame-$f.png" "$OUT/key-$TRIGGER-$i.png"
	i=$((i + 1))
done

if command -v ffmpeg >/dev/null; then
	ffmpeg -loglevel error -y -framerate 60 -i "$FRAMES/frame-%04d.png" \
		-vf "scale=1280:-2:flags=lanczos,format=yuv420p" -c:v libx264 -preset slow -crf 16 -movflags +faststart \
		"$OUT/intro-$TRIGGER.mp4"
	echo "Video: $OUT/intro-$TRIGGER.mp4"
	ffmpeg -loglevel error -y -framerate 60 -i "$FRAMES/frame-%04d.png" \
		-vf "fps=30,scale=640:-2:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=sierra2_4a" \
		"$OUT/intro-$TRIGGER.gif"
	echo "GIF: $OUT/intro-$TRIGGER.gif"
elif command -v magick >/dev/null || command -v convert >/dev/null; then
	IM=$(command -v magick || command -v convert)
	"$IM" -delay 3.33 -loop 0 "$FRAMES"/frame-*.png -resize 640x "$OUT/intro-$TRIGGER.gif"
	echo "GIF: $OUT/intro-$TRIGGER.gif"
else
	# sips can't make an animated GIF; the frames and key frames are it.
	echo "No ffmpeg or ImageMagick: frames only, in $FRAMES"
fi
echo "Key frames: $OUT/key-$TRIGGER-1.png ... key-$TRIGGER-6.png"
[ "${KEEP_FRAMES:-1}" = 0 ] && rm -rf "$FRAMES"
exit 0
