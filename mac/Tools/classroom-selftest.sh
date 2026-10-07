#!/bin/bash
# The classroom core's self-test (CLASSROOM.md 7.1 vectors, 7.2 scenarios, the
# threaded engine, the C interface), built from source with OpenSSL's libcrypto
# (P-256, PBKDF2, AES-GCM), zlib and libcurl as the hooks -- no app, no Xcode.
# Works on a Mac (Homebrew openssl@3) and on Linux (libssl-dev, zlib1g-dev,
# libcurl4-openssl-dev).
#
#   mac/Tools/classroom-selftest.sh             build and run
#   ASAN=1 mac/Tools/classroom-selftest.sh      with AddressSanitizer + UndefinedBehaviorSanitizer
#   TSAN=1 mac/Tools/classroom-selftest.sh      with ThreadSanitizer
#   CAPI=1 mac/Tools/classroom-selftest.sh      through the C interface (cl_classroom_self_test) with C hooks
#   ONLY=s27 mac/Tools/classroom-selftest.sh    just the checks whose name contains s27
#   VECTORS=../cedarlogic-site/tests/classroom/vectors.json mac/Tools/classroom-selftest.sh
#                                               the vectors of another copy of the file (vectors only)
#   CL_CLASSROOM_URL=http://localhost:8788/api/classroom/v1 mac/Tools/classroom-selftest.sh
#                                               also a class's round trip and a live round against a
#                                               real server: the Worker (cedarlogic-site:
#                                               node cloudflare/classroom/test/wrangler.mjs --port 8788)
#                                               or the mock server. With node on the PATH the live
#                                               round has WebSockets (mac/Tools/classroom-ws-relay.mjs);
#                                               NO_RELAY=1 runs it on held polls alone.
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=${OUT:-mac/build/classroom-selftest}
mkdir -p "$OUT"
CXX=${CXX:-clang++}
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter)
NAME=classroom_selftest
if [ "${ASAN:-0}" = 1 ]; then FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined); NAME=classroom_selftest_asan; fi
if [ "${TSAN:-0}" = 1 ]; then FLAGS+=(-fsanitize=thread); NAME=classroom_selftest_tsan; fi
INC=(-Imac/CedarCore -Imac/CedarCore/include)
LIBS=(-lcrypto -lz -lcurl)
if [ "$(uname)" = Darwin ]; then
	O=$(brew --prefix openssl@3 2>/dev/null || true)
	[ -n "$O" ] && [ -d "$O/include" ] || { echo "Needs Homebrew's openssl@3 (brew install openssl@3)"; exit 1; }
	INC+=(-I"$O/include"); LIBS=(-L"$O/lib" "${LIBS[@]}")
else
	LIBS+=(-pthread)
fi
# The classroom core and what it builds on (the sync engine's protocol and JSON).
SRC=(mac/CedarCore/Classroom*.cpp mac/CedarCore/SyncProtocol.cpp mac/CedarCore/SyncJson.cpp mac/CedarCore/QrCodeGen.cpp
	mac/Tools/classroom_selftest.cpp)
echo "Building $OUT/$NAME..."
"$CXX" "${FLAGS[@]}" "${INC[@]}" "${SRC[@]}" "${LIBS[@]}" -o "$OUT/$NAME"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cl-classroom-selftest.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
ARGS=("$TMP")
[ -n "${CL_CLASSROOM_URL:-}" ] && ARGS+=(--server "$CL_CLASSROOM_URL")
[ -n "${CL_LIVE_URL:-}" ] && ARGS+=(--live "$CL_LIVE_URL")
[ -n "${ONLY:-}" ] && ARGS+=(--only "$ONLY")
[ -n "${VECTORS:-}" ] && ARGS+=(--vectors "$VECTORS")
[ "${CAPI:-0}" = 1 ] && ARGS+=(--c-api)
if [ -n "${CL_CLASSROOM_URL:-}" ] && [ "${NO_RELAY:-0}" != 1 ] && command -v node >/dev/null; then
	ARGS+=(--relay mac/Tools/classroom-ws-relay.mjs)
fi
status=0
"$OUT/$NAME" "${ARGS[@]}" | tee "$OUT/$NAME.log" | grep -v '^PASS' || true
tail -1 "$OUT/$NAME.log" | grep -q ' 0 failed' || status=1
exit $status
