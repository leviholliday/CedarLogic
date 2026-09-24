#!/bin/bash
# Package the native Mac app as a disk image: the app and an Applications
# shortcut to drag it onto.
#
#   mac/package.sh    -> mac/build/CedarLogic-Native-<version>.dmg
#
# Builds first (mac/build.sh). Signed ad hoc, like the wx app's DMG, so the
# first launch on another Mac needs right-click > Open.
set -euo pipefail
cd "$(dirname "$0")/.."

mac/build.sh
APP="mac/build/CedarLogic Native.app"
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
DMG="mac/build/CedarLogic-Native-${VERSION}.dmg"
codesign --verify --strict "$APP"

STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"
rm -f "$DMG"
hdiutil create -volname "CedarLogic Native" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null
echo "Packaged $DMG"
