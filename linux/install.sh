#!/usr/bin/env bash
# Install (or update) CedarLogic on Debian, Ubuntu or Raspberry Pi OS:
#
#   curl -fsSL https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing/install.sh | bash
#
# Downloads the .deb for this computer from the test build and installs it
# with apt (which asks for your password). Afterwards CedarLogic is in the
# applications menu, and it keeps itself up to date.
set -euo pipefail
if ! command -v dpkg >/dev/null 2>&1; then
	echo "This installs CedarLogic on Debian, Ubuntu or Raspberry Pi OS. Elsewhere, use the AppImage from the release page." >&2
	exit 1
fi
# The system's own architecture, not the kernel's (uname -m): the 32-bit
# Raspberry Pi OS runs a 64-bit kernel on a Pi 4 or 5.
SYSTEM=$(dpkg --print-architecture)
case "$SYSTEM" in
	amd64|arm64) ARCH=$SYSTEM ;;
	armhf) echo "CedarLogic needs the 64-bit Raspberry Pi OS, and this one is 32-bit (armhf). Raspberry Pi Imager can put the 64-bit one on the card." >&2; exit 1 ;;
	*) echo "CedarLogic needs a 64-bit PC or a 64-bit Raspberry Pi OS (this is $SYSTEM)." >&2; exit 1 ;;
esac
RELEASE=https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# apt reads the file as its own user: let it in.
chmod 755 "$TMP"
FILE=$TMP/cedarlogic_$ARCH.deb
echo "Downloading CedarLogic for $ARCH..."
curl -fL --progress-bar -o "$FILE" "$RELEASE/cedarlogic_$ARCH.deb"
chmod 644 "$FILE"
echo "Installing (this asks for your password)..."
sudo apt-get install -y "$FILE"
echo "Done. CedarLogic is in your applications menu (under Education)."
