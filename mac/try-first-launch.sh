#!/bin/bash
# Plays CedarLogic's very first launch again (the sound, the slow launch
# screen, the welcome and the tour) with this checkout's build: quits the
# running app, installs mac/build/CedarLogic.app over the copy in
# Applications, forgets that the welcome was seen, and opens it.
#
#     bash mac/try-first-launch.sh
set -e
cd "$(dirname "$0")/.."
ID=com.leviholliday.cedarlogic.native
APP="/Applications/CedarLogic Native.app"   # /Applications/CedarLogic.app is the classic app
[ -d mac/build/CedarLogic.app ] || { echo "Build it first: mac/build.sh"; exit 1; }

osascript -e "tell application id \"$ID\" to quit" 2>/dev/null || true
while pgrep -qf "$APP/Contents/MacOS/"; do sleep 0.2; done

rm -rf "$APP"
ditto mac/build/CedarLogic.app "$APP"
defaults delete $ID cl.firstLaunchPlayed 2>/dev/null || true
defaults write $ID cl.hasSeenWelcome -bool false
open "$APP"
echo "Opened $(defaults read "$APP/Contents/Info" CFBundleShortVersionString) (build $(defaults read "$APP/Contents/Info" CFBundleVersion)); turn your volume up."
