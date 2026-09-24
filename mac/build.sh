#!/bin/bash
# Build the native Mac app (no Xcode needed -- the Command Line Tools will do).
#
#   mac/build.sh            build mac/build/CedarLogic Native.app
#   CLEAN=1 mac/build.sh    rebuild everything (after changing a header)
#   OPEN=1 mac/build.sh     then launch it
#
# The engine (C++ shared with the wx app, built with CL_NO_WX) becomes
# libCedarCore.a; the Swift app links it through CedarCore.h.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=mac/build
OBJ=$OUT/obj
APP="$OUT/CedarLogic Native.app"
ARCH=$(uname -m)
MIN=14.0
[ "${CLEAN:-0}" = 1 ] && rm -rf "$OUT"
mkdir -p "$OBJ"

CORE=(
	# the model, drawing, routing and loading/saving, shared with the wx app
	src/gui/guiGate.cpp src/gui/guiWire.cpp src/gui/wireSegment.cpp
	src/gui/klsCollisionChecker.cpp src/gui/GateLibrary.cpp src/gui/LibraryParse.cpp
	src/gui/XMLParser.cpp src/gui/GUICircuitModel.cpp src/gui/RenderMode.cpp
	src/gui/PaletteDrag.cpp src/gui/Settings.cpp src/gui/gl_defs.cpp src/gui/CircuitParse.cpp
	src/gui/klsClipboard.cpp src/gui/CircuitEdits.cpp
	src/gui/route/TrunkRouter.cpp src/gui/route/GridRouter.cpp src/gui/route/Layout.cpp
	# the editing commands (undo/redo), minus the two that manage wx tabs
	$(ls src/gui/command/*.cpp | grep -v -e cmdAddTab -e cmdDeleteTab)
	# the logic engine and the file format library
	logic/src/*.cpp
	format/circuit_file_io.cpp format/legacy_cdl.cpp format/migrate.cpp format/numeric.cpp format/sexpr.cpp
	# the native side
	mac/CedarCore/*.cpp
)
CXXFLAGS=(-std=c++17 -O2 -arch "$ARCH" -mmacosx-version-min=$MIN -DCL_NO_WX
	-Wno-deprecated-declarations -Wno-inconsistent-missing-override -D_PRODUCTION_ -Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include)

echo "Engine..."
OBJS=()
for f in "${CORE[@]}"; do
	o="$OBJ/$(echo "$f" | tr / _).o"
	d="${o%.o}.d"
	OBJS+=("$o")
	# Recompile when the source or any header it includes changed (the .d
	# file clang writes lists them). Missing that once let two files disagree
	# about a struct's layout.
	stale=0
	if [ ! -f "$o" ] || [ ! -f "$d" ] || [ "$f" -nt "$o" ]; then
		stale=1
	else
		for h in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$d"); do
			if [ "$h" -nt "$o" ]; then stale=1; break; fi
		done
	fi
	if [ $stale = 1 ]; then
		clang++ "${CXXFLAGS[@]}" -MMD -MF "$d" -c "$f" -o "$o"
	fi
done
rm -f "$OUT/libCedarCore.a"
ar rcs "$OUT/libCedarCore.a" "${OBJS[@]}"

echo "App..."
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -O -parse-as-library -target "$ARCH-apple-macos$MIN" \
	-import-objc-header mac/CedarCore/include/CedarCore.h \
	mac/App/*.swift "$OUT/libCedarCore.a" -lc++ \
	-framework CoreText -framework OpenGL \
	-o "$APP/Contents/MacOS/CedarLogic"

cp mac/App/Info.plist "$APP/Contents/Info.plist"
cp res/cl_gatedefs.xml "$APP/Contents/Resources/"
cp res/macos/CedarLogic.icns "$APP/Contents/Resources/"
codesign --force --sign - "$APP" >/dev/null 2>&1
echo "Built $APP"
[ "${OPEN:-0}" = 1 ] && open "$APP"
exit 0
