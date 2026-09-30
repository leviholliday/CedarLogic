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

# Windows: an .ico with classic bitmaps up to 64 (what every tool reads,
# the NSIS installer included) and a PNG at 256.
for s in 16 24 32 48 64; do swift mac/Tools/png-to-rgba.swift "$OUT/tile-$s.png" "$OUT/tile-$s.rgba"; done
python3 - "$OUT" res/icon.ico <<'PY'
import struct, sys, os
out, dest = sys.argv[1], sys.argv[2]
entries, blobs = [], []
for s in [16, 24, 32, 48, 64]:
    rgba = open(os.path.join(out, f"tile-{s}.rgba"), "rb").read()
    rows = [rgba[y * s * 4:(y + 1) * s * 4] for y in range(s)]
    bgra = b"".join(bytes(v for i in range(0, len(r), 4) for v in (r[i + 2], r[i + 1], r[i], r[i + 3])) for r in reversed(rows))
    mask_row = ((s + 31) // 32) * 4
    mask = b"\0" * (mask_row * s)   # alpha does the work; the AND mask stays clear
    header = struct.pack("<IiiHHIIiiII", 40, s, s * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0)
    blobs.append(header + bgra + mask); entries.append((s, 32))
blobs.append(open(os.path.join(out, "tile-256.png"), "rb").read()); entries.append((256, 32))
head = struct.pack("<HHH", 0, 1, len(blobs))
offset = 6 + 16 * len(blobs)
table = b""
for (s, bpp), b in zip(entries, blobs):
    table += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, bpp, len(b), offset)
    offset += len(b)
open(dest, "wb").write(head + table + b"".join(blobs))
PY
echo "icons updated"
