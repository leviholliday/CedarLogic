#!/bin/bash
# The sync engine's self-test (SYNC.md 7.1 vectors, 7.2 scenarios, the threaded
# engine, the C interface), built from source with OpenSSL's libcrypto, zlib
# and libcurl as the hooks -- no app, no Xcode. Works on a Mac (Homebrew
# openssl@3) and on Linux (libssl-dev, zlib1g-dev, libcurl4-openssl-dev).
#
#   mac/Tools/sync-selftest.sh               build and run
#   ASAN=1 mac/Tools/sync-selftest.sh        with AddressSanitizer + UndefinedBehaviorSanitizer
#   TSAN=1 mac/Tools/sync-selftest.sh        with ThreadSanitizer
#   CL_SYNC_URL=http://localhost:8787/api/sync/v1 mac/Tools/sync-selftest.sh
#                                            also against the mock server (cedarlogic-site
#                                            scripts/sync-mock-server.mjs)
#   ONLY=s26 mac/Tools/sync-selftest.sh      just the checks whose name contains s26
#   CAPI=1 mac/Tools/sync-selftest.sh        through the C interface (cl_sync_self_test) with C hooks
#   GATES=1 mac/Tools/sync-selftest.sh       also: the core's gate library (res/cl_gatedefs.xml) gives
#                                            the digest the same defaults as CedarLogic Online
#                                            (Mac, after mac/build.sh)
#
# On a Mac it also decodes the sync link's QR code with Core Image.
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=${OUT:-mac/build/sync-selftest}
mkdir -p "$OUT"
CXX=${CXX:-clang++}
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter)
NAME=sync_selftest
if [ "${ASAN:-0}" = 1 ]; then FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined); NAME=sync_selftest_asan; fi
if [ "${TSAN:-0}" = 1 ]; then FLAGS+=(-fsanitize=thread); NAME=sync_selftest_tsan; fi
INC=(-Imac/CedarCore -Imac/CedarCore/include -Iformat)
LIBS=(-lcrypto -lz -lcurl)
if [ "$(uname)" = Darwin ]; then
	O=$(brew --prefix openssl@3 2>/dev/null || true)
	[ -n "$O" ] && [ -d "$O/include" ] || { echo "Needs Homebrew's openssl@3 (brew install openssl@3)"; exit 1; }
	INC+=(-I"$O/include"); LIBS=(-L"$O/lib" "${LIBS[@]}")
else
	LIBS+=(-pthread)
fi
SRC=($(ls mac/CedarCore/Sync*.cpp | grep -v SyncGateDefaults) mac/CedarCore/QrCodeGen.cpp mac/Tools/sync_selftest.cpp
	format/circuit_file_io.cpp format/legacy_cdl.cpp format/numeric.cpp format/sexpr.cpp)
echo "Building $OUT/$NAME..."
"$CXX" "${FLAGS[@]}" "${INC[@]}" "${SRC[@]}" "${LIBS[@]}" -o "$OUT/$NAME"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cl-sync-selftest.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
ARGS=("$TMP")
[ -n "${CL_SYNC_URL:-}" ] && ARGS+=(--server "$CL_SYNC_URL")
[ -n "${ONLY:-}" ] && ARGS+=(--only "$ONLY")
[ "${CAPI:-0}" = 1 ] && ARGS+=(--c-api)
status=0
"$OUT/$NAME" "${ARGS[@]}" | tee "$OUT/$NAME.log" | grep -v '^PASS' || true
tail -1 "$OUT/$NAME.log" | grep -q ' 0 failed' || status=1

# The QR code of a sync link, decoded by Core Image (macOS only).
if [ "$(uname)" = Darwin ] && command -v swiftc >/dev/null && [ -z "${ONLY:-}" ]; then
	"$CXX" -std=c++17 -O1 -Imac/CedarCore mac/Tools/sync_qr_dump.cpp mac/CedarCore/QrCodeGen.cpp -o "$OUT/sync_qr_dump"
	swiftc -O mac/Tools/sync-qr-decode.swift -o "$OUT/sync-qr-decode" 2>/dev/null
	LINK="https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4"
	LONG=$(printf 'x%.0s' $(seq 1 1200))
	got=$("$OUT/sync_qr_dump" "$LINK" "hello" "$LONG" | "$OUT/sync-qr-decode")
	want=$(printf '37 %s\n21 hello\n133 %s' "$LINK" "$LONG")
	if [ "$got" = "$want" ]; then echo "PASS qr: Core Image reads the codes back (versions 1, 5, 29)"
	else echo "FAIL qr: Core Image read: $got"; status=1; fi
fi
# The core's gate defaults against the design's table (needs mac/build/libCedarCore.a).
if [ "${GATES:-0}" = 1 ]; then
	CFL=(-std=c++17 -O1 -DCL_NO_WX -D_PRODUCTION_ -Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat
		-Imac/CedarCore -Imac/CedarCore/include -Wno-deprecated-declarations -Wno-inconsistent-missing-override)
	"$CXX" "${CFL[@]}" mac/Tools/sync_gate_defaults_check.cpp mac/build/libCedarCore.a -framework CoreGraphics \
		-framework CoreText -framework ImageIO -framework CoreServices -framework CoreFoundation -framework OpenGL \
		-o "$OUT/sync_gate_defaults_check"
	if "$OUT/sync_gate_defaults_check" res/cl_gatedefs.xml >"$OUT/gates.log"; then echo "PASS gates: $(tail -1 "$OUT/gates.log")"
	else cat "$OUT/gates.log"; status=1; fi
fi
exit $status
