#!/bin/bash
# Push a CedarLogic update to testers' apps: they get it from inside the app
# (Help > Check for Updates..., or the automatic daily check). No reinstalling.
#
#   scripts/publish-update.sh beta   4.0.1    beta testers only
#   scripts/publish-update.sh normal 4.0.1    everyone (beta testers too)
#   scripts/publish-update.sh promote 4.0.1   a beta that's settled -> everyone,
#                                             same files, nothing rebuilt
#
# Options (before or after the version):
#   --notes FILE      what's new, in Markdown (shown in the update window)
#   --mac FILE        wx Mac DMG      (default: build/CedarLogic-<v>-mac.dmg)
#   --native APP      native Mac app  (e.g. "mac/build/CedarLogic Native.app")
#   --no-ci           don't download the Windows/Linux builds from GitHub
#   --dry-run         build the feeds and show them, publish nothing
#
# What it does:
#   1. collects the builds into dist/<version>/: the Mac DMG from this machine,
#      the Windows installer and Linux AppImages from the newest green CI runs
#      of WIN_BRANCH / LINUX_BRANCH (default windows/phase1, linux/port)
#   2. signs the Mac and Windows files with the update key in your keychain
#      (Sparkle's sign_update, account "cedarlogic"); the Linux files get a
#      SHA-256 in the feed
#   3. makes (or adds to) the GitHub release v<version> in the releases repo,
#      a pre-release when it's a beta
#   4. adds the builds to the feeds there and pushes:
#        appcast.xml / appcast-beta.xml                 CedarLogic (wx) app
#        appcast-native.xml / appcast-native-beta.xml   CedarLogic Native
#      Beta feeds carry everything; normal feeds only what's been released
#      to everyone.
#
# Every build must be newer than what testers have, so bump the version first
# (scripts/set-version.sh 4.0.1), push, wait for CI, build the Mac app, then
# run this.

set -euo pipefail

REPO="${RELEASES_REPO:-leviholliday/CedarLogic-Releases}"
SOURCE_REPO="${SOURCE_REPO:-leviholliday/CedarLogic}"
WIN_BRANCH="${WIN_BRANCH:-windows/phase1}"
LINUX_BRANCH="${LINUX_BRANCH:-linux/port}"
KEY_ACCOUNT="${KEY_ACCOUNT:-cedarlogic}"

die() { echo "publish-update: $*" >&2; exit 1; }
usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

MODE="${1:-}"; shift || true
case "$MODE" in beta|normal|promote) ;; *) usage ;; esac

VERSION="" NOTES="" MAC="" NATIVE="" USE_CI=1 DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --notes) NOTES="$2"; shift 2 ;;
        --mac) MAC="$2"; shift 2 ;;
        --native) NATIVE="$2"; shift 2 ;;
        --no-ci) USE_CI=0; shift ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) usage ;;
        -*) die "unknown option $1" ;;
        *) [ -z "$VERSION" ] || die "two versions given"; VERSION="${1#v}"; shift ;;
    esac
done
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "give a version like 4.0.1 (numbers only; the channel says whether it's a beta)"
TAG="v$VERSION"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ---- the releases repo, where the feeds live ---------------------------------
gh repo clone "$REPO" "$WORK/releases" -- --depth 1 -q || die "could not clone $REPO"

if [ "$MODE" = promote ]; then
    python3 "$ROOT/scripts/appcast.py" promote "$WORK/releases" "$VERSION"
    if [ "$DRY" = 1 ]; then
        (cd "$WORK/releases" && git --no-pager diff); exit 0
    fi
    gh release edit "$TAG" -R "$REPO" --prerelease=false --latest \
        --title "CedarLogic $VERSION" >/dev/null
    (cd "$WORK/releases" && git add -A && git commit -qm "Release $VERSION to everyone" && git push -q)
    echo "CedarLogic $VERSION is now offered to everyone."
    exit 0
fi

# ---- 1. collect ----------------------------------------------------------------
DIST="$ROOT/dist/$VERSION"
mkdir -p "$DIST"

[ -n "$MAC" ] || MAC="$ROOT/build/CedarLogic-$VERSION-mac.dmg"
if [ -f "$MAC" ]; then cp -f "$MAC" "$DIST/"; else echo "note: no wx Mac DMG at $MAC (scripts/package-mac.sh makes it); skipping Mac"; fi

fetch_ci() {   # workflow branch artifact-pattern
    local id
    id=$(gh run list -R "$SOURCE_REPO" -w "$1" -b "$2" -s success -L 1 \
            --json databaseId,headSha -q '.[0].databaseId') || true
    if [ -z "$id" ]; then echo "note: no green '$1' run on $2; skipping"; return; fi
    echo "downloading $3 from '$1' run $id ($2)"
    gh run download "$id" -R "$SOURCE_REPO" -p "$3" -D "$WORK/ci" || { echo "note: download failed; skipping"; return; }
    find "$WORK/ci" -type f \( -name '*.exe' -o -name '*.AppImage' \) -exec cp -f {} "$DIST/" \;
}
if [ "$USE_CI" = 1 ]; then
    fetch_ci "Windows build" "$WIN_BRANCH" "CedarLogic-windows-installer"
    fetch_ci "Linux build" "$LINUX_BRANCH" "CedarLogic-linux-appimage-*"
fi

NATIVE_ZIP="" NATIVE_BUILD="" NATIVE_SHORT=""
if [ -n "$NATIVE" ]; then
    [ -d "$NATIVE" ] || die "no app at $NATIVE"
    # The native app counts its own builds (CFBundleVersion = commit count,
    # set by mac/build.sh); Sparkle compares that, so it goes in the feed.
    NATIVE_BUILD=$(/usr/libexec/PlistBuddy -c "Print CFBundleVersion" "$NATIVE/Contents/Info.plist")
    NATIVE_SHORT=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$NATIVE/Contents/Info.plist")
    NATIVE_ZIP="$DIST/CedarLogic-Native-$VERSION-mac.zip"
    rm -f "$NATIVE_ZIP"
    ditto -c -k --sequesterRsrc --keepParent "$NATIVE" "$NATIVE_ZIP"
fi

# Only this version's files: anything else in dist/ would ship an old build
# under a new number.
shopt -s nullglob
FILES=()
for f in "$DIST"/*; do
    case "$(basename "$f")" in
        *"-$VERSION-"*|*"-$VERSION."*) FILES+=("$f") ;;
        *) echo "note: ignoring $(basename "$f") (not version $VERSION)"; ;;
    esac
done
[ ${#FILES[@]} -gt 0 ] || die "nothing to publish for $VERSION"

# ---- 2. sign ---------------------------------------------------------------------
SIGN=""
for c in "$ROOT/build/_deps/sparkle-src/bin/sign_update" \
         "$ROOT/../../../build/_deps/sparkle-src/bin/sign_update" \
         "$(command -v sign_update || true)"; do
    [ -x "$c" ] && { SIGN="$c"; break; }
done
[ -n "$SIGN" ] || die "Sparkle's sign_update not found (configure the wx build once to download Sparkle)"

MANIFEST="$WORK/manifest.tsv"   # file  os  arch  signature  sha256  length
: > "$MANIFEST"
for f in "${FILES[@]}"; do
    name=$(basename "$f"); len=$(stat -f%z "$f"); sig=""; sha=""; arch=""
    case "$name" in
        CedarLogic-Native-*.zip) os=native ;;
        *-mac.dmg) os=macos ;;
        *-win32.exe) os=windows ;;
        *.AppImage) os=linux; arch=$(echo "$name" | sed -E 's/.*-([^-]+)\.AppImage$/\1/') ;;
        *) echo "note: not a build this knows: $name"; continue ;;
    esac
    if [ "$os" = linux ]; then
        sha=$(shasum -a 256 "$f" | cut -d' ' -f1)
    else
        out=$("$SIGN" --account "$KEY_ACCOUNT" "$f") || die "signing $name failed (is the key in your keychain?)"
        sig=$(echo "$out" | sed -n 's/.*sparkle:edSignature="\([^"]*\)".*/\1/p')
        [ -n "$sig" ] || die "no signature for $name: $out"
    fi
    fv="" fs=""
    [ "$os" = native ] && { fv="$NATIVE_BUILD"; fs="$NATIVE_SHORT (build $NATIVE_BUILD)"; }
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$os" "$arch" "$sig" "$sha" "$len" "$fv" "$fs" >> "$MANIFEST"
    echo "ready: $name ($os${arch:+ $arch})"
done

# ---- 4. feeds (built before uploading, so a mistake stops everything) -----------
python3 "$ROOT/scripts/appcast.py" add "$WORK/releases" "$VERSION" "$MODE" \
    "https://github.com/$REPO/releases/download/$TAG" "$MANIFEST" "${NOTES:-}"

if [ "$DRY" = 1 ]; then
    (cd "$WORK/releases" && git --no-pager diff --stat && git --no-pager diff)
    echo "dry run: nothing published"
    exit 0
fi

# ---- 3. release ---------------------------------------------------------------------
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    echo "adding to existing release $TAG"
else
    TITLE="CedarLogic $VERSION"; FLAGS=()
    if [ "$MODE" = beta ]; then TITLE="$TITLE beta"; FLAGS+=(--prerelease); fi
    if [ -n "$NOTES" ]; then FLAGS+=(--notes-file "$NOTES"); else FLAGS+=(--notes "Update $VERSION. Get it from inside CedarLogic: Help > Check for Updates."); fi
    gh release create "$TAG" -R "$REPO" --title "$TITLE" "${FLAGS[@]}"
fi
UPLOAD=()
while IFS=$'\t' read -r name _; do UPLOAD+=("$DIST/$name"); done < "$MANIFEST"
gh release upload "$TAG" -R "$REPO" --clobber "${UPLOAD[@]}"

(cd "$WORK/releases" && git add -A && git commit -qm "Publish $VERSION ($MODE)" && git push -q)
echo
echo "Published CedarLogic $VERSION to $( [ "$MODE" = beta ] && echo "beta testers" || echo everyone )."
echo "Testers get it at their next check (daily), or right away with Help > Check for Updates."
