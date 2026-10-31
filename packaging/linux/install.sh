#!/bin/sh
# Install (or update) CedarLogic on Linux, from the newest release:
#
#   curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
#
# Debian, Ubuntu, Zorin, Mint, Raspberry Pi OS (anything with apt): the .deb
# for this computer (amd64, or arm64 for a Pi 4/5 on 64-bit Raspberry Pi OS),
# installed with apt, which fetches what it needs and asks for your password.
# Fedora and the rest: the AppImage, in ~/Applications, started once so it
# adds itself to the applications menu. Either way it keeps itself up to date.
#
# Plain POSIX sh (dash, busybox, bash): "| sh" runs it with whatever sh is.
#
# The file is installed only when it checks out:
#   - the release's signed list (SHA256SUMS + SHA256SUMS.sig, signed with
#     CedarLogic's update key, the public half below; the format is
#     update-signing/UpdateSignature.h's) is for this release and names this
#     file with its size and SHA-256; or, for a release with no signed list,
#   - its SHA-256 is the one GitHub's API records for the file.
# A signed list that doesn't verify stops everything. The .deb is checked
# again as root on a copy only root can change, and apt installs that copy.
#
# Settings (environment variables):
#   CEDARLOGIC_FORMAT=deb|appimage   instead of choosing by whether apt is there
#   CEDARLOGIC_VERSION=0.4           that release (v0.4) instead of the newest
#   CEDARLOGIC_TAG=T                 that release tag, pre-releases too
#   CEDARLOGIC_REPO=O/R              instead of leviholliday/CedarLogic-Releases
#                                    (the rolling test build:
#                                    CEDARLOGIC_REPO=leviholliday/CedarLogic CEDARLOGIC_TAG=linux-native-testing)
#   CEDARLOGIC_DRY_RUN=1             find, download and check; install nothing
#   CEDARLOGIC_NO_LAUNCH=1           don't start CedarLogic afterwards
#   CEDARLOGIC_NO_API=1              skip GitHub's API, as when it's rate-limited (for testing)
#   CEDARLOGIC_TEST_RELEASE=URL + CEDARLOGIC_TEST_KEY=PEM   CI's test: a fake
#                                    release (a folder served over HTTP) signed
#                                    with a test key; both or neither
set -eu

REPO=${CEDARLOGIC_REPO:-leviholliday/CedarLogic-Releases}
HEADER="CedarLogic preview manifest 1"

say() { printf '%s\n' "$*" >&2; }
die() { printf 'CedarLogic was not installed: %s\n' "$*" >&2; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cedarlogic-install.XXXXXX")
ROOTDIR='' SUDO=''
cleanup() {
	rm -rf "$TMP"
	if [ -n "$ROOTDIR" ]; then $SUDO rm -rf "$ROOTDIR"; fi
}
trap cleanup EXIT
trap 'exit 1' INT TERM HUP

[ "$(uname -s)" = Linux ] || [ -n "${CEDARLOGIC_DRY_RUN:-}" ] || die "this is the Linux installer. On a Mac or Windows, see https://cedarlogic.netlify.app"
command -v curl >/dev/null 2>&1 || die "it needs curl (sudo apt install curl, or sudo dnf install curl)."

# ---- How: .deb (apt) or AppImage, and for which CPU ---------------------------------
FORMAT=${CEDARLOGIC_FORMAT:-}
if [ -z "$FORMAT" ]; then
	if command -v dpkg >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then FORMAT=deb; else FORMAT=appimage; fi
fi
case "$FORMAT" in
	deb)
		# The system's own architecture, not the kernel's: the 32-bit
		# Raspberry Pi OS runs a 64-bit kernel on a Pi 4 or 5.
		SYSTEM=${CEDARLOGIC_ARCH:-$(dpkg --print-architecture 2>/dev/null || echo unknown)}
		case "$SYSTEM" in
			amd64|arm64) ARCH=$SYSTEM ;;
			armhf) die "CedarLogic needs the 64-bit Raspberry Pi OS, and this one is 32-bit (armhf). Raspberry Pi Imager can put the 64-bit one on the card." ;;
			*) die "CedarLogic needs a 64-bit PC or a 64-bit Raspberry Pi OS (this is $SYSTEM)." ;;
		esac
		PATTERN="cedarlogic(_[0-9][0-9A-Za-z.+~-]*)?_${ARCH}\\.deb"
		;;
	appimage)
		SYSTEM=${CEDARLOGIC_ARCH:-$(uname -m)}
		case "$SYSTEM" in
			x86_64|amd64) ARCH=x86_64 ;;
			aarch64|arm64) ARCH=aarch64 ;;
			*) die "CedarLogic needs a 64-bit PC (x86_64) or ARM computer (aarch64); this is $SYSTEM." ;;
		esac
		PATTERN="CedarLogic(-Native)?-[0-9][0-9A-Za-z.+_-]*-${ARCH}\\.AppImage"
		;;
	*) die "CEDARLOGIC_FORMAT must be deb or appimage, not $FORMAT" ;;
esac

# ---- Tools ----------------------------------------------------------------------------
sha256_of() {
	if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
	else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
size_of() { wc -c < "$1" | tr -d ' '; }
OPENSSL=${OPENSSL:-openssl}
have_openssl3() {
	command -v "$OPENSSL" >/dev/null 2>&1 && "$OPENSSL" version 2>/dev/null | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.'
}

# The update key's public half (Ed25519): the apps', scripts/sign-preview.sh's
# and linux/install.sh's (CI checks they agree).
cat > "$TMP/key.pem" <<'EOT'
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAQYTauigwwdtBa+eGmn2HN4gXDqHAAQSry/0BjX/jz0A=
-----END PUBLIC KEY-----
EOT

# ---- Which release --------------------------------------------------------------------
# BASE: where the release's files download from; TAG: its tag; NAME: our file,
# DIGEST: GitHub's sha256 for it (empty when unknown).
BASE='' TAG='' NAME='' DIGEST=''
if [ -n "${CEDARLOGIC_TEST_RELEASE:-}${CEDARLOGIC_TEST_KEY:-}" ]; then
	[ -n "${CEDARLOGIC_TEST_RELEASE:-}" ] && [ -f "${CEDARLOGIC_TEST_KEY:-}" ] || die "test mode needs both CEDARLOGIC_TEST_RELEASE and CEDARLOGIC_TEST_KEY"
	say "TEST MODE: $CEDARLOGIC_TEST_RELEASE, signed with $CEDARLOGIC_TEST_KEY"
	BASE=$CEDARLOGIC_TEST_RELEASE
	TAG=${CEDARLOGIC_TAG:-linux-native-testing}
	cp "$CEDARLOGIC_TEST_KEY" "$TMP/key.pem"
else
	say "Looking for the newest CedarLogic for Linux ($FORMAT, $ARCH)..."
	if [ -n "${CEDARLOGIC_TAG:-}" ]; then API="https://api.github.com/repos/$REPO/releases/tags/$CEDARLOGIC_TAG"
	else API="https://api.github.com/repos/$REPO/releases?per_page=30"; fi
	if [ -z "${CEDARLOGIC_NO_API:-}" ] && curl -fsSL -H "Accept: application/vnd.github+json" -o "$TMP/releases.json" "$API" 2>"$TMP/api.err"; then
		# One line per file: tag draft prerelease digest url. GitHub's JSON keeps
		# these keys in this order (a release's tag and flags, then each asset's
		# digest before its URL); a quote inside a string is \", so text in a
		# release's notes can't pass for a key. Whatever the order, a digest that
		# isn't the file's only ever refuses the file.
		grep -oE '"(tag_name|draft|prerelease|digest|browser_download_url)": *("[^"]*"|true|false|null)' "$TMP/releases.json" \
			| sed -E 's/^"([a-z_]+)": *"?([^"]*)"?$/\1 \2/' \
			| awk '
				$1 == "tag_name" { tag = $2; draft = "?"; pre = "?"; digest = "" }
				$1 == "draft" { draft = $2 }
				$1 == "prerelease" { pre = $2 }
				$1 == "digest" { digest = ($2 == "null") ? "" : $2 }
				$1 == "browser_download_url" { print tag, draft, pre, (digest == "" ? "-" : digest), $2; digest = "" }
			' > "$TMP/assets"
		while read -r t draft pre digest url; do
			[ "$draft" = false ] || continue
			[ "$pre" = false ] || [ -n "${CEDARLOGIC_TAG:-}" ] || continue
			if [ -n "${CEDARLOGIC_VERSION:-}" ] && [ "$t" != "v${CEDARLOGIC_VERSION#v}" ] && [ "$t" != "$CEDARLOGIC_VERSION" ]; then continue; fi
			n=${url##*/}
			printf '%s\n' "$n" | grep -Eqx "$PATTERN" || continue
			case "$url" in "https://github.com/$REPO/releases/download/$t/$n") ;; *) continue ;; esac
			TAG=$t NAME=$n BASE=${url%/*}
			[ "$digest" = - ] || DIGEST=$digest
			break
		done < "$TMP/assets"
		if [ -z "$TAG" ]; then
			if [ -n "${CEDARLOGIC_TAG:-}${CEDARLOGIC_VERSION:-}" ]; then die "that release has no $FORMAT for $ARCH."; fi
			die "there's no full release with a $FORMAT for $ARCH yet. See https://cedarlogic.netlify.app/download/linux/ for other ways to get it."
		fi
	else
		# The API refused (it allows 60 requests an hour per network: a
		# classroom can run out). The newest release's tag is still in
		# github.com's redirect, and its signed list names the files.
		say "GitHub's API didn't answer ($(head -c 200 "$TMP/api.err" | tr '\n' ' ')); trying the release page instead."
		[ -z "${CEDARLOGIC_VERSION:-}" ] || CEDARLOGIC_TAG=${CEDARLOGIC_TAG:-v${CEDARLOGIC_VERSION#v}}
		if [ -n "${CEDARLOGIC_TAG:-}" ]; then TAG=$CEDARLOGIC_TAG
		else
			LATEST=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest") || die "couldn't reach GitHub. Check the internet connection and try again."
			TAG=${LATEST##*/tag/}
			[ "$TAG" != "$LATEST" ] || die "couldn't tell which release is the newest. Try again in an hour, or download it from https://cedarlogic.netlify.app/download/linux/"
		fi
		BASE="https://github.com/$REPO/releases/download/$TAG"
	fi
fi
printf '%s\n' "$TAG" | grep -Eqx '[A-Za-z0-9._+-]+' || die "a strange release tag: $TAG"

# ---- The signed list, when there is one -----------------------------------------------
SIGNED=0 SIZE='' SHA=''
if curl -fsSL -o "$TMP/SHA256SUMS" "$BASE/SHA256SUMS" 2>/dev/null && curl -fsSL -o "$TMP/SHA256SUMS.sig" "$BASE/SHA256SUMS.sig" 2>/dev/null; then
	have_openssl3 || die "checking CedarLogic's signature needs OpenSSL 3 (Ubuntu 22.04, Debian 12, Fedora 36 or newer have it; this is: $("$OPENSSL" version 2>/dev/null || echo 'no openssl'))."
	if ! "$OPENSSL" pkeyutl -verify -pubin -inkey "$TMP/key.pem" -rawin -in "$TMP/SHA256SUMS" -sigfile "$TMP/SHA256SUMS.sig" >/dev/null 2>&1; then
		die "the release's signed list doesn't check out against CedarLogic's key. Nothing was changed."
	fi
	{ read -r header; read -r tagline; } < "$TMP/SHA256SUMS"
	if [ "$header" != "$HEADER" ] || [ "$tagline" != "tag $TAG" ]; then die "the signed list isn't for release $TAG."; fi
	if [ -z "$NAME" ]; then
		# No API: the list says which file is ours.
		NAME=$(grep -E '^file ' "$TMP/SHA256SUMS" | cut -d' ' -f2 | grep -Ex "$PATTERN" || true)
		[ -n "$NAME" ] || die "release $TAG has no $FORMAT for $ARCH."
		[ "$(printf '%s\n' "$NAME" | wc -l | tr -d ' ')" = 1 ] || die "release $TAG has more than one $FORMAT for $ARCH: $(echo "$NAME" | tr '\n' ' ')"
	fi
	LINE=$(grep -E "^file $(printf '%s' "$NAME" | sed 's/[.+]/\\&/g') [1-9][0-9]* [0-9a-f]{64}\$" "$TMP/SHA256SUMS" || true)
	[ -n "$LINE" ] && [ "$(printf '%s\n' "$LINE" | wc -l | tr -d ' ')" = 1 ] || die "the signed list doesn't name $NAME."
	SIZE=$(printf '%s\n' "$LINE" | cut -d' ' -f3)
	SHA=$(printf '%s\n' "$LINE" | cut -d' ' -f4)
	SIGNED=1
elif [ -n "${CEDARLOGIC_TEST_RELEASE:-}" ]; then
	die "the test release has no signed list."
fi
if [ "$SIGNED" = 0 ]; then
	case "$DIGEST" in
		sha256:*) SHA=${DIGEST#sha256:} ;;
		*) die "release $TAG has no signed list, and GitHub gave no checksum for $NAME, so it can't be checked. Try again in an hour, or download it from https://cedarlogic.netlify.app/download/linux/" ;;
	esac
	printf '%s\n' "$SHA" | grep -Eqx '[0-9a-f]{64}' || die "GitHub's checksum for $NAME looks wrong: $DIGEST"
fi

# ---- Download and check ---------------------------------------------------------------
say "Downloading $NAME (release $TAG)..."
FILE=$TMP/$NAME
curl -fL --progress-bar -o "$FILE" "$BASE/$NAME" || die "the download failed. Try again."
GOT=$(sha256_of "$FILE")
if [ -n "$SIZE" ] && [ "$(size_of "$FILE")" != "$SIZE" ]; then die "the download is $(size_of "$FILE") bytes, not the $SIZE the signed list says. Nothing was changed."; fi
[ "$GOT" = "$SHA" ] || die "the download's SHA-256 ($GOT) isn't the one $( [ "$SIGNED" = 1 ] && echo 'the signed list' || echo 'GitHub') gives ($SHA). Nothing was changed."
say "Checked against $( [ "$SIGNED" = 1 ] && echo "the release's signed list" || echo "GitHub's SHA-256 for it (this release has no signed list)")."
if [ -n "${CEDARLOGIC_DRY_RUN:-}" ]; then say "Dry run: $NAME is downloaded and checked; not installed."; exit 0; fi

graphical() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && [ -z "${CEDARLOGIC_NO_LAUNCH:-}" ]; }
start_detached() {   # program [env assignment]
	if command -v setsid >/dev/null 2>&1; then setsid "$@" >/dev/null 2>&1 < /dev/null &
	else nohup "$@" >/dev/null 2>&1 < /dev/null & fi
}

# ---- Install: .deb ----------------------------------------------------------------------
if [ "$FORMAT" = deb ]; then
	if [ "$(id -u)" = 0 ]; then SUDO=''
	elif command -v sudo >/dev/null 2>&1; then SUDO=sudo
	else die "installing the .deb needs root, and there's no sudo here. Run this as root, or with CEDARLOGIC_FORMAT=appimage."; fi
	say "Installing with apt${SUDO:+ (this asks for your password)}..."
	# Checked again as root, on a copy in a folder only root can write: the
	# one apt installs can't be swapped after the check.
	ROOTDIR=$($SUDO mktemp -d /var/tmp/cedarlogic-install-XXXXXX)
	$SUDO cp "$FILE" "$ROOTDIR/$NAME"
	if ! echo "$SHA  $ROOTDIR/$NAME" | $SUDO sha256sum -c --status -; then die "the file changed after it was checked."; fi
	# apt reads it as its own user: the folder opened to read, still only root's to change.
	$SUDO chmod 755 "$ROOTDIR"
	$SUDO chmod 644 "$ROOTDIR/$NAME"
	$SUDO apt-get install -y "$ROOTDIR/$NAME" || die "apt couldn't install it (see above). If it says it's a downgrade, a newer CedarLogic is already installed."
	say "Done. CedarLogic is in your applications menu (under Education), and it keeps itself up to date."
	if graphical && command -v cedarlogic >/dev/null 2>&1; then start_detached cedarlogic; fi
	exit 0
fi

# ---- Install: AppImage --------------------------------------------------------------------
SUDO=''
DEST_DIR=${CEDARLOGIC_APPIMAGE_DIR:-$HOME/Applications}
DEST=$DEST_DIR/CedarLogic.AppImage
mkdir -p "$DEST_DIR"
# Next to where it goes, then renamed over it: a running copy keeps its old file.
cp "$FILE" "$DEST_DIR/.CedarLogic.AppImage.new"
chmod 755 "$DEST_DIR/.CedarLogic.AppImage.new"
mv -f "$DEST_DIR/.CedarLogic.AppImage.new" "$DEST"
say "Installed $DEST. It updates itself there."

# AppImages need FUSE 2 to start quickly; without it they can still unpack
# themselves each time (APPIMAGE_EXTRACT_AND_RUN), which CedarLogic notices
# and writes into its menu entry.
FUSE=0
if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -q 'libfuse\.so\.2'; then FUSE=1
else
	for f in /usr/lib/libfuse.so.2* /usr/lib64/libfuse.so.2* /lib/*/libfuse.so.2* /usr/lib/*/libfuse.so.2*; do
		[ -e "$f" ] && { FUSE=1; break; }
	done
fi
if [ "$FUSE" = 0 ]; then
	say "Note: FUSE 2 isn't installed, so CedarLogic starts a little slower (it unpacks itself each time)."
	say "  To fix that: Fedora: sudo dnf install fuse fuse-libs   openSUSE: sudo zypper install libfuse2   Arch: sudo pacman -S fuse2"
fi
if graphical; then
	if [ "$FUSE" = 1 ]; then start_detached "$DEST"; else start_detached env APPIMAGE_EXTRACT_AND_RUN=1 "$DEST"; fi
	say "Done. CedarLogic is starting, and from now on it's in your applications menu."
else
	if [ "$FUSE" = 1 ]; then say "Done. Start it once ($DEST) and it adds itself to your applications menu."
	else say "Done. Start it once (APPIMAGE_EXTRACT_AND_RUN=1 $DEST) and it adds itself to your applications menu."; fi
fi
