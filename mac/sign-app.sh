#!/bin/bash
# Sign the app ad hoc (there's no Developer ID yet) with the hardened runtime,
# so other programs can't inject code into it or attach to it:
#
#   mac/sign-app.sh <app>
#
# build.sh calls this, and so do the check scripts that re-sign a test copy.
# Inside out and without --deep, as Sparkle's guide asks: each helper gets
# its own signature, and only the app itself gets the entitlements
# (mac/App/CedarLogic.entitlements says why each one is needed).
set -euo pipefail
APP=$1
ENTITLEMENTS="$(cd "$(dirname "$0")" && pwd)/App/CedarLogic.entitlements"

# (Quiet unless it fails: every file says "replacing existing signature".)
sign() {
    local out
    out=$(codesign --force --sign - --options runtime "$@" 2>&1) || { echo "$out" >&2; return 1; }
}

FRAMEWORKS="$APP/Contents/Frameworks"
SPARKLE="$FRAMEWORKS/Sparkle.framework/Versions/B"
if [ -d "$SPARKLE" ]; then
    sign "$SPARKLE/Autoupdate"
    sign "$SPARKLE/Updater.app"
fi
for framework in "$FRAMEWORKS"/*.framework; do
    [ -d "$framework" ] && sign "$framework"
done
sign --entitlements "$ENTITLEMENTS" "$APP"
codesign --verify --strict "$APP"
