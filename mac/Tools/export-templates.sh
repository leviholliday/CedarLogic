#!/bin/bash
# Writes the Mac app's built-in templates (File > New from Template: the lab
# page, the counter, the flip-flops and latches, the registers, the
# combinational circuits, the counters and the Classic Mistakes) as files in
# res/templates, for the apps that don't build them in Swift (Linux reads
# them; see linux/App/Collections.cpp), named as --render-ui names them: template-<id>.cdl, and a flip-flop's
# or register's Clock choices template-<id>-running.cdl and -pulse.cdl; index.tsv
# lists id, group, name, detail and choices in the picker's order.
#
# It compiles mac/App/Templates.swift (without its SwiftUI picker) and
# TemplatesExamples.swift with export-templates/Stubs.swift (no student name;
# Step Clock on K, the default) into a small command-line tool on the engine,
# so the files are exactly what the Mac app builds. Run it again after a
# template changes on the Mac:
#
#   mac/Tools/export-templates.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=mac/build/export-templates
OBJ=$OUT/obj
# CLEAN=1 after changing an engine header (objects are only rebuilt when their own source changed).
[ "${CLEAN:-0}" = 1 ] && rm -rf "$OUT"
mkdir -p "$OBJ"
# The engine, as mac/build.sh builds it (its CORE list), several files at a time.
SRC=$(sed -n '/^CORE=(/,/^)/p' mac/build.sh | grep -v '^CORE=(\|^)\|^\s*#')
SRC=$(eval echo $SRC)
export OBJ
compile() {
	o="$OBJ/$(echo "$1" | tr / _).o"
	[ -f "$o" ] && [ "$o" -nt "$1" ] && return 0
	clang++ -std=c++17 -O1 -arch "$(uname -m)" -mmacosx-version-min=14.0 -DCL_NO_WX -D_PRODUCTION_ \
		-Wno-deprecated-declarations -Wno-inconsistent-missing-override \
		-Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include \
		-c "$1" -o "$o"
}
export -f compile
echo "Engine..."
echo $SRC | tr ' ' '\n' | xargs -P "$(sysctl -n hw.ncpu)" -I{} bash -c 'compile {}'
OBJS=()
for f in $SRC; do OBJS+=("$OBJ/$(echo "$f" | tr / _).o"); done
rm -f "$OUT/libCedarCore.a"
ar rcs "$OUT/libCedarCore.a" "${OBJS[@]}"

echo "Tool..."
n=$(grep -n '^struct TemplatePicker' mac/App/Templates.swift | cut -d: -f1)
head -n $((n - 1)) mac/App/Templates.swift > "$OUT/Templates.swift"
swiftc -O -import-objc-header mac/CedarCore/include/CedarCore.h \
	mac/Tools/export-templates/Stubs.swift "$OUT/Templates.swift" mac/App/TemplatesExamples.swift mac/Tools/export-templates/main.swift \
	"$OUT/libCedarCore.a" -lc++ -framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices \
	-framework CoreFoundation -framework OpenGL -lz -o "$OUT/export-templates"

rm -f res/templates/*.cdl res/templates/index.tsv
"$OUT/export-templates" res/cl_gatedefs.xml res/templates
