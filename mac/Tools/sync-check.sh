#!/bin/bash
# The Mac app's sync hooks, checked (SYNC.md 10 M): builds a small tool from
# mac/App/SyncHooks.swift (the app's own CryptoKit, Compression, URLSession,
# secret-file and flock hooks) and libCedarCore.a, then runs
#
#   1. the engine's self-test through those hooks: every 7.1 vector and the
#      7.2 scenarios on the in-process fake server, plus the hooks' extras
#      (cut-short inflate, the 0600 secret, the lock, http only to localhost);
#   2. if the website's mock server is around (cedarlogic-site's
#      scripts/sync-mock-server.mjs), the same against it over HTTP, then a
#      run of two real engines -- two libraries, as two Macs -- through it:
#      Turn On, preview, Link, edits both ways, a dirty window left alone,
#      a delete, quitting, Turn Off.
#   3. adding a device by scanning (SYNC.md 11): this Mac as D shows a QR code,
#      a phone written in node (mac/Tools/sync-pair-answer.mjs) answers it, and
#      D previews and links with what came; a cancelled QR code leaves nothing
#      on the server. Needs a mock server that has pairing (it checks).
#
#   mac/Tools/sync-check.sh                  (runs mac/build.sh first if libCedarCore.a is missing)
#   CL_SITE=<cedarlogic-site checkout> mac/Tools/sync-check.sh
#   CL_SYNC_URL=http://localhost:8787/api/sync/v1 mac/Tools/sync-check.sh   (a mock server already running)
#   NO_SERVER=1 mac/Tools/sync-check.sh      (the fake server only)
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=mac/build/sync-check
mkdir -p "$OUT"
[ -f mac/build/libCedarCore.a ] || bash mac/build.sh
ARCH=$(uname -m)
echo "Building $OUT/sync-check..."
swiftc -O -parse-as-library -target "$ARCH-apple-macos14.0" \
	-import-objc-header mac/CedarCore/include/CedarCore.h \
	mac/App/SyncHooks.swift mac/Tools/sync-check.swift mac/build/libCedarCore.a -lc++ \
	-o "$OUT/sync-check"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cl-sync-check.XXXXXX")
SERVER_PID=
cleanup() {
	[ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
	rm -rf "$TMP"
}
trap cleanup EXIT
status=0

echo "== Self-test with the Mac hooks (fake server)"
if "$OUT/sync-check" selftest "$TMP/fake" >"$OUT/selftest.log" 2>&1; then :; else status=1; fi
grep -v '^PASS' "$OUT/selftest.log" || true

# The mock server: one that's running (CL_SYNC_URL), or the website's, started here.
URL=${CL_SYNC_URL:-}
if [ -z "$URL" ] && [ "${NO_SERVER:-0}" != 1 ] && command -v node >/dev/null; then
	SITE=${CL_SITE:-}
	if [ -z "$SITE" ]; then
		for d in "../cedarlogic-site-sync-s" "../cedarlogic-site" "$(git rev-parse --git-common-dir)/../../cedarlogic-site-sync-s" \
			"$(git rev-parse --git-common-dir)/../../cedarlogic-site"; do
			[ -f "$d/scripts/sync-mock-server.mjs" ] && { SITE=$d; break; }
		done
	fi
	if [ -n "$SITE" ] && [ -f "$SITE/scripts/sync-mock-server.mjs" ]; then
		node "$SITE/scripts/sync-mock-server.mjs" --port 0 >"$TMP/server.out" 2>"$TMP/server.err" &
		SERVER_PID=$!
		for _ in $(seq 1 100); do
			URL=$(sed -n '1s/.*"url":"\([^"]*\)".*/\1/p' "$TMP/server.out" 2>/dev/null || true)
			[ -n "$URL" ] && break
			sleep 0.1
		done
		[ -n "$URL" ] || { echo "The mock server didn't start:"; cat "$TMP/server.err"; status=1; }
	else
		echo "(No mock server: set CL_SITE to a cedarlogic-site checkout with scripts/sync-mock-server.mjs.)"
	fi
fi

if [ -n "$URL" ]; then
	echo "== Self-test with the Mac hooks against the mock server ($URL)"
	if "$OUT/sync-check" selftest "$TMP/mock" --server "$URL" >"$OUT/selftest-mock.log" 2>&1; then :; else status=1; fi
	grep -v '^PASS' "$OUT/selftest-mock.log" || true
	echo "== Two engines through the mock server"
	if CL_SYNC_URL=$URL "$OUT/sync-check" engine "$TMP/engine" format/tests/fixtures/Lab5.cdl format/tests/fixtures/lab6.cdl \
		>"$OUT/engine.log" 2>&1; then :; else status=1; fi
	grep -v '^PASS' "$OUT/engine.log" || true
fi

if [ -n "$URL" ] && command -v node >/dev/null; then
	if curl -s --max-time 5 "$URL/health" | grep -q pairSeconds; then
		echo "== Adding a device by scanning, through the mock server"
		PHONE="Sam’s phone"
		CL_SYNC_URL=$URL CL_PAIR_L_NAME=$PHONE "$OUT/sync-check" pair "$TMP/pair" format/tests/fixtures/Lab5.cdl >"$OUT/pair.log" 2>&1 &
		D_PID=$!
		node mac/Tools/sync-pair-answer.mjs "$URL" "$TMP/pair" "$PHONE" >"$OUT/pair-l.log" 2>&1 || status=1
		wait "$D_PID" || status=1
		grep -v '^PASS' "$OUT/pair.log" || true
		cat "$OUT/pair-l.log"
	else
		echo "(The mock server has no pairing yet: skipping the pairing round.)"
	fi
fi

[ $status = 0 ] && echo "sync-check: everything passed" || echo "sync-check: FAILED (logs in $OUT)"
exit $status
