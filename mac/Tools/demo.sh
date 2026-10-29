#!/bin/bash
# A montage to screen-record (App/DemoMode.swift): the app builds a half
# adder by itself, flips its switches, then shows the 4-bit counter running
# in Simulation View. Your pointer moves with it, so keep your hands off the
# mouse and trackpad while it runs (about 45 seconds).
#
#   mac/build.sh && mac/Tools/demo.sh            # then start recording
#   SPEED=1.5 mac/Tools/demo.sh                  # faster (0.7 = slower)
#   CHECK=1 mac/Tools/demo.sh                    # a check run, off screen
#
# It runs a copy of mac/build/CedarLogic.app with its own bundle id and home
# folder, so your settings and Your Circuits are never touched; the copy and
# its settings are removed when you quit it.
set -uo pipefail
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-demo.XXXXXX")
APP="$WORK/CedarLogic.app"
ID=com.leviholliday.cedarlogic.native.demo
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier $ID" "$P"
/usr/libexec/PlistBuddy -c "Delete SUPublicEDKey" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleDocumentTypes" "$P" 2>/dev/null
mac/sign-app.sh "$APP" >/dev/null
mkdir -p "$WORK/home"
defaults delete $ID >/dev/null 2>&1

if [ -z "${CHECK:-}" ]; then
	echo "Start your screen recording now. The demo begins in 5 seconds."
	echo "Keep your hands off the mouse and trackpad until it says done."
	for i in 5 4 3 2 1; do printf '%s ' "$i"; sleep 1; done; echo
fi
env CFFIXED_USER_HOME="$WORK/home" CL_CLASSROOM=0 CL_NO_SPLASH=1 \
	CL_DEMO_SPEED="${SPEED:-1}" ${CHECK:+CL_DEMO_CHECK=1} ${SHOTS:+CL_DEMO_SHOTS="$SHOTS"} \
	"$APP/Contents/MacOS/CedarLogic" --demo 2>/dev/null | grep -E "^(PASS|FAIL|demo)"
code=${PIPESTATUS[0]}

defaults delete $ID >/dev/null 2>&1
while security delete-generic-password -s "$ID" >/dev/null 2>&1; do :; done
rm -rf "$WORK"
exit "$code"
