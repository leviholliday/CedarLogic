#!/bin/bash
# "Nothing broke" check for the wx app: apply this branch's shared-code
# changes (src/, include/) to the main checkout, build it, run the tests,
# render a circuit and compare it to a reference, then put the main checkout
# back exactly as it was and rebuild it.
#   check-wx-app.sh <main checkout> <circuit.cdl> <page> <reference.png>
set -uo pipefail
cd "$(dirname "$0")/../.."
MAIN="$1"; CDL="$2"; PAGE="$3"; REF="$4"
PATCH=$(mktemp)
git add -N $(git ls-files --others --exclude-standard src include) 2>/dev/null
git diff "$(git -C "$MAIN" rev-parse HEAD)" -- src include > "$PATCH"
[ -z "$(git -C "$MAIN" status --porcelain -- src include)" ] || { echo "main checkout has changes; stopping"; exit 1; }
NEW=$(grep '^+++ b/' "$PATCH" | sed 's#^+++ b/##')
git -C "$MAIN" apply --whitespace=nowarn "$PATCH" || { echo "patch didn't apply"; exit 1; }
ok=1
( cd "$MAIN" && cmake -B build . >/dev/null 2>&1 && cmake --build build -j4 2>&1 | grep -E "error:" ) && ok=0
( cd "$MAIN/build" && ctest 2>&1 | grep -E "tests passed|tests failed" )
OUT=$(mktemp -d)/check.png
CEDARLOGIC_RENDER_PAGE=$PAGE "$MAIN/build/CedarLogic.app/Contents/MacOS/CedarLogic" --render "$CDL" "$OUT" 1400 1000
if cmp -s "$OUT" "$REF"; then echo "render: identical to reference"; else echo "render: DIFFERS ($OUT)"; ok=0; fi
# restore
git -C "$MAIN" checkout -- src include
for f in $NEW; do git -C "$MAIN" ls-files --error-unmatch "$f" >/dev/null 2>&1 || rm -f "$MAIN/$f"; done
( cd "$MAIN" && cmake -B build . >/dev/null 2>&1 && cmake --build build --target CedarLogic -j4 2>&1 | grep -E "error:|Built target CedarLogic$" )
git -C "$MAIN" status --short -- src include | head -5
[ $ok = 1 ] && echo "wx app OK" || echo "wx app CHECK FAILED"
