#!/bin/bash
# Build the engine and edit_check with AddressSanitizer and run it, to catch
# memory errors (use after free, overflows) the plain build only crashes on
# sometimes.   asan-check.sh <circuit.cdl> <page>
set -euo pipefail
cd "$(dirname "$0")/../.."
OBJ=mac/build/asan-obj; mkdir -p "$OBJ"
FLAGS=(-std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer -DCL_NO_WX -D_PRODUCTION_
	-Wno-deprecated-declarations -Wno-inconsistent-missing-override
	-Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include)
SRC=$(sed -n '/^CORE=(/,/^)/p' mac/build.sh | grep -v '^CORE=(\|^)\|^\s*#')
SRC=$(eval echo $SRC)
OBJS=()
for f in $SRC mac/Tools/edit_check.cpp; do
	o="$OBJ/$(echo "$f" | tr / _).o"; d="${o%.o}.d"; OBJS+=("$o")
	# Rebuild when the source or a header it includes changed (as build.sh does).
	stale=0
	if [ ! -f "$o" ] || [ ! -f "$d" ] || [ "$f" -nt "$o" ]; then stale=1
	else for h in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$d"); do [ "$h" -nt "$o" ] && { stale=1; break; }; done; fi
	if [ $stale = 1 ]; then clang++ "${FLAGS[@]}" -MMD -MF "$d" -c "$f" -o "$o"; fi
done
clang++ -fsanitize=address "${OBJS[@]}" -framework CoreGraphics -framework CoreText -framework ImageIO \
	-framework CoreServices -framework CoreFoundation -framework OpenGL -o mac/build/edit_check_asan
mac/build/edit_check_asan res/cl_gatedefs.xml "$1" "$2"
