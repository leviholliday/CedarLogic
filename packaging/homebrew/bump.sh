#!/usr/bin/env bash
# Point the Homebrew cask at a CedarLogic for Mac release.
#
#   packaging/homebrew/bump.sh               the newest full release that has a Mac DMG
#   packaging/homebrew/bump.sh 0.4           that version (release v0.4)
#       --cask FILE      the cask to rewrite (default: Casks/cedarlogic.rb next to this script;
#                        point it at the tap's checkout to bump the tap itself)
#       --repo O/R       the releases repo (default leviholliday/CedarLogic-Releases)
#       --no-mount       don't open the DMG to check the app inside it
#       --dry-run        check everything, print the new lines, change nothing
#
# What it does: finds the release (tag v<version>) and its DMG
# (CedarLogic-<version>-Mac.dmg), downloads it, checks it against the sha256
# GitHub keeps for the file, opens it read-only and checks it holds
# CedarLogic.app with that version and bundle id (and says which CPUs it runs
# on), then writes the version and sha256 into the cask. It never pushes:
# commit and push the tap yourself (packaging/homebrew/README.md).
set -euo pipefail

REPO=leviholliday/CedarLogic-Releases
HERE=$(cd "$(dirname "$0")" && pwd)
CASK=$HERE/Casks/cedarlogic.rb
BUNDLE_ID=com.leviholliday.cedarlogic.native
VERSION='' MOUNT=1 DRY=0

die() { echo "bump: $*" >&2; exit 1; }
say() { echo "bump: $*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--cask) CASK=$2; shift 2 ;;
		--repo) REPO=$2; shift 2 ;;
		--no-mount) MOUNT=0; shift ;;
		--dry-run) DRY=1; shift ;;
		-h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $1" ;;
		*) [ -z "$VERSION" ] || die "two versions given"; VERSION=${1#v}; shift ;;
	esac
done
[ -f "$CASK" ] || die "no cask at $CASK"
command -v curl >/dev/null || die "needs curl"
command -v python3 >/dev/null || die "needs python3"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-bump-brew.XXXXXX")
MNT=
cleanup() {
	if [ -n "$MNT" ]; then hdiutil detach -quiet "$MNT" 2>/dev/null || hdiutil detach -quiet -force "$MNT" 2>/dev/null || true; fi
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

api() {   # path -> JSON on stdout (gh when it's signed in: no rate limit)
	if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then gh api "$1"
	else curl -fsSL -H "Accept: application/vnd.github+json" "https://api.github.com/$1"; fi
}

# The release: "tag<TAB>asset name<TAB>download url<TAB>digest" for the Mac DMG.
api "repos/$REPO/releases?per_page=50" > "$WORK/releases.json" || die "couldn't list the releases of $REPO"
LINE=$(python3 -I - "$WORK/releases.json" "$VERSION" <<'PY'
import json, re, sys
releases = json.load(open(sys.argv[1]))
want = sys.argv[2]
for r in releases:
    tag = r.get("tag_name") or ""
    m = re.fullmatch(r"v?(\d+(?:\.\d+)+)", tag)
    if not m:
        continue
    if want:
        if m.group(1) != want:
            continue
    elif r.get("draft") or r.get("prerelease"):
        continue   # the newest *full* release, as livecheck sees it
    name = "CedarLogic-%s-Mac.dmg" % m.group(1)
    for a in r.get("assets") or []:
        if a.get("name") == name:
            print("\t".join([m.group(1), tag, name, a["browser_download_url"], a.get("digest") or "",
                             "prerelease" if r.get("prerelease") else "release"]))
            sys.exit(0)
    if want:
        sys.exit("release %s has no %s" % (tag, name))
sys.exit("no release %s with a Mac DMG in the newest 50" % (("v" + want) if want else "(full, not a beta)"))
PY
) || die "couldn't find the release"
IFS=$'\t' read -r VERSION TAG NAME URL DIGEST KIND <<<"$LINE"
[ "$KIND" = release ] || say "note: $TAG is a pre-release (a beta); the cask is meant for full releases"
say "$TAG: $NAME"

# The cask builds its url from the version; it must be this one.
CASK_URL=$(sed -n 's/^ *url "\([^"]*\)".*$/\1/p' "$CASK" | head -1)
[ -n "$CASK_URL" ] || die "no url line in $CASK"
EXPECT=${CASK_URL//\#\{version\}/$VERSION}
[ "$EXPECT" = "$URL" ] || die "the cask's url would be $EXPECT, but the release's file is $URL"

say "downloading $URL"
curl -fL --progress-bar -o "$WORK/$NAME" "$URL"
SHA=$(shasum -a 256 "$WORK/$NAME" | cut -d' ' -f1)
if [ -n "$DIGEST" ]; then
	[ "$DIGEST" = "sha256:$SHA" ] || die "the download's sha256 is $SHA, but GitHub's digest for it is $DIGEST"
	say "sha256 $SHA (matches GitHub's digest)"
else
	say "sha256 $SHA (GitHub gave no digest to compare with)"
fi

if [ "$MOUNT" = 1 ]; then
	MNT=$(mktemp -d "$WORK/mnt.XXXXXX")
	hdiutil attach -quiet -nobrowse -readonly -noautoopen -mountpoint "$MNT" "$WORK/$NAME" || { MNT=; die "couldn't open the DMG"; }
	APP="$MNT/CedarLogic.app"
	[ -d "$APP" ] || die "the DMG has no CedarLogic.app (the cask's app stanza names that)"
	PL="$APP/Contents/Info.plist"
	V=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$PL")
	ID=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "$PL")
	MIN=$(/usr/libexec/PlistBuddy -c "Print LSMinimumSystemVersion" "$PL" 2>/dev/null || echo "?")
	EXE=$(/usr/libexec/PlistBuddy -c "Print CFBundleExecutable" "$PL")
	ARCHS=$(lipo -archs "$APP/Contents/MacOS/$EXE" 2>/dev/null || echo "?")
	[ "$V" = "$VERSION" ] || die "the app in the DMG says version $V, not $VERSION"
	[ "$ID" = "$BUNDLE_ID" ] || die "the app's bundle id is $ID, not $BUNDLE_ID (the cask's uninstall/zap name that)"
	say "CedarLogic.app $V ($ID), macOS $MIN or newer, runs on: $ARCHS"
	case " $ARCHS " in
		*" x86_64 "*) grep -q 'depends_on arch: :arm64' "$CASK" && say "note: this build runs on Intel too: the cask's 'depends_on arch: :arm64' can go" ;;
		*) grep -q 'depends_on arch: :arm64' "$CASK" || say "WARNING: this build is Apple silicon only, and the cask doesn't say depends_on arch: :arm64" ;;
	esac
	hdiutil detach -quiet "$MNT" || true
	MNT=
fi

OLD_V=$(sed -n 's/^  version "\(.*\)"$/\1/p' "$CASK" | head -1)
OLD_S=$(sed -n 's/^  sha256 "\(.*\)"$/\1/p' "$CASK" | head -1)
[ -n "$OLD_V" ] && [ -n "$OLD_S" ] || die "couldn't find the cask's version and sha256 lines"
if [ "$DRY" = 1 ]; then
	say "dry run: would change version $OLD_V -> $VERSION, sha256 $OLD_S -> $SHA in $CASK"
	exit 0
fi
TMPC="$WORK/cask.rb"
sed -e "s/^  version \".*\"$/  version \"$VERSION\"/" -e "s/^  sha256 \".*\"$/  sha256 \"$SHA\"/" "$CASK" > "$TMPC"
cat "$TMPC" > "$CASK"
say "updated $CASK: version $OLD_V -> $VERSION"
if [ "$OLD_V" = "$VERSION" ] && [ "$OLD_S" != "$SHA" ] && [ "$OLD_S" != "$(printf "%064d" 0)" ]; then
	say "WARNING: same version, different sha256: the release's DMG was replaced. Homebrew users with the old one cached will see a checksum error until they 'brew update'."
fi
say "next: check it (brew style / brew audit, see README), then commit and push the tap"
