#!/usr/bin/env bash
# Install (or update) CedarLogic on Debian, Ubuntu or Raspberry Pi OS:
#
#   curl -fsSL https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing/install.sh | bash
#
# Downloads the .deb for this computer from the test build and installs it
# with apt (which asks for your password). Afterwards CedarLogic is in the
# applications menu, and it keeps itself up to date.
#
# The .deb is installed only when it's the one the release's signed list
# names: SHA256SUMS, signed with CedarLogic's preview key (the public half is
# below; update-signing/UpdateSignature.h has the format). The check is done
# again as root on a copy only root can change, and apt installs that copy.
set -euo pipefail
if ! command -v dpkg >/dev/null 2>&1; then
	echo "This installs CedarLogic on Debian, Ubuntu or Raspberry Pi OS. Elsewhere, use the AppImage from the release page." >&2
	exit 1
fi
if ! command -v openssl >/dev/null 2>&1; then
	echo "Installing CedarLogic checks its signature with openssl, which isn't here. Install it first (sudo apt install openssl), then run this again." >&2
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
TAG=linux-native-testing
NAME=cedarlogic_$ARCH.deb
TMP=$(mktemp -d)
ROOTDIR=
cleanup() {
	rm -rf "$TMP"
	if [ -n "$ROOTDIR" ]; then sudo rm -rf "$ROOTDIR"; fi
}
trap cleanup EXIT

# The preview key's public half (Ed25519).
cat > "$TMP/key.pem" <<'EOT'
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAQYTauigwwdtBa+eGmn2HN4gXDqHAAQSry/0BjX/jz0A=
-----END PUBLIC KEY-----
EOT
# CI's test of this script: a fake release on localhost, signed with a key
# made for the test. Both or neither.
if [ -n "${CEDARLOGIC_TEST_RELEASE:-}${CEDARLOGIC_TEST_KEY:-}" ]; then
	[ -n "${CEDARLOGIC_TEST_RELEASE:-}" ] && [ -f "${CEDARLOGIC_TEST_KEY:-}" ] || { echo "test mode needs both CEDARLOGIC_TEST_RELEASE and CEDARLOGIC_TEST_KEY" >&2; exit 1; }
	echo "TEST MODE: $CEDARLOGIC_TEST_RELEASE, signed with $CEDARLOGIC_TEST_KEY" >&2
	RELEASE=$CEDARLOGIC_TEST_RELEASE
	cp "$CEDARLOGIC_TEST_KEY" "$TMP/key.pem"
fi

echo "Downloading CedarLogic for $ARCH..."
curl -fsSL -o "$TMP/SHA256SUMS" "$RELEASE/SHA256SUMS" &&
	curl -fsSL -o "$TMP/SHA256SUMS.sig" "$RELEASE/SHA256SUMS.sig" || {
	echo "This test build hasn't been signed yet, so it can't be checked. Try again in a little while." >&2
	exit 1
}
if ! openssl pkeyutl -verify -pubin -inkey "$TMP/key.pem" -rawin -in "$TMP/SHA256SUMS" -sigfile "$TMP/SHA256SUMS.sig" >/dev/null 2>&1; then
	if ! openssl pkeyutl -help 2>&1 | grep -q -- -rawin; then
		echo "This openssl ($(openssl version)) is too old to check the signature (it needs OpenSSL 3). CedarLogic needs Ubuntu 22.04, Debian 12 or newer anyway." >&2
	else
		echo "The test build's signature doesn't check out, so it wasn't installed. Nothing was changed." >&2
	fi
	exit 1
fi
# The verified list: this tag, and this computer's .deb in it.
{ read -r header; read -r tagline; } < "$TMP/SHA256SUMS"
if [ "$header" != "CedarLogic preview manifest 1" ] || [ "$tagline" != "tag $TAG" ]; then
	echo "The signed list isn't for this release, so nothing was installed." >&2
	exit 1
fi
LINE=$(grep -E "^file $NAME [1-9][0-9]* [0-9a-f]{64}\$" "$TMP/SHA256SUMS" || true)
if [ "$(printf '%s\n' "$LINE" | grep -c .)" != 1 ]; then
	echo "The signed list has no $NAME, so nothing was installed." >&2
	exit 1
fi
SIZE=$(printf '%s\n' "$LINE" | cut -d' ' -f3)
SHA=$(printf '%s\n' "$LINE" | cut -d' ' -f4)

FILE=$TMP/$NAME
curl -fL --progress-bar -o "$FILE" "$RELEASE/$NAME"
if [ "$(wc -c < "$FILE" | tr -d ' ')" != "$SIZE" ] || [ "$(sha256sum "$FILE" | cut -d' ' -f1)" != "$SHA" ]; then
	echo "The download isn't the file the signed list names, so it wasn't installed. Nothing was changed." >&2
	exit 1
fi

echo "Installing (this asks for your password)..."
# Checked again as root, on a copy in a folder only root can write: the one
# apt installs can't be swapped after the check.
ROOTDIR=$(sudo mktemp -d /var/tmp/cedarlogic-install-XXXXXX)
sudo cp "$FILE" "$ROOTDIR/$NAME"
if ! echo "$SHA  $ROOTDIR/$NAME" | sudo sha256sum -c --status -; then
	echo "The file changed after it was checked, so it wasn't installed." >&2
	exit 1
fi
# apt reads it as its own user: the folder opened to read, still only root's to change.
sudo chmod 755 "$ROOTDIR"
sudo chmod 644 "$ROOTDIR/$NAME"
sudo apt-get install -y "$ROOTDIR/$NAME"
echo "Done. CedarLogic is in your applications menu (under Education)."
