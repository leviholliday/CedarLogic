#!/bin/bash
# What a launch decides and does, from many saved states, headless
# (App/LaunchSelfTest.swift):
#
#   mac/build.sh && mac/Tools/launch-selftest.sh [rounds]
#
# A copy of mac/build/CedarLogic.app with its own bundle id and home folder
# (your settings and Your Circuits are never touched; no updater, no crash
# reports; Classroom pointed at an address nothing answers) is set up in
# each saved state and then "launched" with --launch-selftest, `rounds`
# times over (default 3, so 30 launches): the welcome / What's New choice,
# which circuit reopens and that it opens, Your Circuits, versions, Sync and
# Classroom starting offline, and (the "damaged" state) damaged circuits.
# No window opens. A launch
# passes when it exits 0 within 60 s.
set -uo pipefail
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
ROUNDS=${1:-3}
STATES=(first normal updated nolibrary missinglast corruptlast emptylast unsaved brokenlibrary damaged)
# STATES_ONLY="damaged" mac/Tools/launch-selftest.sh 40: only those states.
[ -n "${STATES_ONLY:-}" ] && read -ra STATES <<<"$STATES_ONLY"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-launch-selftest.XXXXXX")
APP="$WORK/CedarLogicLaunchSelfTest.app"
ID=com.leviholliday.cedarlogic.native.launchselftest
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier $ID" "$P"
/usr/libexec/PlistBuddy -c "Delete SUPublicEDKey" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleDocumentTypes" "$P" 2>/dev/null
codesign --force --deep --sign - "$APP" >/dev/null 2>&1
mkdir -p "$WORK/home"
run() {   # run <log> <args...>: the app, killed after 60 s
	local log=$1; shift
	CFFIXED_USER_HOME="$WORK/home" CL_CLASSROOM=1 CL_CLASSROOM_SERVICE=http://127.0.0.1:9 \
		"$APP/Contents/MacOS/CedarLogic" "$@" >"$log" 2>&1 &
	local pid=$!
	( sleep 60; kill -9 $pid 2>/dev/null ) &
	local killer=$!
	wait $pid; local code=$?
	kill $killer 2>/dev/null; wait $killer 2>/dev/null
	return $code
}
fail=0; n=0
for r in $(seq 1 "$ROUNDS"); do
	for s in "${STATES[@]}"; do
		n=$((n + 1))
		log="$WORK/launch$n-$s.log"
		run "$WORK/setup$n.log" --launch-selftest setup "$s"
		if CL_DAMAGE_ROUND=$r run "$log" --launch-selftest check "$s"; then
			echo "PASS launch $n ($s)"
		else
			echo "FAIL launch $n ($s), exit $? (log: $log)"
			grep -E "^(FAIL|INFO)" "$log" | sed 's/^/    /'
			# A circuit that crashed it is kept beside the log.
			[ -f "$WORK/home/damaged-current.cdl" ] && mv "$WORK/home/damaged-current.cdl" "$WORK/crash$n.cdl" \
				&& echo "    the circuit it crashed on: $WORK/crash$n.cdl"
			fail=1
		fi
		[ "$r" = 1 ] && grep -E "^INFO" "$log" | sed 's/^/    /'
	done
done
defaults delete $ID >/dev/null 2>&1
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$APP" 2>/dev/null
[ $fail = 0 ] && rm -rf "$WORK"
exit $fail
