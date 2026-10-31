#!/bin/bash
# The same editing session must give the same circuit every time it runs.
# Runs fuzz_edit sessions several times each and compares the circuits they
# save (before their last operation, CL_FUZZ_SAVEBEFORE); fails on any
# difference. A difference means the engine's result hangs on something other
# than the edits: memory addresses (a set of pointers walked in order, as
# checkHotspots and startSegDrag did), the clock, uninitialised memory.
#
#   mac/Tools/fuzz-determinism.sh <fuzz_edit> [first-seed] [count] [ops] [runs]
#
# Defaults: seeds 1-20, 200 operations, 4 runs each, both the basic and the
# wide set of operations. Run from anywhere; the fixtures come from the repo.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$1"; FIRST="${2:-1}"; COUNT="${3:-20}"; OPS="${4:-200}"; RUNS="${5:-4}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/cl-determinism.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
cd "$ROOT"
bad=0; ran=0
for mode in basic wide; do
	for ((seed = FIRST; seed < FIRST + COUNT; seed++)); do
		first=""
		for ((run = 1; run <= RUNS; run++)); do
			dir="$OUT/$mode-$seed-$run"; mkdir -p "$dir"
			if [ "$mode" = wide ]; then
				CL_FUZZ_WIDE=1 CL_FUZZ_NOSIM=1 CL_FUZZ_SAVEBEFORE=$((OPS - 1)) CL_FUZZ_DUMP="$dir" \
					"$BIN" res/cl_gatedefs.xml "$seed" 1 "$OPS" > "$dir/out.txt" 2>&1
			else
				CL_FUZZ_NOSIM=1 CL_FUZZ_SAVEBEFORE=$((OPS - 1)) CL_FUZZ_DUMP="$dir" \
					"$BIN" res/cl_gatedefs.xml "$seed" 1 "$OPS" > "$dir/out.txt" 2>&1
			fi
			if [ ! -s "$dir/before-op.cdl" ]; then
				echo "FAIL $mode seed $seed run $run: saved nothing"; tail -3 "$dir/out.txt"; bad=$((bad + 1)); continue
			fi
			if [ -z "$first" ]; then first="$dir/before-op.cdl"
			elif ! cmp -s "$first" "$dir/before-op.cdl"; then
				echo "FAIL $mode seed $seed: run $run saved a different circuit from run 1"
				diff "$first" "$dir/before-op.cdl" | head -12
				bad=$((bad + 1))
			fi
		done
		ran=$((ran + 1))
	done
done
echo "$ran sessions run $RUNS times each: $bad difference(s)"
[ "$bad" = 0 ]
