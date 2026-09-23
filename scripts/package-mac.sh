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
# A Finder alias rather than a plain symlink, with the Applications folder icon
# stamped on it as a custom icon. Recent macOS draws both a symlink and a bare
# alias to /Applications inside a disk image as a blank square; a custom icon
# lives in the file itself, so Finder has nothing to look up.
APPS_ICON=/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/ApplicationsFolderIcon.icns
if osascript -e "tell application \"Finder\" to make alias file to POSIX file \"/Applications\" at POSIX file \"$STAGING\"" >/dev/null 2>&1; then
  osascript -l JavaScript -e "
    ObjC.import('AppKit');
    const img = $.NSImage.alloc.initWithContentsOfFile('$APPS_ICON');
    $.NSWorkspace.sharedWorkspace.setIconForFileOptions(img, '$STAGING/Applications', 0);
  " >/dev/null
else
  ln -s /Applications "$STAGING/Applications"
fi
hdiutil create -volname "CedarLogic $VERSION" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null

echo "Made $DMG"
shasum -a 256 "$DMG"
