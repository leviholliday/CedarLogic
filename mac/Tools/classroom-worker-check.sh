#!/bin/bash
# The classroom client against the Cloudflare Worker on this computer (CLASSROOM.md 3.13,
# 3.14): starts it with the website's cloudflare/classroom/test/wrangler.mjs (`wrangler dev`:
# workerd and SQLite here, nothing on Cloudflare; run `npm install` in cloudflare/classroom
# once), runs the checks below against it, and stops it.
#
#   1. classroom-selftest.sh               the portable core (OpenSSL, curl) with WebSockets
#                                          through node (classroom-ws-relay.mjs): a class's round
#                                          trip and a live round (sockets, a blocked student's
#                                          held polls, answers, a removal, a deletion)
#   2. NO_RELAY=1 classroom-selftest.sh    the same live round with no sockets: held polls only
#   3. classroom-check.sh                  the Mac's own hooks (URLSession's HTTP and WebSockets);
#                                          after mac/build.sh
#   4. classroom-interop.sh --worker       the web core and the C++ core together (it starts a
#                                          Worker of its own)
#
#   CEDARLOGIC_SITE=/path/to/cedarlogic-site mac/Tools/classroom-worker-check.sh
#   PORT=8790 mac/Tools/classroom-worker-check.sh        another port (8788 by default)
#   SKIP_MAC=1 / SKIP_INTEROP=1                          leave out 3 / 4
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
SITE=${CEDARLOGIC_SITE:-$ROOT/../cedarlogic-site}
W="$SITE/cloudflare/classroom/test/wrangler.mjs"
[ -f "$W" ] || { echo "No $W: set CEDARLOGIC_SITE to the website's checkout"; exit 1; }
PORT=${PORT:-8788}
LOG=$(mktemp "${TMPDIR:-/tmp}/cl-worker.XXXXXX")
node "$W" --port "$PORT" >"$LOG" 2>&1 &
WPID=$!
trap 'kill $WPID 2>/dev/null; wait $WPID 2>/dev/null; rm -f "$LOG"' EXIT
for _ in $(seq 1 120); do
	grep -q '"listening"' "$LOG" && break
	kill -0 $WPID 2>/dev/null || { cat "$LOG"; echo "The Worker didn't start"; exit 1; }
	sleep 0.5
done
grep -q '"listening"' "$LOG" || { cat "$LOG"; echo "The Worker didn't start in a minute"; exit 1; }
export CL_CLASSROOM_URL="http://localhost:$PORT/api/classroom/v1" CL_LIVE_URL="http://localhost:$PORT/api/live/v1"
reset() { curl -s -X POST "http://localhost:$PORT/__mock/reset" >/dev/null; }   # a fresh address allowance each time

status=0
run() {
	local name=$1
	shift
	reset
	echo "== $name"
	if "$@"; then echo "== $name: passed"; else echo "== $name: FAILED"; status=1; fi
}
run "core, WebSockets through node" bash mac/Tools/classroom-selftest.sh
run "core, held polls only" env NO_RELAY=1 bash mac/Tools/classroom-selftest.sh
[ "${SKIP_MAC:-0}" = 1 ] || run "the Mac's hooks" bash mac/Tools/classroom-check.sh
[ "${SKIP_INTEROP:-0}" = 1 ] || run "interop with the web core" env CEDARLOGIC_SITE="$SITE" CL_SERVER=worker bash mac/Tools/classroom-interop.sh
exit $status
