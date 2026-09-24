#!/bin/bash
# Build the headless check tools against mac/build/libCedarCore.a (run
# mac/build.sh first).
set -euo pipefail
cd "$(dirname "$0")/../.."
for t in render_png sim_check; do
	clang++ -std=c++17 -O1 -Imac/CedarCore/include mac/Tools/$t.cpp mac/build/libCedarCore.a \
		-framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreServices \
		-framework CoreFoundation -framework OpenGL -o mac/build/$t
done
echo "Built the check tools in mac/build"
