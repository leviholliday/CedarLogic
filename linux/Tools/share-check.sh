#!/bin/bash
# Build and run Tools/share_check.cpp (GLib only: runs on a Mac with Homebrew's
# glib too). With the website's fixtures (SITE=<the cedarlogic-site checkout>),
# a link python's zlib made must decode, and the link this makes must inflate
# in python's zlib (raw deflate) to the same file.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d)
clang++ -std=c++17 -O1 $(pkg-config --cflags gio-2.0) linux/Tools/share_check.cpp linux/App/ShareLink.cpp $(pkg-config --libs gio-2.0) -o "$OUT/share_check"
if [ -n "${SITE:-}" ]; then
	"$OUT/share_check" "$SITE/scripts/fixtures/practice.link" "$SITE/scripts/fixtures/practice.cdl" "$OUT/made.link"
	python3 - "$OUT/made.link" "$SITE/scripts/fixtures/practice.cdl" <<'PY'
import sys, zlib, base64
d = open(sys.argv[1]).read().strip()
raw = zlib.decompress(base64.urlsafe_b64decode(d + "=" * (-len(d) % 4)), -15)
print(("ok   " if raw == open(sys.argv[2], "rb").read() else "FAIL ") + "python's zlib inflates this link to the file")
PY
else
	"$OUT/share_check"
fi
rm -rf "$OUT"
