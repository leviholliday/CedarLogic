#!/bin/bash
# Time the engine's drag step (optimised build, no sanitizers).
#   bench-drag.sh <circuit.cdl> <page> [steps]
set -euo pipefail
cd "$(dirname "$0")/../.."
OBJ=mac/build/bench-obj; mkdir -p "$OBJ"
FLAGS=(-std=c++17 -O2 -DCL_NO_WX -D_PRODUCTION_ -Wno-deprecated-declarations -Wno-inconsistent-missing-override
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
clang++ "${FLAGS[@]}" -c mac/Tools/bench_drag.cpp -o "$OBJ/bench_drag.o"
clang++ "${OBJS[@]}" "$OBJ/bench_drag.o" -framework CoreGraphics -framework CoreText -framework ImageIO \
	-framework CoreServices -framework CoreFoundation -framework OpenGL -o mac/build/bench_drag
mac/build/bench_drag res/cl_gatedefs.xml "$@"
