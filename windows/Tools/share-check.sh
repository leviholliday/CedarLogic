#!/bin/bash
# Checks the share link format (windows/App/ShareCodec.cpp) on any machine with
# a C++17 compiler and python3: the app's own --share-test checks, with the
# sanitizers on. With SITE=<the cedarlogic-site checkout>, a link the website
# made (scripts/fixtures/practice.link) must decode to its circuit, and the
# link made here must inflate in python's zlib (raw deflate) to the same file.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
${CXX:-clang++} -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	windows/Tools/share_check.cpp windows/App/ShareCodec.cpp windows/App/Deflate.cpp -o "$OUT/share_check"
if [ -n "${SITE:-}" ]; then
	"$OUT/share_check" "$SITE/scripts/fixtures/practice.cdl" "$SITE/scripts/fixtures/practice.link" "$OUT/made.link"
	python3 - "$OUT/made.link" "$SITE/scripts/fixtures/practice.cdl" <<'PY'
import sys, zlib, base64
d = open(sys.argv[1]).read().strip()
raw = zlib.decompress(base64.urlsafe_b64decode(d + "=" * (-len(d) % 4)), -15)
print(("PASS  " if raw == open(sys.argv[2], "rb").read() else "FAIL  ") + "python's zlib inflates this link to the file")
PY
else
	"$OUT/share_check" res/samples/practice.cdl
fi
