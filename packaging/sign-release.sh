#!/usr/bin/env bash
# Sign a versioned release's files, so the one-line installers can check them.
#
#   packaging/sign-release.sh v0.4              sign, then show what to upload (uploads nothing)
#   packaging/sign-release.sh v0.4 --upload     sign and upload SHA256SUMS + SHA256SUMS.sig to it
#   packaging/sign-release.sh v0.4 --check-only every check and the build number, then stop:
#                                               no key, no signing, nothing written or uploaded
#       --build N       the manifest's build number (below); default: found from the test builds
#       --repo O/R      default leviholliday/CedarLogic-Releases
#       --test-key      sign with a throwaway key instead of the Keychain's (to try it; never uploads)
#       --out DIR       where SHA256SUMS(.sig) are written (default $TMPDIR/cedarlogic-signed-<tag>)
#       --sign-preview PATH   scripts/sign-preview.sh (default: this checkout's, else the copy on
#                       the private/windows/native branch)
#
# linux/install.sh and packaging/windows/install.ps1 check a
# release's SHA256SUMS against its .sig with the update key (the one the apps'
# updaters trust) when the release has them, and its "tag" line must be the
# release's own tag. This writes that manifest for every file on the release,
# using scripts/sign-preview.sh's own --dry-run signing (the same key, read
# from the Keychain, the same format and checks), and verifies it.
#
# The build number. The Linux app's Normal testers update from these releases
# (linux/App/Updater.h), and take one only when its build is newer than their
# own CL_BUILD_TIME: the committer time of the commit the app was built from.
# So the build here must be that time for the files on the release, not the
# time this runs (or a Beta tester on a newer test build, switching to
# Normal, would be taken back to an older one). Without --build, it's found:
# each file's SHA-256 is looked up in the signed SHA256SUMS of
# linux-native-testing and, if it has one, windows-native-testing (both
# checked against the update key, each for its own tag), and the "build"
# line of every list that has one of the files is taken. They must all
# agree; if they don't, or no file is in either list (the test build has
# moved on since the files were copied), it refuses and says why: then pass
# --build N, the commit time (git log -1 --format=%ct <commit>) of the build
# the Linux files came from.
#
# A manifest for "tag v0.4" can't be replayed to the apps as a test build:
# their updaters take manifests only for the release they're looking at.
#
# Sign LAST, after every file is on the release. Once a release has a
# SHA256SUMS, the installers refuse any file it doesn't name, so a file
# uploaded afterwards (a late Linux .deb, a re-uploaded DMG) needs this run
# again.
#
# For tests (never with --upload): CEDARLOGIC_SIGN_TEST_RELEASE=DIR uses the
# files in DIR as the release (no GitHub), CEDARLOGIC_SIGN_TEST_PREVIEWS=DIR
# the test builds' lists from DIR/<tag>/SHA256SUMS(.sig), signed by
# CEDARLOGIC_SIGN_TEST_PUBHEX instead of the update key.
set -euo pipefail

REPO=leviholliday/CedarLogic-Releases
PREVIEW_REPO=leviholliday/CedarLogic
PREVIEWS=(linux-native-testing windows-native-testing)
TAG='' UPLOAD=0 TESTKEY=0 SIGNER='' OUT='' BUILD='' CHECKONLY=0
ROOT=$(cd "$(dirname "$0")/.." && pwd)
die() { echo "sign-release: $*" >&2; exit 1; }
say() { echo "sign-release: $*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--repo) REPO=$2; shift 2 ;;
		--upload) UPLOAD=1; shift ;;
		--test-key) TESTKEY=1; shift ;;
		--check-only) CHECKONLY=1; shift ;;
		--build) BUILD=$2; shift 2 ;;
		--sign-preview) SIGNER=$2; shift 2 ;;
		--out) OUT=$2; shift 2 ;;
		-h|--help) sed -n '2,48p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $1" ;;
		*) [ -z "$TAG" ] || die "two tags given"; TAG=$1; shift ;;
	esac
done
[ -n "$TAG" ] || die "which release? (e.g. v0.4)"
[ "$TESTKEY" = 0 ] || [ "$UPLOAD" = 0 ] || die "--test-key never uploads"
[ "$CHECKONLY" = 0 ] || [ "$UPLOAD" = 0 ] || die "--check-only never uploads"
[ -z "$BUILD" ] || [[ "$BUILD" =~ ^[1-9][0-9]{0,14}$ ]] || die "--build needs a positive number (a commit time in Unix seconds), not $BUILD"
TEST_RELEASE=${CEDARLOGIC_SIGN_TEST_RELEASE:-} TEST_PREVIEWS=${CEDARLOGIC_SIGN_TEST_PREVIEWS:-}
if [ -n "$TEST_RELEASE$TEST_PREVIEWS" ]; then
	[ "$UPLOAD" = 0 ] || die "the test folders never upload"
	[ "$TESTKEY" = 1 ] || [ "$CHECKONLY" = 1 ] || die "the test folders need --test-key or --check-only"
	[ -z "$TEST_RELEASE" ] || [ -d "$TEST_RELEASE" ] || die "no folder $TEST_RELEASE"
fi
[ -n "$TEST_RELEASE" ] && [ -n "$TEST_PREVIEWS" ] || command -v gh >/dev/null || die "needs the GitHub CLI (gh)"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-sign-release.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

if [ -z "$SIGNER" ]; then
	if [ -f "$ROOT/scripts/sign-preview.sh" ]; then SIGNER=$ROOT/scripts/sign-preview.sh
	else
		git -C "$ROOT" show private/windows/native:scripts/sign-preview.sh > "$WORK/sign-preview.sh" 2>/dev/null \
			|| die "no scripts/sign-preview.sh here and none on private/windows/native: pass --sign-preview PATH"
		SIGNER=$WORK/sign-preview.sh
		say "using scripts/sign-preview.sh from private/windows/native"
	fi
fi
# The update key's public half, and the manifests' first line: sign-preview.sh's.
PUBHEX=$(sed -n 's/^PUBHEX=\([0-9a-f]\{64\}\)$/\1/p' "$SIGNER" | head -1)
[ -n "$PUBHEX" ] || die "no PUBHEX in $SIGNER"
[ -z "${CEDARLOGIC_SIGN_TEST_PUBHEX:-}" ] || { [ -n "$TEST_PREVIEWS" ] || die "CEDARLOGIC_SIGN_TEST_PUBHEX goes with CEDARLOGIC_SIGN_TEST_PREVIEWS"; PUBHEX=$CEDARLOGIC_SIGN_TEST_PUBHEX; }
HEADER="CedarLogic preview manifest 1"

# OpenSSL 3 (macOS's own is LibreSSL, which can't check Ed25519), for the test builds' lists.
OSSL=''
for c in "${OPENSSL:-}" openssl /opt/homebrew/opt/openssl@3/bin/openssl /usr/local/opt/openssl@3/bin/openssl \
         /opt/homebrew/opt/openssl/bin/openssl /usr/local/opt/openssl/bin/openssl; do
	[ -n "$c" ] || continue
	command -v "$c" >/dev/null 2>&1 || [ -x "$c" ] || continue
	if "$c" version 2>/dev/null | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.'; then OSSL=$c; break; fi
done
[ -n "$OSSL" ] || die "needs OpenSSL 3 or later (brew install openssl@3, or set OPENSSL=/path/to/openssl)"
sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }

# ---- The release's files --------------------------------------------------------------
mkdir -p "$WORK/files" "$WORK/out"
if [ -n "$TEST_RELEASE" ]; then
	say "TEST: the release is the folder $TEST_RELEASE"
	COMMIT=$(printf '%040d' 0 | tr 0 b)
	for f in "$TEST_RELEASE"/*; do
		[ -f "$f" ] || continue
		name=${f##*/}
		case "$name" in SHA256SUMS|SHA256SUMS.sig) continue ;; esac
		cp "$f" "$WORK/files/$name"
		printf '%s %s \n' "$name" "$(wc -c < "$f" | tr -d ' ')" >> "$WORK/assets"
	done
else
	say "checking $TAG on $REPO"
	gh release view "$TAG" -R "$REPO" --json tagName >/dev/null || die "no release $TAG in $REPO"
	COMMIT=$(gh api "repos/$REPO/commits/$TAG" --jq .sha) || die "couldn't read the tag's commit"
	gh api "repos/$REPO/releases/tags/$TAG" --jq '.assets[] | select(.name != "SHA256SUMS" and .name != "SHA256SUMS.sig") | [.name, (.size|tostring), (.digest // "")] | join(" ")' > "$WORK/assets"
fi
[ -s "$WORK/assets" ] || die "the release has no files"
: > "$WORK/hashes"
while read -r name size digest; do
	[[ "$name" =~ ^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$ ]] || die "a file name the installers would refuse: $name"
	[ -n "$TEST_RELEASE" ] || gh release download "$TAG" -R "$REPO" -D "$WORK/files" --pattern "$name" --clobber < /dev/null
	f="$WORK/files/$name"
	[ "$(wc -c < "$f" | tr -d ' ')" = "$size" ] || die "$name didn't download whole"
	h=$(sha256_of "$f")
	[ -z "$digest" ] || [ "$digest" = "sha256:$h" ] || die "$name: sha256 $h, but GitHub's digest says $digest"
	printf '%s %s %s\n' "$name" "$size" "$h" >> "$WORK/hashes"
	say "  $name  $size  $h"
done < "$WORK/assets"

# ---- The build number ---------------------------------------------------------------
# A test build's signed list, checked: the update key's signature, its own
# tag. 0: in $WORK/previews/<tag>/SHA256SUMS; 1: that release has no list.
fetch_preview() {   # tag
	local t=$1 d="$WORK/previews/$1"
	mkdir -p "$d"
	if [ -n "$TEST_PREVIEWS" ]; then
		[ -f "$TEST_PREVIEWS/$t/SHA256SUMS" ] && [ -f "$TEST_PREVIEWS/$t/SHA256SUMS.sig" ] || return 1
		cp "$TEST_PREVIEWS/$t/SHA256SUMS" "$TEST_PREVIEWS/$t/SHA256SUMS.sig" "$d/"
	else
		gh release download "$t" -R "$PREVIEW_REPO" -D "$d" --pattern SHA256SUMS --pattern SHA256SUMS.sig --clobber < /dev/null 2>/dev/null || return 1
		[ -f "$d/SHA256SUMS" ] && [ -f "$d/SHA256SUMS.sig" ] || return 1
	fi
	printf '302a300506032b6570032100%s' "$PUBHEX" | xxd -r -p | "$OSSL" pkey -pubin -inform DER -out "$WORK/pub.pem" 2>/dev/null \
		|| die "couldn't make a public key from $PUBHEX"
	if ! { [ "$(wc -c < "$d/SHA256SUMS.sig" | tr -d ' ')" = 64 ] &&
		"$OSSL" pkeyutl -verify -pubin -inkey "$WORK/pub.pem" -rawin -in "$d/SHA256SUMS" -sigfile "$d/SHA256SUMS.sig" >/dev/null 2>&1; }; then
		die "$t's SHA256SUMS doesn't verify with the update key: not taking its build number"
	fi
	if [ "$(sed -n 1p "$d/SHA256SUMS")" != "$HEADER" ] || [ "$(sed -n 2p "$d/SHA256SUMS")" != "tag $t" ]; then
		die "$t's signed list isn't a list for $t"
	fi
	sed -n 4p "$d/SHA256SUMS" | grep -Eqx 'build [1-9][0-9]{0,14}' || die "$t's signed list has no build line"
	return 0
}

if [ -n "$BUILD" ]; then
	say "build $BUILD (given)"
else
	FOUND='' FOUND_FROM='' SEEN=''
	: > "$WORK/matched"
	for t in "${PREVIEWS[@]}"; do
		if ! fetch_preview "$t"; then say "  $t: no signed list (skipped)"; continue; fi
		SEEN="$SEEN $t"
		b=$(sed -n 's/^build //p' "$WORK/previews/$t/SHA256SUMS")
		files=''
		while read -r name size h; do
			if grep -Eq "^file [^ ]+ $size $h\$" "$WORK/previews/$t/SHA256SUMS"; then
				files="$files $name"
				echo "$name" >> "$WORK/matched"
			fi
		done < "$WORK/hashes"
		if [ -z "$files" ]; then say "  $t (build $b): none of $TAG's files"; continue; fi
		say "  $t (build $b, $(date -r "$b" '+%Y-%m-%d %H:%M' 2>/dev/null || date -d "@$b" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "?")):$files"
		if [ -z "$FOUND" ]; then FOUND=$b FOUND_FROM="$t ($files )"
		elif [ "$FOUND" != "$b" ]; then
			die "the test builds disagree: $FOUND_FROM has build $FOUND, $t ($files ) has build $b. They were built from different commits; pass --build N for the one the Linux files came from"
		fi
	done
	[ -n "$SEEN" ] || die "no test build has a signed list to take the build from: pass --build N (git log -1 --format=%ct <the commit the Linux files were built from>)"
	[ -n "$FOUND" ] || die "none of $TAG's files is in the signed list of$SEEN (the test build has moved on since they were copied?): pass --build N (git log -1 --format=%ct <the commit the Linux files were built from>)"
	# Linux and Windows files that no test build lists: say so (their build isn't known).
	while read -r name _ _; do
		case "$name" in *.deb|*.AppImage|*.exe|*.msi|*.zip)
			grep -qx "$name" "$WORK/matched" || say "  note: $name isn't in a test build's list; the build number is the others'" ;;
		esac
	done < "$WORK/hashes"
	BUILD=$FOUND
	say "build $BUILD (from$SEEN)"
fi

if [ "$CHECKONLY" = 1 ]; then
	say "check only: $TAG ($(wc -l < "$WORK/hashes" | tr -d ' ') files, commit ${COMMIT:0:12}, build $BUILD) would be signed. Nothing was signed or written."
	echo "$BUILD"
	exit 0
fi

ARGS=(--dry-run "$WORK/files" --tag "$TAG" --commit "$COMMIT" --build "$BUILD" --out "$WORK/out")
[ "$TESTKEY" = 0 ] || ARGS+=(--test-key)
bash "$SIGNER" "${ARGS[@]}" > "$WORK/key" || die "signing failed"
head -2 "$WORK/out/SHA256SUMS" | tail -1 | grep -qx "tag $TAG" || die "the manifest's tag isn't $TAG"
sed -n 4p "$WORK/out/SHA256SUMS" | grep -qx "build $BUILD" || die "the manifest's build isn't $BUILD"

OUT=${OUT:-${TMPDIR:-/tmp}/cedarlogic-signed-$TAG}
mkdir -p "$OUT"
cp "$WORK/out/SHA256SUMS" "$WORK/out/SHA256SUMS.sig" "$OUT/"
say "signed with $([ "$TESTKEY" = 1 ] && echo "a throwaway test key" || echo "the Keychain key") ($(cat "$WORK/key")), verified: $OUT/SHA256SUMS(.sig)"
if [ "$UPLOAD" = 1 ]; then
	gh release upload "$TAG" -R "$REPO" "$OUT/SHA256SUMS" "$OUT/SHA256SUMS.sig" --clobber
	mkdir -p "$WORK/back"
	gh release download "$TAG" -R "$REPO" -D "$WORK/back" --pattern 'SHA256SUMS*'
	if ! cmp -s "$WORK/back/SHA256SUMS" "$OUT/SHA256SUMS" || ! cmp -s "$WORK/back/SHA256SUMS.sig" "$OUT/SHA256SUMS.sig"; then
		die "what's on the release now isn't what was uploaded: look at it"
	fi
	say "uploaded to $TAG. The one-line installers check against it from now on."
elif [ "$TESTKEY" = 0 ]; then
	say "not uploaded. To upload: packaging/sign-release.sh $TAG${BUILD:+ --build $BUILD} --upload  (or: gh release upload $TAG -R $REPO $OUT/SHA256SUMS $OUT/SHA256SUMS.sig --clobber)"
fi
