#!/bin/bash
# Package the native Mac app as a disk image: the app and an Applications
# shortcut to drag it onto.
#
#   mac/package.sh    -> mac/build/CedarLogic-<version>-Mac.dmg
#
# Builds first (mac/build.sh). Signed ad hoc, like the wx app's DMG, so the
# first launch on another Mac needs right-click > Open.
set -euo pipefail
cd "$(dirname "$0")/.."

mac/build.sh
APP="mac/build/CedarLogic.app"
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
DMG="mac/build/CedarLogic-${VERSION}-Mac.dmg"
codesign --verify --strict "$APP"

rm -f "$DMG"
# The styled window (the wx app's background, big icons, the arrow between
# them) needs dmgbuild (pip install dmgbuild; DMGBUILD=<path> to point at it).
DMGBUILD="${DMGBUILD:-$(command -v dmgbuild || true)}"
if [ -n "$DMGBUILD" ]; then
	"$DMGBUILD" -s mac/dmg-settings.py -D app="$APP" "CedarLogic" "$DMG" >/dev/null
else
	echo "note: dmgbuild not found; making a plain disk image"
	STAGING=$(mktemp -d)
	trap 'rm -rf "$STAGING"' EXIT
	cp -R "$APP" "$STAGING/"
	ln -s /Applications "$STAGING/Applications"
	hdiutil create -volname "CedarLogic" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null
fi
echo "Packaged $DMG"
