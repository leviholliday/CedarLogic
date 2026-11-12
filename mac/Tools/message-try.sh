#!/bin/bash
# See a message from CedarLogic (App/AppMessages.swift) now, in a test copy of the app: its own
# bundle id and home folder, so your settings, Your Circuits and the messages you've seen are
# never touched. The copy reads docs/message.json (or the file given) with every "from" date
# moved to today, and shows the first message about 4 seconds after it opens. Quit it when done.
#
#   mac/build.sh && mac/Tools/message-try.sh [message.json]
set -euo pipefail
cd "$(dirname "$0")/../.."
SRC=mac/build/CedarLogic.app
[ -d "$SRC" ] || { echo "Build first: mac/build.sh"; exit 1; }
FILE=${1:-docs/message.json}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-message-try.XXXXXX")
APP="$WORK/CedarLogicMessageTry.app"
ditto "$SRC" "$APP"
P="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set CFBundleIdentifier com.leviholliday.cedarlogic.native.messagetry" "$P"
/usr/libexec/PlistBuddy -c "Delete SentryDSN" "$P" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Delete CFBundleURLTypes" "$P" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Delete CFBundleDocumentTypes" "$P" 2>/dev/null || true
mac/sign-app.sh "$APP" >/dev/null
sed -E "s/\"from\": *\"[0-9-]+\"/\"from\": \"$(date +%F)\"/" "$FILE" >"$WORK/message.json"
mkdir -p "$WORK/home"
echo "Opening a test copy (it shows the message in about 4 seconds; quit it when you're done)."
CFFIXED_USER_HOME="$WORK/home" CL_MESSAGE_URL="file://$WORK/message.json" \
	CL_UPDATE_FEED=https://updates.cedarlogic-offline.invalid/appcast.xml \
	"$APP/Contents/MacOS/CedarLogic" >/dev/null 2>&1 || true
rm -rf "$WORK"
