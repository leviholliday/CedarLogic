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
	o="$OBJ/$(echo "$f" | tr / _).o"; OBJS+=("$o")
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then clang++ "${FLAGS[@]}" -c "$f" -o "$o"; fi
done
clang++ -fsanitize=address "${OBJS[@]}" -framework CoreGraphics -framework CoreText -framework ImageIO \
	-framework CoreServices -framework CoreFoundation -framework OpenGL -o mac/build/edit_check_asan
mac/build/edit_check_asan res/cl_gatedefs.xml "$1" "$2"
