#!/bin/bash
# Build and run the share link checks (ShareLinkCodec.swift on its own, no
# engine needed). With the website's fixtures (SITE=<the cedarlogic-site
# checkout>), a link python's zlib made must decode, and the link this makes
# must inflate in python's zlib (raw deflate) to the same file.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/cl-share-check
mkdir -p "$OUT"
swiftc -O -target "$(uname -m)-apple-macos14" mac/Tools/share-check/main.swift mac/App/ShareLinkCodec.swift -o "$OUT/share-check"
if [ -n "${SITE:-}" ]; then
	"$OUT/share-check" "$SITE/scripts/fixtures/practice.link" "$SITE/scripts/fixtures/practice.cdl" "$OUT/made.link"
	python3 - "$OUT/made.link" "$SITE/scripts/fixtures/practice.cdl" <<'PY'
import sys, zlib, base64
d = open(sys.argv[1]).read().strip()
raw = zlib.decompress(base64.urlsafe_b64decode(d + "=" * (-len(d) % 4)), -15)
print(("ok   " if raw == open(sys.argv[2], "rb").read() else "FAIL ") + "python's zlib inflates the Mac's link to the file")
PY
else
	"$OUT/share-check"
fi
rm -rf "$OUT"
