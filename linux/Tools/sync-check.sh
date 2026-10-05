#!/bin/bash
# Build and run Tools/sync_check.cpp: sync's checks with the hooks the app uses
# (OpenSSL's libcrypto, GIO's zlib, the curl program), no window needed. Works on
# Linux (libssl-dev, libglib2.0-dev, curl) and, to check the code here, on a Mac
# with Homebrew's openssl@3 and glib.
#
#   linux/Tools/sync-check.sh                  vectors, scenarios, the threaded engine, the C interface
#   MOCK=1 linux/Tools/sync-check.sh           and the app's own checks against Tools/sync_mock.py:
#                                              the curl program's use (no token on its command line,
#                                              no temp file left) and two libraries syncing
#   SITE=<cedarlogic-site checkout> linux/Tools/sync-check.sh
#                                              also the engine's full scenarios over HTTP against the
#                                              website's mock server (needs node)
#   ASAN=1 linux/Tools/sync-check.sh           with AddressSanitizer + UndefinedBehaviorSanitizer
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=${OUT:-linux/build/sync-check}
mkdir -p "$OUT"
CXX=${CXX:-c++}
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter)
if [ "${ASAN:-0}" = 1 ]; then FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined); fi
INC=(-Imac/CedarCore -Imac/CedarCore/include -Iformat -Ilinux/App $(pkg-config --cflags gio-2.0))
LIBS=($(pkg-config --libs gio-2.0) -lcrypto -pthread)
if [ "$(uname)" = Darwin ]; then
	O=$(brew --prefix openssl@3 2>/dev/null || true)
	[ -n "$O" ] && [ -d "$O/include" ] || { echo "Needs Homebrew's openssl@3 (brew install openssl@3)"; exit 1; }
	INC+=(-I"$O/include"); LIBS=(-L"$O/lib" "${LIBS[@]}")
fi
SRC=($(ls mac/CedarCore/Sync*.cpp | grep -v SyncGateDefaults) mac/CedarCore/QrCodeGen.cpp
	linux/App/SyncPlatform.cpp linux/Tools/sync_check.cpp
	format/circuit_file_io.cpp format/legacy_cdl.cpp format/numeric.cpp format/sexpr.cpp)
echo "Building $OUT/sync_check..."
"$CXX" "${FLAGS[@]}" "${INC[@]}" "${SRC[@]}" "${LIBS[@]}" -o "$OUT/sync_check"

status=0
"$OUT/sync_check" | tee "$OUT/selftest.log" | grep -v '^PASS' || true
tail -2 "$OUT/selftest.log" | grep -q 'all passed' || status=1

MOCKPID=""
cleanup() { [ -n "$MOCKPID" ] && kill "$MOCKPID" 2>/dev/null || true; }
trap cleanup EXIT
# Starts a server's script, reads the first line it prints (its port) and sets URL.
start() {
	local log=$1; shift
	"$@" > "$log" 2>&1 &
	MOCKPID=$!
	for _ in $(seq 1 100); do grep -q listening "$log" 2>/dev/null && break; sleep 0.1; done
	URL=$(sed -n 's/.*"url": *"\([^"]*\)".*/\1/p' "$log" | head -1 | sed 's/localhost/127.0.0.1/')
	[ -n "$URL" ] || { echo "the server didn't start"; cat "$log"; status=1; return 1; }
}
if [ "${MOCK:-0}" = 1 ]; then
	start "$OUT/mock.log" python3 linux/Tools/sync_mock.py --port 0 || exit 1
	"$OUT/sync_check" --curl-check "$URL" | grep -v '^PASS' || true
	"$OUT/sync_check" --curl-check "$URL" | tail -1 | grep -q 'all passed' || status=1
	"$OUT/sync_check" --roundtrip "$URL" | tee "$OUT/roundtrip.log" | grep -v '^PASS' || true
	tail -1 "$OUT/roundtrip.log" | grep -q 'all passed' || status=1
	cleanup; MOCKPID=""
fi
if [ -n "${SITE:-}" ]; then
	start "$OUT/site.log" node "$SITE/scripts/sync-mock-server.mjs" --port 0 || exit 1
	CL_SYNC_URL="$URL" "$OUT/sync_check" | tee "$OUT/site-selftest.log" | grep -v '^PASS' || true
	tail -2 "$OUT/site-selftest.log" | grep -q 'all passed' || status=1
	cleanup; MOCKPID=""
fi
exit $status
