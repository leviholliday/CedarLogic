#!/bin/bash
# Build and run the usage stats checks (App/UsageReport.swift on its own, no
# engine or window) against usage-check/server.py, a stand-in for the site's
# /api/usage implementing the contract in docs/PRIVACY.md: snapshot first,
# delta with only the changed settings, ping when nothing changed,
# haveSnapshot false forcing a snapshot, DELETE on opt-out, reports kept
# (max 7) when it's down, 429, nothing sent when off or policy-disabled, the
# 16 KB cap, the 0600 file and the leak scan. Headless; nothing real is sent.
#   mac/Tools/usage-check.sh            (USAGE_SAMPLES=<file>: also write one report of each kind there)
#   CL_USAGE_URL=https://usage-stats-test--cedarlogic.netlify.app/api/usage mac/Tools/usage-check.sh --live
#                                       snapshot, delta, ping and DELETE against that server (opt in: real network)
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/cl-usage-check.XXXXXX")
trap '[ -n "${SERVER:-}" ] && { kill $SERVER; wait $SERVER || true; } 2>/dev/null; defaults delete usage-check >/dev/null 2>&1 || true; rm -rf "$OUT"' EXIT
swiftc -O -target "$(uname -m)-apple-macos14" mac/Tools/usage-check/main.swift mac/App/UsageReport.swift -o "$OUT/usage-check" 2>&1 \
	| grep -v "^$" || true
[ -x "$OUT/usage-check" ] || { echo "usage-check: didn't build"; exit 1; }
if [ "${1:-}" = --live ]; then
	[ -n "${CL_USAGE_URL:-}" ] || { echo "usage-check --live: set CL_USAGE_URL to the site or a draft"; exit 1; }
	"$OUT/usage-check" "$OUT" --live
	exit $?
fi
python3 mac/Tools/usage-check/server.py "$OUT/port" &
SERVER=$!
for _ in $(seq 50); do [ -s "$OUT/port" ] && break; sleep 0.1; done
CL_USAGE_URL="http://127.0.0.1:$(cat "$OUT/port")" "$OUT/usage-check" "$OUT"
