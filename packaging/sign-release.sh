#!/usr/bin/env bash
# Sign a versioned release's files, so the one-line installers can check them.
#
#   packaging/sign-release.sh v0.4              sign, then show what to upload (uploads nothing)
#   packaging/sign-release.sh v0.4 --upload     sign and upload SHA256SUMS + SHA256SUMS.sig to it
#       --repo O/R      default leviholliday/CedarLogic-Releases
#       --test-key      sign with a throwaway key instead of the Keychain's (to try it; never uploads)
#       --out DIR       where SHA256SUMS(.sig) are written (default $TMPDIR/cedarlogic-signed-<tag>)
#       --sign-preview PATH   scripts/sign-preview.sh (default: this checkout's, else the copy on
#                       the private/windows/native branch)
#       --windows-commit C    the leviholliday/CedarLogic commit the release's Windows files were
#                       built from (default: the one windows-native-testing is on, when the release's
#                       Windows files are byte for byte that release's: see "build" below)
#
# linux/install.sh and packaging/windows/install.ps1 check a
# release's SHA256SUMS against its .sig with the update key (the one the apps'
# updaters trust) when the release has them, and its "tag" line must be the
# release's own tag. This writes that manifest for every file on the release,
# using scripts/sign-preview.sh's own --dry-run signing (the same key, read
# from the Keychain, the same format and checks), and verifies it.
#
# The Windows app's normal testers (Settings > General > Testing group) update
# from the newest full release here with CedarLogic-<v>-Windows-<x64|ARM64>.zip,
# through this manifest: its tag must be that release's, its commit the one the
# tag is on in this repo, and its build newer than the running app's. So:
#
#   build   for a release with Windows files, their build time: the committer
#           time (Unix seconds) of the leviholliday/CedarLogic commit they were
#           built from, the CL_BUILD_TIME baked into them and the "build" of the
#           windows-native-testing manifest that signed them. Not the time of
#           signing: then a beta tester switching to normal would be "updated"
#           to a release older than the test build they have, and signing again
#           (a file added later) would offer the same release again to everyone
#           already on it. By default it's read from windows-native-testing's
#           signed manifest, and only while the release's Windows zips and
#           installers are byte for byte that release's files (renamed as
#           docs/RELEASE-0.4.md step 2 does); after the test build has moved on,
#           pass --windows-commit. Without Windows files, the time of signing, as
#           before (the installers don't compare builds).
#
# The Linux app's updater reads only linux-native-testing, so the build here is
# the Windows files' even when the Linux files are from another commit.
#
# A manifest for "tag v0.4" can't be replayed as a test build: the updaters take
# a manifest only for the tag they're reading (windows-native-testing,
# linux-native-testing, or for Windows normal testers that release's own).
#
# Sign LAST, after every file is on the release. Once a release has a
# SHA256SUMS, the installers refuse any file it doesn't name, so a file
# uploaded afterwards (a late Linux .deb, a re-uploaded DMG) needs this run
# again.
set -euo pipefail

REPO=leviholliday/CedarLogic-Releases
TAG='' UPLOAD=0 TESTKEY=0 SIGNER='' OUT='' WINCOMMIT=''
SOURCE_REPO=leviholliday/CedarLogic PREVIEW_TAG=windows-native-testing
ROOT=$(cd "$(dirname "$0")/.." && pwd)
die() { echo "sign-release: $*" >&2; exit 1; }
say() { echo "sign-release: $*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--repo) REPO=$2; shift 2 ;;
		--upload) UPLOAD=1; shift ;;
		--test-key) TESTKEY=1; shift ;;
		--sign-preview) SIGNER=$2; shift 2 ;;
		--out) OUT=$2; shift 2 ;;
		--windows-commit) WINCOMMIT=$2; shift 2 ;;
		-h|--help) sed -n '2,/^set -e/{/^#/p;}' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $1" ;;
		*) [ -z "$TAG" ] || die "two tags given"; TAG=$1; shift ;;
	esac
done
[ -n "$TAG" ] || die "which release? (e.g. v0.4)"
[ "$TESTKEY" = 0 ] || [ "$UPLOAD" = 0 ] || die "--test-key never uploads"
command -v gh >/dev/null || die "needs the GitHub CLI (gh)"

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

say "checking $TAG on $REPO"
gh release view "$TAG" -R "$REPO" --json tagName >/dev/null || die "no release $TAG in $REPO"
COMMIT=$(gh api "repos/$REPO/commits/$TAG" --jq .sha) || die "couldn't read the tag's commit"
BUILD=$(date +%s)
mkdir -p "$WORK/files" "$WORK/out"
gh api "repos/$REPO/releases/tags/$TAG" --jq '.assets[] | select(.name != "SHA256SUMS" and .name != "SHA256SUMS.sig") | [.name, (.size|tostring), (.digest // "")] | join(" ")' > "$WORK/assets"
[ -s "$WORK/assets" ] || die "the release has no files"
while read -r name size digest; do
	[[ "$name" =~ ^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$ ]] || die "a file name the installers would refuse: $name"
	gh release download "$TAG" -R "$REPO" -D "$WORK/files" --pattern "$name" --clobber < /dev/null
	f="$WORK/files/$name"
	[ "$(wc -c < "$f" | tr -d ' ')" = "$size" ] || die "$name didn't download whole"
	h=$(shasum -a 256 "$f" | cut -d' ' -f1)
	[ -z "$digest" ] || [ "$digest" = "sha256:$h" ] || die "$name: sha256 $h, but GitHub's digest says $digest"
	say "  $name  $size  $h"
done < "$WORK/assets"

# ---- The build: the Windows files' own, when there are any ----------------------
VERSION=${TAG#v}
WINFILES=$(cut -d' ' -f1 "$WORK/assets" | grep -E '^CedarLogic-.*-Windows-' || true)
if [ -n "$WINFILES" ]; then
	# The apps look for exactly CedarLogic-<v>-Windows-<arch>.zip, <v> being the tag without its "v".
	while read -r name; do
		case "$name" in
			"CedarLogic-$VERSION-Windows-x64.zip"|"CedarLogic-$VERSION-Windows-ARM64.zip"|\
			"CedarLogic-$VERSION-Windows-Setup-x64.exe"|"CedarLogic-$VERSION-Windows-Setup-ARM64.exe") ;;
			*) die "$name: the Windows files must be CedarLogic-$VERSION-Windows-{x64,ARM64}.zip and -Windows-Setup-{x64,ARM64}.exe for $TAG" ;;
		esac
	done <<<"$WINFILES"
	grep -qx "CedarLogic-$VERSION-Windows-x64.zip" <<<"$WINFILES" \
		|| say "warning: no CedarLogic-$VERSION-Windows-x64.zip: Intel/AMD PCs' apps won't update to $TAG"
	grep -qx "CedarLogic-$VERSION-Windows-ARM64.zip" <<<"$WINFILES" \
		|| say "warning: no CedarLogic-$VERSION-Windows-ARM64.zip: ARM PCs' apps won't update to $TAG"
	[ "$(gh release view "$TAG" -R "$REPO" --json isPrerelease --jq .isPrerelease)" = false ] \
		|| say "warning: $TAG is a pre-release: the Windows app's normal testers pass over it"
	if [ -z "$WINCOMMIT" ]; then
		# The test build these are, by their bytes: the release's files, renamed.
		say "reading the build of the Windows files from $PREVIEW_TAG"
		mkdir -p "$WORK/preview"
		gh release download "$PREVIEW_TAG" -R "$SOURCE_REPO" -D "$WORK/preview" --pattern SHA256SUMS --clobber < /dev/null \
			|| die "couldn't read $PREVIEW_TAG's SHA256SUMS: pass --windows-commit (the commit the Windows files were built from)"
		PM=$WORK/preview/SHA256SUMS
		[ "$(sed -n 2p "$PM")" = "tag $PREVIEW_TAG" ] || die "$PREVIEW_TAG's SHA256SUMS isn't for $PREVIEW_TAG"
		WINCOMMIT=$(sed -n 's/^commit \([0-9a-f]\{40\}\)$/\1/p' "$PM")
		PBUILD=$(sed -n 's/^build \([0-9]\{1,15\}\)$/\1/p' "$PM")
		[ -n "$WINCOMMIT" ] && [ -n "$PBUILD" ] || die "$PREVIEW_TAG's SHA256SUMS has no commit or build line"
		while read -r name; do
			arch=${name##*-}; arch=${arch%.*}
			case "$name" in *-Setup-*) orig="CedarLogic-Windows-Setup-$arch.exe" ;; *) orig="CedarLogic-Windows-native-$arch.zip" ;; esac
			want=$(awk -v n="$orig" '$1 == "file" && $2 == n { print $4 }' "$PM")
			have=$(shasum -a 256 "$WORK/files/$name" | cut -d' ' -f1)
			[ -n "$want" ] && [ "$want" = "$have" ] || die "$name isn't $PREVIEW_TAG's signed $orig (it has moved on since?): pass --windows-commit (the commit $name was built from)"
		done <<<"$WINFILES"
		say "the Windows files are $PREVIEW_TAG's build ${WINCOMMIT:0:12}, as signed for testers"
	fi
	[[ "$WINCOMMIT" =~ ^[0-9a-f]{40}$ ]] || WINCOMMIT=$(gh api "repos/$SOURCE_REPO/commits/$WINCOMMIT" --jq .sha) \
		|| die "no commit $WINCOMMIT in $SOURCE_REPO"
	BUILD=$(gh api "repos/$SOURCE_REPO/commits/$WINCOMMIT" --jq '.commit.committer.date | fromdateiso8601') \
		|| die "couldn't read the time of $WINCOMMIT"
	[ -z "${PBUILD:-}" ] || [ "$PBUILD" = "$BUILD" ] || die "$PREVIEW_TAG's manifest says build $PBUILD, the commit's time is $BUILD"
	say "build $BUILD: the Windows files' (${WINCOMMIT:0:12}, $(date -r "$BUILD" 2>/dev/null || date -d "@$BUILD"))"
fi

ARGS=(--dry-run "$WORK/files" --tag "$TAG" --commit "$COMMIT" --build "$BUILD" --out "$WORK/out")
[ "$TESTKEY" = 0 ] || ARGS+=(--test-key)
bash "$SIGNER" "${ARGS[@]}" > "$WORK/key" || die "signing failed"
head -2 "$WORK/out/SHA256SUMS" | tail -1 | grep -qx "tag $TAG" || die "the manifest's tag isn't $TAG"

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
	say "not uploaded. To upload: packaging/sign-release.sh $TAG --upload  (or: gh release upload $TAG -R $REPO $OUT/SHA256SUMS $OUT/SHA256SUMS.sig --clobber)"
fi
