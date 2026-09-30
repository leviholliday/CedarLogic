#!/bin/bash
# The toolbar's real-mouse test: clicks, hover, tooltips, dragging, double-
# click to fill, focus mode's tab strip and full screen, with real mouse
# events, then PASS or FAIL for each. Run it after changing anything in the
# title bar row; nothing else catches what goes wrong up there.
#
#   mac/Tools/bar-test.sh            test mac/build/CedarLogic.app
#   SCREEN=1 mac/Tools/bar-test.sh   on the second display
#   STYLE=minimal mac/Tools/bar-test.sh   the Minimal toolbar (for this run
#                                         only; your setting stays)
#
# It moves the pointer for about a minute: leave the mouse alone. It needs
# the terminal (or Claude) to be allowed to control the computer (System
# Settings > Privacy & Security > Accessibility). The app opens a new,
# untitled circuit and logs tool clicks instead of doing them, so nothing
# of yours changes.
set -uo pipefail
cd "$(dirname "$0")/../.."
APP="$PWD/mac/build/CedarLogic.app"
OUT="${TMPDIR:-/tmp}/cl-bar-test"
rm -rf "$OUT"; mkdir -p "$OUT"
swiftc -O mac/Tools/bar-test.swift -o "$OUT/bar-test" || exit 2
ARGS=()
case "${STYLE:-}" in
	minimal) ARGS=(--args -cl.toolbarStyle 2) ;;
	classic) ARGS=(--args -cl.toolbarStyle 0) ;;
	seamless) ARGS=(--args -cl.toolbarStyle 3) ;;
esac
open -n --env CL_BAR_TEST="$OUT" --env CL_NO_SPLASH=1 --env CL_BAR_SCREEN="${SCREEN:-0}" "$APP" ${ARGS[@]+"${ARGS[@]}"}
PID=""
for i in $(seq 1 50); do PID=$(pgrep -f "mac/build/CedarLogic.app/Contents/MacOS/CedarLogic" | head -1); [ -n "$PID" ] && break; sleep 0.1; done
[ -n "$PID" ] || { echo "The app didn't start."; exit 2; }
"$OUT/bar-test" "$OUT" "$PID"
STATUS=$?
kill "$PID" 2>/dev/null; sleep 0.3; kill -9 "$PID" 2>/dev/null
echo "(logs: $OUT)"
exit $STATUS
