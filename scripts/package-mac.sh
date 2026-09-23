#!/usr/bin/env bash
# Build CedarLogic and wrap it in a drag-to-Applications DMG.
#
#   scripts/package-mac.sh
#
# Output: build/CedarLogic-<version>-mac.dmg
#
# The app is ad-hoc signed, not notarized (that needs a paid Apple Developer
# account). On another Mac, the first launch needs right-click > Open.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --build build --target CedarLogic -j4

APP=build/CedarLogic.app
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP/Contents/Info.plist")
DMG="build/CedarLogic-${VERSION}-mac.dmg"

# Sign inside-out so the embedded Sparkle framework doesn't break the seal.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT
cp -a "$APP" "$STAGING/"
# A Finder alias rather than a plain symlink: recent macOS draws a symlink to
# /Applications inside a disk image as a blank square, while an alias gets the
# real folder icon. Fall back to the symlink if Finder can't be scripted.
if ! osascript -e "tell application \"Finder\" to make alias file to POSIX file \"/Applications\" at POSIX file \"$STAGING\"" >/dev/null 2>&1; then
  ln -s /Applications "$STAGING/Applications"
fi
hdiutil create -volname "CedarLogic $VERSION" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null

echo "Made $DMG"
shasum -a 256 "$DMG"
