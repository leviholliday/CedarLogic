#!/bin/bash
# Build the headless check tools against mac/build/libCedarCore.a (run
# mac/build.sh first).
set -euo pipefail
cd "$(dirname "$0")/../.."
for t in render_png sim_check edit_check save_check tt_check cl_check part_check make_fixtures tidy_render layout_check clock_check make_check_cases check_seq mistakes_check predict_check lock_check ink_check register_check pagelinks_check straighten_lab; do
	clang++ -std=c++17 -O1 -DCL_NO_WX -Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include -Wno-deprecated-declarations -Wno-inconsistent-missing-override mac/Tools/$t.cpp mac/build/libCedarCore.a \
		-framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices \
		-framework CoreFoundation -framework OpenGL -o mac/build/$t
done
echo "Built the check tools in mac/build"
