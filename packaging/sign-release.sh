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
#
# packaging/linux/install.sh and packaging/windows/install.ps1 check a
# release's SHA256SUMS against its .sig with the update key (the one the apps'
# updaters trust) when the release has them, and its "tag" line must be the
# release's own tag. This writes that manifest for every file on the release,
# using scripts/sign-preview.sh's own --dry-run signing (the same key, read
# from the Keychain, the same format and checks), and verifies it.
#
# A manifest for "tag v0.4" can't be replayed to the apps: their updaters only
# take manifests for their own tag (windows-native-testing, linux-native-testing).
#
# Sign LAST, after every file is on the release. Once a release has a
# SHA256SUMS, the installers refuse any file it doesn't name, so a file
# uploaded afterwards (a late Linux .deb, a re-uploaded DMG) needs this run
# again.
set -euo pipefail

REPO=leviholliday/CedarLogic-Releases
TAG='' UPLOAD=0 TESTKEY=0 SIGNER='' OUT=''
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
		-h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
