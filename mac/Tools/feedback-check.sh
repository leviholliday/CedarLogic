#!/bin/bash
# Build and run the Send Feedback client checks (FeedbackServer.swift on its
# own, no engine or window) against feedback-check/server.py, a stand-in for
# the site's /api/feedback: install id, required summary, the 10-a-day cap
# (not retried), Request More, other 429s still retried. Headless.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d "${TMPDIR:-/tmp}/cl-feedback-check.XXXXXX")
trap '[ -n "${SERVER:-}" ] && { kill $SERVER; wait $SERVER; } 2>/dev/null; defaults delete feedback-check >/dev/null 2>&1; rm -rf "$OUT"' EXIT
swiftc -O -target "$(uname -m)-apple-macos14" mac/Tools/feedback-check/main.swift mac/App/FeedbackServer.swift -o "$OUT/feedback-check"
python3 mac/Tools/feedback-check/server.py "$OUT/port" &
SERVER=$!
for _ in $(seq 50); do [ -s "$OUT/port" ] && break; sleep 0.1; done
CL_FEEDBACK_URL="http://127.0.0.1:$(cat "$OUT/port")" CL_FEEDBACK_KEY=check "$OUT/feedback-check"
