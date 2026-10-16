#!/bin/bash
# The cross-check of the two classroom clients against the real server code:
# builds mac/build/classroom-selftest/classroom_interop (the C++ core as a
# command-line device, classroom_interop.cpp) and runs the website's
# scripts/test_classroom_interop.mjs with it, which starts the mock server and
# drives whole classes with a teacher in JavaScript and students in C++, and
# the other way round. Needs the website's checkout (cedarlogic-site) and node,
# and Homebrew's openssl@3 as classroom-selftest.sh does.
#
#   mac/Tools/classroom-interop.sh                       build and run (the site beside this checkout)
#   CEDARLOGIC_SITE=/path/to/cedarlogic-site mac/Tools/classroom-interop.sh
#   BUILD_ONLY=1 mac/Tools/classroom-interop.sh          just build the tool
#   mac/Tools/classroom-interop.sh js-teacher            one direction (js-teacher or cpp-teacher)
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

OUT=${OUT:-mac/build/classroom-selftest}
mkdir -p "$OUT"
CXX=${CXX:-clang++}
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter)
INC=(-Imac/CedarCore -Imac/CedarCore/include)
LIBS=(-lcrypto -lz -lcurl)
if [ "$(uname)" = Darwin ]; then
	O=$(brew --prefix openssl@3 2>/dev/null || true)
	[ -n "$O" ] && [ -d "$O/include" ] || { echo "Needs Homebrew's openssl@3 (brew install openssl@3)"; exit 1; }
	INC+=(-I"$O/include"); LIBS=(-L"$O/lib" "${LIBS[@]}")
else
	LIBS+=(-pthread)
fi
SRC=(mac/CedarCore/Classroom*.cpp mac/CedarCore/SyncProtocol.cpp mac/CedarCore/SyncJson.cpp mac/CedarCore/QrCodeGen.cpp
	mac/Tools/classroom_interop.cpp)
echo "Building $OUT/classroom_interop..."
"$CXX" "${FLAGS[@]}" "${INC[@]}" "${SRC[@]}" "${LIBS[@]}" -o "$OUT/classroom_interop"
[ "${BUILD_ONLY:-0}" = 1 ] && exit 0

SITE=${CEDARLOGIC_SITE:-$ROOT/../cedarlogic-site}
[ -f "$SITE/scripts/test_classroom_interop.mjs" ] || { echo "No $SITE/scripts/test_classroom_interop.mjs: set CEDARLOGIC_SITE to the website's checkout"; exit 1; }
exec node "$SITE/scripts/test_classroom_interop.mjs" --tool "$ROOT/$OUT/classroom_interop" "$@"
