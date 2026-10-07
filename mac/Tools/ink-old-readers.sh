#!/bin/bash
# The "old readers" check of the website's DRAWING-NOTES.md 6 (item 6): the
# format library as it was before drawings (a git revision, cls/integrate by
# default) reads every drawing sample in format/tests/fixtures/drawing/cdl
# without an error and finds the same gates and wires as in the base circuit
# it was made from; the unescaped <version> sample is the one the apps
# refuse. Prints PASS or FAIL per sample.
#
#   mac/Tools/ink-old-readers.sh [revision]
set -euo pipefail
cd "$(dirname "$0")/../.."
REV="${1:-cls/integrate}"
TMP=$(mktemp -d /private/tmp/ink-old-readers.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/format"
for f in circuit_file.hpp circuit_file_io.cpp circuit_file_io.hpp legacy_cdl.cpp legacy_cdl.hpp migrate.cpp migrate.hpp numeric.cpp numeric.hpp sexpr.cpp sexpr.hpp; do
	git show "$REV:format/$f" > "$TMP/format/$f"
done
cat > "$TMP/read.cpp" <<'CPP'
#include "migrate.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
// Prints each page's gate and wire count, or the error.
int main(int argc, char** argv) {
	std::ifstream in(argv[1], std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	try {
		cl::LoadResult r = cl::loadCircuit(ss.str());
		for (const cl::Page& p : r.file.pages) printf("page %d: %zu gates %zu wires\n", p.index, p.gates.size(), p.wires.size());
		printf("notices %zu\n", r.notices.size());
	} catch (const std::exception& e) {
		printf("error: %s\n", e.what());
		return 1;
	}
}
CPP
clang++ -std=c++17 -O1 -I"$TMP/format" "$TMP/read.cpp" "$TMP"/format/*.cpp -o "$TMP/read"
D=format/tests/fixtures/drawing/cdl
fail=0
for f in "$D"/*.cdl; do
	name=$(basename "$f" .cdl)
	case "$name" in base-*) continue;; esac
	if ! out=$("$TMP/read" "$f" 2>&1); then echo "FAIL $name: $out"; fail=$((fail + 1)); continue; fi
	# The base it was made from (the hand-written samples have none).
	base=""
	grep -q '"AA_TOGGLE"' "$f" && { grep -q '_XOR' "$f" && base=$D/base-half-adder.cdl || base=$D/base-and.cdl; }
	if [ -n "$base" ]; then
		want=$("$TMP/read" "$base" | grep '^page')
		got=$(echo "$out" | grep '^page' | grep -v ': 0 gates 0 wires' || true)
		if [ "$got" != "$want" ]; then echo "FAIL $name: $got (want $want)"; fail=$((fail + 1)); continue; fi
	fi
	if grep -q '<version>' "$f"; then echo "PASS $name: reads, and holds the raw <version> every app refuses (the hazard)"
	else echo "PASS $name: reads, $(echo "$out" | grep -c '^page') page(s), the base's gates and wires"; fi
done
echo "old readers ($REV): $fail failed"
exit $fail
