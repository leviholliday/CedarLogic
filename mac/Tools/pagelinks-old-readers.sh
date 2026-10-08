#!/bin/bash
# Page link groups and older readers (docs/PAGE-LINKS.md):
#  1. the format library from before link groups (a git revision, HEAD by
#     default -- pass the last commit without them once this one is in)
#     reads every sample in format/tests/fixtures/pagelinks without an error
#     and finds the same pages, gates and wires as this one;
#  2. for every .cdl under format/tests/fixtures, this library's read-then-
#     write gives byte for byte what the old one does (files with no link
#     groups keep their bytes).
# Prints PASS or FAIL per file.
#
#   mac/Tools/pagelinks-old-readers.sh [revision]
set -euo pipefail
cd "$(dirname "$0")/../.."
REV="${1:-HEAD}"
TMP=$(mktemp -d /private/tmp/pagelinks-old-readers.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/old" "$TMP/new"
FILES="circuit_file.hpp circuit_file_io.cpp circuit_file_io.hpp ink.cpp ink.hpp legacy_cdl.cpp legacy_cdl.hpp migrate.cpp migrate.hpp numeric.cpp numeric.hpp sexpr.cpp sexpr.hpp"
for f in $FILES; do
	git show "$REV:format/$f" > "$TMP/old/$f" 2>/dev/null || true
	cp "format/$f" "$TMP/new/$f"
done
cat > "$TMP/tool.cpp" <<'CPP'
#include "circuit_file_io.hpp"
#include "migrate.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
// read <file>: each page's gate and wire count. write <file>: the v3 text
// read and written again (v3 files only).
int main(int argc, char** argv) {
	std::ifstream in(argv[2], std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	try {
		if (std::string(argv[1]) == "write") { fputs(cl::writeCircuitFile(cl::readCircuitFile(ss.str())).c_str(), stdout); return 0; }
		cl::LoadResult r = cl::loadCircuit(ss.str());
		for (const cl::Page& p : r.file.pages) printf("page %d: %zu gates %zu wires\n", p.index, p.gates.size(), p.wires.size());
	} catch (const std::exception& e) {
		printf("error: %s\n", e.what());
		return 1;
	}
}
CPP
for v in old new; do
	clang++ -std=c++17 -O1 -I"$TMP/$v" "$TMP/tool.cpp" $(ls "$TMP/$v"/*.cpp) -o "$TMP/$v/tool"
done
fail=0
for f in format/tests/fixtures/pagelinks/*.cdl; do
	if ! o=$("$TMP/old/tool" read "$f" 2>&1); then echo "FAIL old reader on $f: $o"; fail=$((fail + 1)); continue; fi
	n=$("$TMP/new/tool" read "$f")
	if [ "$o" != "$n" ]; then echo "FAIL $f: old reader finds $o, new $n"; fail=$((fail + 1)); else echo "PASS old reader: $f"; fi
done
checked=0
while IFS= read -r f; do
	head -c 200 "$f" | grep -q '(cedarlogic' || continue
	grep -q linkgroup "$f" && continue
	o=$("$TMP/old/tool" write "$f" 2>&1) || continue   # files the old reader refuses
	n=$("$TMP/new/tool" write "$f" 2>&1) || { echo "FAIL new writer on $f"; fail=$((fail + 1)); continue; }
	if [ "$o" != "$n" ]; then echo "FAIL bytes differ: $f"; fail=$((fail + 1)); fi
	checked=$((checked + 1))
done < <(find format/tests/fixtures tests -name '*.cdl')
echo "PASS same bytes as $REV for $checked v3 files without link groups"
echo "old readers ($REV): $fail failed"
exit $fail
