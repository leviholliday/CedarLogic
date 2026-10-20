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
set -uo pipefail
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
ACTIONS=${1:-2000}
shift || true
SEEDS=("$@")
[ ${#SEEDS[@]} -gt 0 ] || SEEDS=(1)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-session-check.XXXXXX")
APP="$WORK/CedarLogicSessionCheck.app"
ID=com.leviholliday.cedarlogic.native.sessioncheck
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier $ID" "$P"
/usr/libexec/PlistBuddy -c "Delete SUPublicEDKey" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null
/usr/libexec/PlistBuddy -c "Delete CFBundleDocumentTypes" "$P" 2>/dev/null
codesign --force --deep --sign - "$APP" >/dev/null 2>&1
fail=0
for seed in "${SEEDS[@]}"; do
	home="$WORK/home-$seed"
	mkdir -p "$home"
	defaults delete $ID >/dev/null 2>&1
	log="$WORK/session-$seed.log"
	OS_ACTIVITY_DT_MODE=1 CFFIXED_USER_HOME="$home" CL_CLASSROOM=0 \
		"$APP/Contents/MacOS/CedarLogic" --session-check "$ACTIONS" "$seed" >"$log" 2>&1
	code=$?
	warnings=$(grep -c -E "Publishing changes from within view updates|Modifying state during view update" "$log")
	grep -E "^(PASS|FAIL|session|memory|after)" "$log" | sed "s/^/[seed $seed] /"
	if [ "$code" = 0 ] && [ "$warnings" = 0 ]; then
		echo "PASS session, seed $seed"
	else
		echo "FAIL session, seed $seed: exit $code, $warnings SwiftUI warning(s) (log: $log)"
		fail=1
	fi
done
defaults delete $ID >/dev/null 2>&1
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$APP" 2>/dev/null
[ $fail = 0 ] && rm -rf "$WORK"
exit $fail
