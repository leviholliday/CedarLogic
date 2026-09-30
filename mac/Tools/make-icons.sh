#!/bin/bash
# Makes every CedarLogic icon from res/macos/icon-artwork.png and puts each
# where it's used:
#   mac/App/CedarLogic.icns           the Mac app
#   mac/App/CedarLogicDocument.icns   .cdl files in Finder
#   mac/App/LaunchIcon.png            the launch screen and welcome
#   res/macos/CedarLogic.icns         the wx app on the Mac
#   res/icon.ico, res/icon.png        the wx app on Windows and Linux
#   mac/build/icons/tile-*.png        for the website (favicons and so on)
#   mac/Tools/make-icons.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=mac/build/icons
rm -rf "$OUT"; mkdir -p "$OUT"
swift mac/Tools/make-icons.swift res/macos/icon-artwork.png "$OUT"

iconutil -c icns "$OUT/app.iconset" -o mac/App/CedarLogic.icns
cp mac/App/CedarLogic.icns res/macos/CedarLogic.icns
cp "$OUT/LaunchIcon.png" mac/App/LaunchIcon.png
cp "$OUT/tile-64.png" res/icon.png

# The document icon puts the bare tile on a page.
mkdir -p "$OUT/tile.iconset"
for s in 16 32 128 256 512; do
    sips -z $s $s "$OUT/tile-1024.png" --out "$OUT/tile.iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$OUT/tile-1024.png" --out "$OUT/tile.iconset/icon_${s}x${s}@2x.png" >/dev/null
done
swift mac/Tools/make-doc-icon.swift "$OUT/tile.iconset" "$OUT/doc.iconset"
iconutil -c icns "$OUT/doc.iconset" -o mac/App/CedarLogicDocument.icns

# Windows: an .ico of PNGs, 16 to 256.
python3 - "$OUT" res/icon.ico <<'PY'
import struct, sys, os
out, dest = sys.argv[1], sys.argv[2]
sizes = [16, 24, 32, 48, 64, 128, 256]
blobs = [open(os.path.join(out, f"tile-{s}.png"), "rb").read() for s in sizes]
head = struct.pack("<HHH", 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
entries = b""
for s, b in zip(sizes, blobs):
    entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(b), offset)
    offset += len(b)
open(dest, "wb").write(head + entries + b"".join(blobs))
PY
echo "icons updated"
