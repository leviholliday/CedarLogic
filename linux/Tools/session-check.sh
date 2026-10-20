#!/bin/bash
# A long working session in a circuit window, on a virtual display
# (App/SessionCheck.cpp):
#
#   linux/Tools/session-check.sh <cedarlogic> [actions] [seeds...]
#
# Runs `<cedarlogic> --session-check <actions> <seed>` under xvfb-run for each
# seed (default 300 actions, seeds 1-4), then each seed again with
# G_DEBUG=fatal-criticals (a GTK critical stops it there, with the action it
# came in), and the first seed with the canvas always drawn through its frame
# and whole (CEDARLOGIC_CANVAS_FRAME=1 CEDARLOGIC_DRAW_ALL=1). The app keeps
# its settings and Your Circuits in a temporary folder of its own, so yours
# are never touched. A run passes when every check passes, it exits 0 within
# the time cap (SESSION_TIMEOUT, 420 s: past it, it hung) and nothing printed
# a GTK or GLib warning. Logs go in $SESSION_LOGS (default session-logs).
set -uo pipefail
APP=${1:?usage: session-check.sh <cedarlogic> [actions] [seeds...]}
shift
ACTIONS=${1:-300}
shift || true
SEEDS=("$@")
[ ${#SEEDS[@]} -gt 0 ] || SEEDS=(1 2 3 4)
OUT=${SESSION_LOGS:-session-logs}
CAP=${SESSION_TIMEOUT:-420}
mkdir -p "$OUT"
fail=0

run() {
	local name=$1 seed=$2
	shift 2
	local log="$OUT/session-$name.log"
	env "$@" xvfb-run -a -s "-screen 0 1600x1000x24" timeout -k 10 "$CAP" "$APP" --session-check "$ACTIONS" "$seed" >"$log" 2>&1
	local code=$?
	local warnings
	warnings=$(grep -c -E "(CRITICAL|WARNING) \*\*" "$log")
	grep -E "^(PASS|FAIL|session|memory|actions|  )" "$log" | sed "s/^/[$name] /"
	if [ "$code" = 0 ] && [ "$warnings" = 0 ]; then
		echo "PASS session $name"
	else
		local why="exit $code"
		[ "$code" = 124 ] && why="timed out after $CAP s (a hang?)"
		echo "FAIL session $name: $why, $warnings warning line(s); the end of $log:"
		tail -60 "$log" | sed "s/^/    /"
		# A crash's backtrace, with the functions' names.
		if grep -q "^FAIL no crash" "$log" && command -v addr2line >/dev/null; then
			echo "    backtrace:"
			grep -o "$(basename "$APP")(+0x[0-9a-f]*)" "$log" | sed 's/.*(+\(0x[0-9a-f]*\))/\1/' | head -40 |
				xargs -r addr2line -f -C -p -e "$APP" | sed "s/^/      /"
		fi
		fail=1
	fi
}

for s in "${SEEDS[@]}"; do run "seed$s" "$s"; done
for s in "${SEEDS[@]}"; do run "seed$s-fatal-criticals" "$s" G_DEBUG=fatal-criticals; done
run "seed${SEEDS[0]}-frame-whole" "${SEEDS[0]}" CEDARLOGIC_CANVAS_FRAME=1 CEDARLOGIC_DRAW_ALL=1
[ $fail = 0 ] && echo "session-check.sh: every run passed" || echo "session-check.sh: some runs FAILED"
exit $fail
