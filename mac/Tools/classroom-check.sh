#!/bin/bash
# The Mac app's classroom hooks, checked (CLASSROOM.md 10 M): builds a small
# tool from mac/App/SyncHooks.swift and mac/App/ClassroomHooks.swift (the app's
# own CryptoKit, CommonCrypto, Compression, URLSession, file and flock hooks)
# and libCedarCore.a, then runs the classroom core's self-test through them --
# every 7.1 vector, the 7.2 scenarios on the in-process fake server, the
# threaded engine -- plus the hooks' extras (P-256 edge cases, PBKDF2's time,
# the 0600 files in the 0700 folder, the lock).
#
#   mac/Tools/classroom-check.sh                (runs mac/build.sh first if libCedarCore.a is missing)
#   CL_CLASSROOM_URL=http://localhost:8788/api/classroom/v1 mac/Tools/classroom-check.sh
#                                               also a class's round trip against the mock server
#                                               (cedarlogic-site scripts/classroom-mock-server.mjs)
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=mac/build/classroom-check
mkdir -p "$OUT"
[ -f mac/build/libCedarCore.a ] || bash mac/build.sh
ARCH=$(uname -m)
echo "Building $OUT/classroom-check..."
swiftc -O -parse-as-library -target "$ARCH-apple-macos14.0" \
	-import-objc-header mac/CedarCore/include/CedarCore.h \
	mac/App/SyncHooks.swift mac/App/ClassroomHooks.swift mac/Tools/classroom-check.swift mac/build/libCedarCore.a -lc++ \
	-o "$OUT/classroom-check"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cl-classroom-check.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
ARGS=(selftest "$TMP")
[ -n "${CL_CLASSROOM_URL:-}" ] && ARGS+=(--server "$CL_CLASSROOM_URL")
[ -n "${CL_LIVE_URL:-}" ] && ARGS+=(--live "$CL_LIVE_URL")
status=0
"$OUT/classroom-check" "${ARGS[@]}" >"$OUT/selftest.log" 2>&1 || status=1
grep -v '^PASS' "$OUT/selftest.log" || true
[ $status = 0 ] && echo "classroom-check: everything passed" || echo "classroom-check: FAILED (log in $OUT/selftest.log)"
exit $status
