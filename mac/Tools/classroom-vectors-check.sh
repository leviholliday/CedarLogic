#!/bin/bash
# The classroom vectors (docs/CLASSROOM.md 7.1) checked with the Mac's own
# primitives: builds mac/Tools/classroom-vectors-check.swift (CryptoKit,
# CommonCrypto, Compression; no CedarCore needed) and runs it on
# tests/classroom/vectors.json, or on the file named as the argument (the
# website checkout's copy, say). Every line is PASS or FAIL; the exit code is
# the number of failures.
#
#   mac/Tools/classroom-vectors-check.sh
#   mac/Tools/classroom-vectors-check.sh ../cedarlogic-site/tests/classroom/vectors.json
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=mac/build/classroom-vectors-check
mkdir -p mac/build
ARCH=$(uname -m)
echo "Building $OUT..."
swiftc -O -target "$ARCH-apple-macos14.0" mac/Tools/classroom-vectors-check.swift -o "$OUT"
"$OUT" "${1:-tests/classroom/vectors.json}"
