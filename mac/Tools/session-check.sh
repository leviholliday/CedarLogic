#!/bin/bash
# A long working session in a circuit window, headless (App/SessionCheck.swift):
#
#   mac/build.sh && mac/Tools/session-check.sh [actions] [seeds...]
#
# Runs a copy of mac/build/CedarLogic.app (its own bundle id and home folder,
# so your settings and Your Circuits are never touched; no updater, no crash
# reports) with `--session-check <actions> <seed>` for each seed (default
# 2000 actions, seed 1). The window is never ordered in: nothing shows on
# screen and nothing is clicked. A run passes when every check passes and
# SwiftUI printed none of its "Publishing changes from within view updates"
# / "Modifying state during view update" warnings.
#
# START=<file.cdl> starts each session from that circuit (as importing it
# would put it in Your Circuits); START=template:<id>[:running|:pulse] from
# a built-in template (ids in App/Templates.swift, e.g. builtin-counter,
# builtin-ff-jk, builtin-reg-shift). CHECK_TAG=<word> gives the copy a
# bundle id of its own, so two runs with different tags can go at once.
set -uo pipefail
START=${START:-}
[ -z "$START" ] || [[ "$START" == template:* ]] || START=$(cd "$(dirname "$START")" && pwd)/$(basename "$START")
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
ACTIONS=${1:-2000}
shift || true
SEEDS=("$@")
[ ${#SEEDS[@]} -gt 0 ] || SEEDS=(1)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-session-check.XXXXXX")
APP="$WORK/CedarLogicSessionCheck.app"
ID=com.leviholliday.cedarlogic.native.sessioncheck${CHECK_TAG:+.$CHECK_TAG}
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier $ID" "$P"
/usr/libexec/PlistBuddy -c "Delete SUPublicEDKey" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleDocumentTypes" "$P" 2>/dev/null
mac/sign-app.sh "$APP"
fail=0
for seed in "${SEEDS[@]}"; do
	home="$WORK/home-$seed"
	mkdir -p "$home"
	defaults delete $ID >/dev/null 2>&1
	log="$WORK/session-$seed.log"
	OS_ACTIVITY_DT_MODE=1 CFFIXED_USER_HOME="$home" CL_CLASSROOM=0 \
		"$APP/Contents/MacOS/CedarLogic" --session-check "$ACTIONS" "$seed" ${START:+"$START"} >"$log" 2>&1
	code=$?
	warnings=$(grep -c -E "Publishing changes from within view updates|Modifying state during view update" "$log")
	grep -E "^(PASS|FAIL|session|memory|after|INFO|TRACE)" "$log" | sed "s/^/[seed $seed] /"
	if [ "$code" = 0 ] && [ "$warnings" = 0 ]; then
		echo "PASS session, seed $seed"
	else
		echo "FAIL session, seed $seed: exit $code, $warnings SwiftUI warning(s) (log: $log)"
		fail=1
	fi
done
defaults delete $ID >/dev/null 2>&1
# Any Keychain items the copy made (its own service: its bundle id), gone too.
while security delete-generic-password -s "$ID" >/dev/null 2>&1; do :; done
[ -n "${CHECK_TAG:-}" ] && rm -f "$HOME/Library/Preferences/$ID.plist"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$APP" 2>/dev/null
[ $fail = 0 ] && rm -rf "$WORK"
exit $fail
