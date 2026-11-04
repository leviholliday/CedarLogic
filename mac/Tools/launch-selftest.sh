#!/bin/bash
# What a launch decides and does, from many saved states, headless
# (App/LaunchSelfTest.swift):
#
#   mac/build.sh && mac/Tools/launch-selftest.sh [rounds]
#
# A copy of mac/build/CedarLogic.app with its own bundle id and home folder
# (your settings and Your Circuits are never touched; Classroom, the
# updater and crash reports pointed at addresses nothing answers) is set up
# in each saved state and then "launched" with --launch-selftest, `rounds`
# times over (default 3, so 39 launches): the welcome / What's New choice,
# which circuit reopens and that it opens, Your Circuits, versions, Sync and
# Classroom starting offline, the updater and crash reports with no network
# ("offline"), (the "damaged" state) damaged circuits, and the Actions
# panel: on for a first launch and for every earlier install (0.4's one-time
# migration, the old In the toolbar and Hidden modes included: "migrated"),
# a choice made after it kept ("paneloff"), folded as it was left
# ("actions"), and Show Me Around's stops, keys and Escape in a real window
# off screen. No window opens. A launch
# passes when it exits 0 within 60 s.
#
# FOCUS=1 then also launches the copy for real (through `open`, as the Dock
# does) in the first, normal and updated states (FOCUS_STATES to choose),
# `rounds` times each: its windows show on screen for about 8 s each, and
# nothing is clicked. Once the launch screen has gone, and the welcome or
# What's New has been closed, the circuit window must be the key window, its
# canvas must have the keyboard, File > Export as Image… must be enabled and
# its key (⌘E, sent through the app's own event queue) must ask for the
# export (LaunchFocusCheck); then the same after another window has been in front
# for a moment; and Help > Show Me Around is in the menu bar, and the walk
# on the real window steps with → and ← and ends with Escape (keys sent
# through the event queue, nothing clicked). It takes the keyboard for those seconds: run it when you're
# not typing elsewhere (a launch macOS keeps from the front, as with the
# screen locked, is a SKIP). HEADLESS=0 FOCUS=1: only those launches.
set -uo pipefail
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
ROUNDS=${1:-3}
STATES=(first normal updated nolibrary missinglast corruptlast emptylast unsaved brokenlibrary damaged offline trash actions migrated paneloff)
# STATES_ONLY="damaged" mac/Tools/launch-selftest.sh 40: only those states.
[ -n "${STATES_ONLY:-}" ] && read -ra STATES <<<"$STATES_ONLY"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-launch-selftest.XXXXXX")
APP="$WORK/CedarLogicLaunchSelfTest.app"
ID=com.leviholliday.cedarlogic.native.launchselftest
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier $ID" "$P"
# The update key stays (the updater only starts in the offline state, at a
# feed that can't be reached: CL_UPDATE_FEED); crash reports go nowhere real.
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleDocumentTypes" "$P" 2>/dev/null
mac/sign-app.sh "$APP"
mkdir -p "$WORK/home"
run() {   # run <log> <args...>: the app, killed after 60 s
	local log=$1; shift
	CFFIXED_USER_HOME="$WORK/home" CL_CLASSROOM=1 CL_CLASSROOM_SERVICE=http://127.0.0.1:9 \
		CL_UPDATE_FEED=https://updates.cedarlogic-offline.invalid/appcast.xml \
		CL_SENTRY_DSN=https://00000000000000000000000000000000@reports.cedarlogic-offline.invalid/1 \
		"$APP/Contents/MacOS/CedarLogic" "$@" >"$log" 2>&1 &
	local pid=$!
	( sleep 60; kill -9 $pid 2>/dev/null ) &
	local killer=$!
	wait $pid; local code=$?
	kill $killer 2>/dev/null; wait $killer 2>/dev/null
	return $code
}
# HEADLESS=0: only the real launches.
[ "${HEADLESS:-1}" = 1 ] || STATES=()
fail=0; n=0
for r in $(seq 1 "$ROUNDS"); do
	for s in ${STATES[@]+"${STATES[@]}"}; do
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
# The real launches (FOCUS=1): through Launch Services, so the copy is
# brought forward the way a click in the Dock brings it.
if [ "${FOCUS:-0}" = 1 ]; then
	read -ra FSTATES <<<"${FOCUS_STATES:-first normal updated}"
	for r in $(seq 1 "$ROUNDS"); do
		for s in "${FSTATES[@]}"; do
			n=$((n + 1))
			log="$WORK/focus$n-$s.log"
			run "$WORK/setup$n.log" --launch-selftest setup "$s"
			: >"$log"
			open -n -W --stdout "$log" --stderr "$log" --env CFFIXED_USER_HOME="$WORK/home" --env CL_FOCUS_CHECK="$s" \
				--env CL_CLASSROOM=1 --env CL_CLASSROOM_SERVICE=http://127.0.0.1:9 \
				--env CL_UPDATE_FEED=https://updates.cedarlogic-offline.invalid/appcast.xml "$APP" &
			opener=$!
			( sleep 45; pkill -9 -f "$APP/Contents/MacOS/CedarLogic" 2>/dev/null ) &
			killer=$!
			wait $opener
			kill $killer 2>/dev/null; wait $killer 2>/dev/null
			if grep -q "^focus-check $s: all passed" "$log"; then
				echo "PASS focus $n ($s)"
			elif grep -q "^SKIP" "$log"; then
				# Not brought to the front (a locked screen): not a failure,
				# but nothing was checked either.
				grep "^SKIP" "$log" | sed "s/^SKIP /SKIP focus $n: /"
			else
				echo "FAIL focus $n ($s) (log: $log)"
				grep -E "^(FAIL|TRACE)" "$log" | sed 's/^/    /'
				fail=1
			fi
		done
	done
fi
defaults delete $ID >/dev/null 2>&1
# Any Keychain items the copy made (its own service: its bundle id), gone too.
while security delete-generic-password -s "$ID" >/dev/null 2>&1; do :; done
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$APP" 2>/dev/null
[ $fail = 0 ] && rm -rf "$WORK"
exit $fail
