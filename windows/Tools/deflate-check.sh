#!/bin/bash
# Checks windows/App/Deflate.cpp (the compression Share Link's links use) on
# any machine with a C++17 compiler and python3: built with the sanitizers,
# then run against python's zlib in both directions, on damaged streams, and
# on the repo's own circuits. No Windows needed.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
${CXX:-clang++} -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	windows/Tools/deflate_check.cpp windows/App/Deflate.cpp -o "$OUT/deflate_check"
python3 windows/Tools/deflate-check.py "$OUT/deflate_check" res/samples/practice.cdl format/tests/fixtures/*.cdl
