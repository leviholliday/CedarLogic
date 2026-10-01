#!/bin/bash
# Build and run the formula checks against mac/build/libCedarCore.a (run
# mac/build.sh first). An optional folder gets a .cdl of each built circuit.
set -euo pipefail
cd "$(dirname "$0")/../.."
swiftc -O -target "$(uname -m)-apple-macos14" -import-objc-header mac/CedarCore/include/CedarCore.h \
	mac/Tools/formula-check/main.swift mac/App/BooleanAlgebra.swift mac/App/FormulaCircuit.swift \
	mac/build/libCedarCore.a -lc++ -framework CoreText -framework OpenGL -framework CoreGraphics \
	-o mac/build/formula-check
mac/build/formula-check res/cl_gatedefs.xml "$@"
