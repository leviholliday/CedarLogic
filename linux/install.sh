#!/usr/bin/env bash
# Install (or update) CedarLogic on Debian, Ubuntu or Raspberry Pi OS:
#
#   curl -fsSL https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing/install.sh | bash
#
# Downloads the .deb for this computer from the test build and installs it
# with apt (which asks for your password). Afterwards CedarLogic is in the
# applications menu, and it keeps itself up to date.
set -euo pipefail
case "$(uname -m)" in
	x86_64) ARCH=amd64 ;;
	aarch64|arm64) ARCH=arm64 ;;
	*) echo "CedarLogic needs a 64-bit PC or a 64-bit Raspberry Pi OS (this is $(uname -m))." >&2; exit 1 ;;
esac
RELEASE=https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing
FILE=$(mktemp -d)/cedarlogic_$ARCH.deb
echo "Downloading CedarLogic for $ARCH..."
curl -fL --progress-bar -o "$FILE" "$RELEASE/cedarlogic_$ARCH.deb"
echo "Installing (this asks for your password)..."
sudo apt-get install -y "$FILE"
echo "Done. CedarLogic is in your applications menu (under Education)."
