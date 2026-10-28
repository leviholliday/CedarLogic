#!/bin/bash
# Build the engine and fuzz_edit with AddressSanitizer + UndefinedBehaviorSanitizer and run it.
#   fuzz-check.sh <first-seed> <seed-count> <ops-per-seed> [start.cdl...]
# (For many seeds in parallel, each with a time cap, run the binary this builds
# through fuzz-sweep.py; mac/Tools/headless builds it for any machine.)
set -euo pipefail
if [ $# -lt 3 ]; then
	echo "usage: $0 <first-seed> <seed-count> <ops-per-seed> [start.cdl...]" >&2
	exit 2
fi
cd "$(dirname "$0")/../.."
OBJ=mac/build/fuzz-obj; mkdir -p "$OBJ"
SAN=address,undefined
FLAGS=(-std=c++17 -O1 -g -fsanitize=$SAN -fno-sanitize-recover=undefined -fno-omit-frame-pointer -DCL_NO_WX -D_PRODUCTION_
	-Wno-deprecated-declarations -Wno-inconsistent-missing-override
	-Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include)
SRC=$(sed -n '/^CORE=(/,/^)/p' mac/build.sh | grep -v '^CORE=(\|^)\|^\s*#')
SRC=$(eval echo $SRC)
OBJS=(); PIDS=()
for f in $SRC; do
	o="$OBJ/$(echo "$f" | tr / _).o"; d="${o%.o}.d"; OBJS+=("$o")
	stale=0
	if [ ! -f "$o" ] || [ ! -f "$d" ] || [ "$f" -nt "$o" ]; then stale=1
	else for h in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$d"); do [ "$h" -nt "$o" ] && { stale=1; break; }; done; fi
	if [ $stale = 1 ]; then
		clang++ "${FLAGS[@]}" -MMD -MF "$d" -c "$f" -o "$o" &
		PIDS+=($!)
		while [ "$(jobs -r | wc -l)" -ge 8 ]; do sleep 0.5; done
	fi
done
for p in ${PIDS[@]+"${PIDS[@]}"}; do wait "$p"; done
clang++ "${FLAGS[@]}" -c mac/Tools/fuzz_edit.cpp -o "$OBJ/fuzz_edit.o"
clang++ -fsanitize=$SAN "${OBJS[@]}" "$OBJ/fuzz_edit.o" -framework CoreGraphics -framework CoreText -framework ImageIO \
	-framework CoreServices -framework CoreFoundation -framework OpenGL -o mac/build/fuzz_edit
mac/build/fuzz_edit res/cl_gatedefs.xml "$@"
