#!/bin/bash
# A real launch of the built app, checked for SwiftUI's runtime warnings:
#
#   mac/build.sh && mac/Tools/launch-check.sh [runs]
#
# Launches a copy of mac/build/CedarLogic.app (its own bundle id, so its
# settings are its own; its own home folder, so Your Circuits, Sync and
# Classroom start empty; no updater) `runs` times (default 3), the first as
# a first launch. Each launch plays the launch screen, opens a new circuit
# window and quits by itself after about 4 s (CL_MENU_DUMP); one that's
# still there after 40 s is stuck (the first launch's sound once froze it). A launch passes when the app exits cleanly
# and SwiftUI printed none of "Publishing changes from within view updates"
# or "Modifying state during view update" (undefined behaviour: a change
# made then can be lost, leaving the window out of step). The windows do
# show on screen for those seconds; it moves nothing and clicks nothing.
set -uo pipefail
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
RUNS=${1:-3}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-launch-check.XXXXXX")
APP="$WORK/CedarLogicLaunchCheck.app"
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier com.leviholliday.cedarlogic.native.launchcheck" "$P"
/usr/libexec/PlistBuddy -c "Delete SUPublicEDKey" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null
mac/sign-app.sh "$APP"
mkdir -p "$WORK/home"
# The copy's own settings, from nothing: launch 1 is a first launch (the
# welcome, the first-launch sound), the rest ordinary ones.
defaults delete com.leviholliday.cedarlogic.native.launchcheck >/dev/null 2>&1
fail=0
for i in $(seq 1 "$RUNS"); do
	log="$WORK/run$i.log"
	# OS_ACTIVITY_DT_MODE: the os_log lines (SwiftUI's warnings) on stderr too.
	OS_ACTIVITY_DT_MODE=1 CFFIXED_USER_HOME="$WORK/home" CL_MENU_DUMP="$WORK/menu$i.txt" \
		"$APP/Contents/MacOS/CedarLogic" >"$log" 2>&1 &
	pid=$!
	( sleep 40; kill -9 $pid 2>/dev/null ) &
	killer=$!
	wait $pid; code=$?
	kill $killer 2>/dev/null; wait $killer 2>/dev/null
	warnings=$(grep -c -E "Publishing changes from within view updates|Modifying state during view update" "$log")
	if [ "$code" = 0 ] && [ "$warnings" = 0 ] && [ -s "$WORK/menu$i.txt" ]; then
		echo "PASS launch $i"
	else
		echo "FAIL launch $i: exit $code, $warnings SwiftUI warning(s) (log: $log)"
		fail=1
	fi
done
# The copy unregistered, so Finder never offers it for .cdl files.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$APP" 2>/dev/null
[ $fail = 0 ] && rm -rf "$WORK"
exit $fail
