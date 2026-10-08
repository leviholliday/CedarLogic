#!/bin/bash
# Overnight stress test of Straighten (see straighten_lab.cpp / _driver.py).
#
#   mac/Tools/straighten-lab.sh [hours=10] [results dir]
#
# Runs detached at low priority with the Mac kept awake (caffeinate -i), writes
# its PID to <results>/lab.pid and its log to <results>/lab.log. Re-running
# resumes (seeds continue; kept cases stay). Stop it with:
#   kill $(cat "<results>/lab.pid")      (or: touch "<results>/STOP")
# Needs mac/build/libCedarCore.a (mac/build.sh); builds the tool itself.
set -euo pipefail
cd "$(dirname "$0")/../.."
HOURS=${1:-10}
OUT=${2:-"/Users/leviholliday/Coding Projects/2026/straighten-lab-results"}
mkdir -p "$OUT"
rm -f "$OUT/STOP"
if [ -f "$OUT/lab.pid" ] && kill -0 "$(cat "$OUT/lab.pid")" 2>/dev/null; then
	echo "already running: PID $(cat "$OUT/lab.pid")"; exit 1
fi
clang++ -std=c++17 -O2 -DCL_NO_WX -Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include \
	-Wno-deprecated-declarations -Wno-inconsistent-missing-override mac/Tools/straighten_lab.cpp mac/build/libCedarCore.a \
	-framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices -framework CoreFoundation -framework OpenGL \
	-o mac/build/straighten_lab
cp mac/build/straighten_lab "$OUT/straighten_lab"   # the run uses its own copy, so rebuilds don't disturb it
cp res/cl_gatedefs.xml "$OUT/cl_gatedefs.xml"
cp mac/Tools/straighten_lab_driver.py "$OUT/driver.py"
FIX=()
while IFS= read -r f; do FIX+=("$f"); done < <(find "$PWD/format/tests/fixtures" "$PWD/tests/fixtures" "$PWD/res/samples" -name '*.cdl' 2>/dev/null | sort)
nohup nice -n 10 python3 -u "$OUT/driver.py" "$OUT/straighten_lab" "$OUT/cl_gatedefs.xml" "$OUT" "$HOURS" "${FIX[@]}" \
	>> "$OUT/lab.log" 2>&1 < /dev/null &
echo $! > "$OUT/lab.pid"
nohup caffeinate -i -w "$(cat "$OUT/lab.pid")" >/dev/null 2>&1 < /dev/null &
echo "started PID $(cat "$OUT/lab.pid") for $HOURS h; results in $OUT; stop: kill \$(cat \"$OUT/lab.pid\")"
