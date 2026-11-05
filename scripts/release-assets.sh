#!/usr/bin/env bash
# Release day, go-list steps 4 and 5: the Windows and Linux files onto the
# versioned release, under their public names, and the release's notes.
#
#   scripts/release-assets.sh --dry-run --commit C       every check and download; uploads nothing
#   scripts/release-assets.sh --go --commit C            upload to v<version> (--clobber), set the notes
#       --commit C        the release commit (go-list step 1's; both test builds must be on it)
#       --version V       default 0.4 (release v<V> in leviholliday/CedarLogic-Releases)
#       --source S        release (default): the signed windows-native-testing and
#                         linux-native-testing releases, after scripts/sign-preview.sh;
#                         run: the artifacts of the green CI run for C (no signed list needed)
#       --platform P      windows, linux or both (default both)
#       --files-only      step 4 only          --notes-only    step 5 only
#       --notes FILE      default docs/RELEASE-<V>-public-notes.md
#       --site DIR        a checkout of cedarlogic-site whose releases.json names the files
#                         (default $CEDARLOGIC_SITE, else ../cedarlogic-site next to the main checkout);
#                         --no-site skips that check
#       --work DIR        where files are downloaded (default .release-work/assets in this checkout)
#       --keep            leave the work folder (the staged, renamed files are in DIR/stage)
#       --stand-in        --dry-run only: take each test build's own commit instead of --commit
#                         (to try this before release day, when the two are on other commits)
#       --yes             don't ask before uploading
#
# Checks, for each platform (all of them in --dry-run):
#   - the testing release's tag, its branch (windows/native, linux/native) and a
#     green run of its workflow are all on C, and no run is still going
#   - with --source release: the release's SHA256SUMS verifies with the update
#     key, is for that tag and commit C, and names every file with its size and
#     SHA-256; each download matches GitHub's digest
#   - each file is the file of the same name in the CI run's artifacts
#   - exactly the files a build makes: Windows both installers and both
#     zips, Linux both .debs and both AppImages
#   - what's inside: the AppImages and the .debs are version V (padded to
#     three numbers), the .debs' architecture matches their names, the
#     installers are Inno Setup installers of version V, the zips hold
#     CedarLogic/CedarLogic.exe (Scoop's extract_dir)
#   - the public names (below) are the ones the site's releases.json links
#   - release v<V> exists and holds the Mac's DMG and updater zip; a
#     SHA256SUMS already on it is stale once files change (step 6 again)
# The renames (docs/RELEASE-0.4.md):
#   CedarLogic-Windows-Setup-<arch>.exe -> CedarLogic-<V>-Windows-Setup-<arch>.exe   (x64, ARM64)
#   CedarLogic-Windows-native-<arch>.zip -> CedarLogic-<V>-Windows-<arch>.zip        (x64, ARM64; the
#                                          Windows updater's file for Normal testers, and Scoop's)
#   cedarlogic_<arch>.deb -> cedarlogic_<V>_<arch>.deb                               (amd64, arm64)
#   CedarLogic-Native-<V.0>-<arch>.AppImage                                          (unchanged)
# --go then uploads them with --clobber, reads the release back to check each
# file's digest, and sets the notes (the file without its leading <!-- -->
# comment; it refuses while the file still says TODO-OWNER).
# shellcheck source-path=SCRIPTDIR
set -euo pipefail
ME=release-assets
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=release-lib.sh
. "$ROOT/scripts/release-lib.sh"

MODE='' COMMIT='' VERSION=0.4 SOURCE=release PLATFORMS='windows linux' DO_FILES=1 DO_NOTES=1
NOTES='' SITE=${CEDARLOGIC_SITE:-} NO_SITE=0 WORK='' KEEP=0 STANDIN=0 YES=${YES:-0}
while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) [ -z "$MODE" ] || die "--dry-run or --go, not both"; MODE=dry; shift ;;
		--go) [ -z "$MODE" ] || die "--dry-run or --go, not both"; MODE=go; shift ;;
		--commit) COMMIT=$2; shift 2 ;;
		--version) VERSION=${2#v}; shift 2 ;;
		--source) case "$2" in release|run) SOURCE=$2 ;; *) die "--source release|run" ;; esac; shift 2 ;;
		--platform) case "$2" in windows|linux) PLATFORMS=$2 ;; both) PLATFORMS='windows linux' ;; *) die "--platform windows|linux|both" ;; esac; shift 2 ;;
		--files-only) DO_NOTES=0; shift ;;
		--notes-only) DO_FILES=0; shift ;;
		--notes) NOTES=$2; shift 2 ;;
		--site) SITE=$2; shift 2 ;;
		--no-site) NO_SITE=1; shift ;;
		--work) WORK=$2; shift 2 ;;
		--keep) KEEP=1; shift ;;
		--stand-in) STANDIN=1; shift ;;
		--yes) YES=1; shift ;;
		-h|--help) sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown option $1" ;;
	esac
done
require_mode "$MODE"
[ "$DO_FILES$DO_NOTES" != 00 ] || die "--files-only and --notes-only together leave nothing to do"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "bad version $VERSION"
[ "$STANDIN" = 0 ] || [ "$MODE" = dry ] || die "--stand-in is for --dry-run only"
if [ "$DO_FILES" = 1 ] && [ "$STANDIN" = 0 ]; then
	[ -n "$COMMIT" ] || die "--commit C: the release commit (go-list step 1)"
	[[ "$COMMIT" =~ ^[0-9a-f]{40}$ ]] || COMMIT=$(git -C "$ROOT" rev-parse --verify "$COMMIT^{commit}") || die "not a commit: $COMMIT"
fi
command -v gh >/dev/null || die "needs the GitHub CLI (gh)"
OSSL=$(find_openssl) || die "needs OpenSSL 3 or later (brew install openssl@3, or OPENSSL=)"
command -v unzip >/dev/null || die "needs unzip"
TAG=v$VERSION V3=$(pad_version "$VERSION")
NOTES=${NOTES:-$ROOT/docs/RELEASE-$VERSION-public-notes.md}
WORK=${WORK:-$ROOT/.release-work/assets}
rm -rf "$WORK"; mkdir -p "$WORK/stage"
if [ "$KEEP" = 0 ]; then trap 'rm -rf "$WORK"' EXIT INT TERM; fi
PROBLEMS=0
problem() { warn "$*"; PROBLEMS=$((PROBLEMS + 1)); }

# ---- the target release --------------------------------------------------------
gh release view "$TAG" -R "$RELEASES_REPO" --json tagName >/dev/null 2>&1 || die "no release $TAG in $RELEASES_REPO (go-list step 3 makes it)"
TARGET=$(release_assets "$RELEASES_REPO" "$TAG")
TARGET_PRE=$(gh release view "$TAG" -R "$RELEASES_REPO" --json isPrerelease --jq .isPrerelease)
say "$TAG in $RELEASES_REPO ($([ "$TARGET_PRE" = true ] && echo pre-release || echo full release)) has:"
if [ -n "$TARGET" ]; then while read -r l; do say "    $l"; done <<<"$TARGET"; else say "    nothing"; fi

# ---- step 4: the files ---------------------------------------------------------------
check_inside() {   # platform original-name file public-name
	local orig=$2 f=$3 pub=$4 ctl ver arch maint
	case "$orig" in
		*.zip)
			unzip -l "$f" | awk '{ print $NF }' | grep -qx 'CedarLogic/CedarLogic.exe' \
				|| { problem "$orig has no CedarLogic/CedarLogic.exe (Scoop's extract_dir and the updater expect it)"; return; }
			say "    $orig: holds CedarLogic/CedarLogic.exe" ;;
		*.exe)
			python3 -I - "$f" "$V3" <<'PY' || problem "$orig isn't an Inno Setup installer of version $V3"
import sys
data = open(sys.argv[1], "rb").read()
want = sys.argv[2]
if data[:2] != b"MZ" or b"Inno Setup" not in data:
    sys.exit("    not an Inno Setup installer")
key = "ProductVersion".encode("utf-16-le")
i = data.find(key)
if i < 0:
    sys.exit("    no ProductVersion")
j = i + len(key)
while data[j:j + 2] == b"\0\0":
    j += 2
k = data.find(b"\0\0", j)
while k >= 0 and (k - j) % 2:
    k = data.find(b"\0\0", k + 1)
got = data[j:k].decode("utf-16-le", "replace").strip(" \0")
if got != want and not got.startswith(want + "."):
    sys.exit("    ProductVersion %s, not %s" % (got, want))
print("release-assets:     Inno Setup installer, version %s" % got, file=sys.stderr)
PY
			;;
		*.deb)
			ctl=$(ar t "$f" | grep '^control\.tar' | head -1) || true
			[ -n "$ctl" ] || { problem "$orig: no control.tar in the .deb"; return; }
			ar p "$f" "$ctl" > "$WORK/control.tar.x"
			tar -xOf "$WORK/control.tar.x" ./control > "$WORK/control" 2>/dev/null || tar -xOf "$WORK/control.tar.x" control > "$WORK/control" \
				|| { problem "$orig: can't read its control file"; return; }
			ver=$(sed -n 's/^Version: //p' "$WORK/control") arch=$(sed -n 's/^Architecture: //p' "$WORK/control")
			maint=$(sed -n 's/^Maintainer: //p' "$WORK/control")
			case "$ver" in "$V3"|"$V3"[+.~-]*) ;; *) problem "$orig is version $ver, not $V3" ;; esac
			case "$pub" in *_"$arch".deb) ;; *) problem "$orig's architecture is $arch, but it would be named $pub" ;; esac
			say "    $orig: Package $(sed -n 's/^Package: //p' "$WORK/control"), Version $ver, Architecture $arch, Maintainer $maint"
			case "$maint" in *@gmail.com*|*leviholliday7*) warn "$orig's Maintainer is a personal address ($maint): the publisher decision is \"CedarLogic <cedarlogic@proton.me>\" (linux/package-deb.sh)" ;; esac
			;;
		*.AppImage)
			head -c 4 "$f" | od -An -tx1 | tr -d ' \n' | grep -qx 7f454c46 || problem "$orig isn't an ELF program"
			say "    $orig: an ELF program (AppImage)" ;;
	esac
}

public_name() {   # platform original -> public name (empty: not a release file)
	case "$2" in
		CedarLogic-Windows-Setup-x64.exe|CedarLogic-Windows-Setup-ARM64.exe) echo "CedarLogic-$VERSION-Windows-${2#CedarLogic-Windows-}" ;;
		CedarLogic-Windows-native-x64.zip|CedarLogic-Windows-native-ARM64.zip) echo "CedarLogic-$VERSION-Windows-${2#CedarLogic-Windows-native-}" ;;
		cedarlogic_amd64.deb|cedarlogic_arm64.deb) echo "cedarlogic_${VERSION}_${2#cedarlogic_}" ;;
		"CedarLogic-Native-$V3-x86_64.AppImage"|"CedarLogic-Native-$V3-aarch64.AppImage") echo "$2" ;;
	esac
}

EXPECTED_windows="CedarLogic-Windows-Setup-ARM64.exe CedarLogic-Windows-Setup-x64.exe CedarLogic-Windows-native-ARM64.zip CedarLogic-Windows-native-x64.zip"
EXPECTED_linux="CedarLogic-Native-$V3-aarch64.AppImage CedarLogic-Native-$V3-x86_64.AppImage cedarlogic_amd64.deb cedarlogic_arm64.deb"
ARTIFACTS_windows="CedarLogic-Windows-native-x64 CedarLogic-Windows-native-ARM64"
ARTIFACTS_linux="CedarLogic-Native-linux-appimage-x86_64 CedarLogic-Native-linux-appimage-aarch64 CedarLogic-Native-linux-deb-x86_64 CedarLogic-Native-linux-deb-aarch64"

if [ "$DO_FILES" = 1 ]; then
	: > "$WORK/staged.tsv"   # public  original  platform  sha256  size
	for P in $PLATFORMS; do
		if [ "$P" = windows ]; then T=$WIN_TAG BR=$WIN_BRANCH WF=$WIN_WORKFLOW EXP=$EXPECTED_windows ARTS=$ARTIFACTS_windows
		else T=$LINUX_TAG BR=$LINUX_BRANCH WF=$LINUX_WORKFLOW EXP=$EXPECTED_linux ARTS=$ARTIFACTS_linux; fi
		say "--- $P: $T ($SOURCE)"
		TC=$(ref_commit "$SOURCE_REPO" "$T") || die "no tag $T in $SOURCE_REPO"
		HEAD=$(ref_commit "$SOURCE_REPO" "$BR") || die "no branch $BR"
		WANT=$COMMIT
		if [ "$STANDIN" = 1 ]; then
			WANT=$TC
			[ -z "$COMMIT" ] || [ "$COMMIT" = "$TC" ] || say "  stand-in: $T is on ${TC:0:12}, not the release commit ${COMMIT:0:12} (taken as it is, for this dry run)"
		fi
		[ "$TC" = "$WANT" ] || die "$T is on ${TC:0:12}, not the release commit ${WANT:0:12}: run go-list step 1 (scripts/release-ci.sh) first"
		[ "$HEAD" = "$WANT" ] || die "$BR is on ${HEAD:0:12}, not ${WANT:0:12}: something newer was pushed there; it will publish over $T"
		RUN=$(green_run "$SOURCE_REPO" "$WF" "$BR" "$WANT")
		[ -n "$RUN" ] || die "no green $WF run on $BR for ${WANT:0:12}"
		PENDING=$(gh run list -R "$SOURCE_REPO" --workflow "$WF" --branch "$BR" --limit 20 --json status --jq '[.[] | select(.status != "completed")] | length')
		[ "$PENDING" = 0 ] || die "a $WF run on $BR is still going: it may publish over $T"
		say "  $T, $BR and green run $RUN are all on ${WANT:0:12} (build $(gh api "repos/$SOURCE_REPO/commits/$WANT" --jq '.commit.committer.date | fromdateiso8601'))"

		SRC=$WORK/src/$P CI=$WORK/ci/$P
		mkdir -p "$SRC" "$CI"
		for a in $ARTS; do
			gh run download "$RUN" -R "$SOURCE_REPO" -n "$a" -D "$CI/$a" || die "couldn't download artifact $a of run $RUN (expired? then use the signed release and compare by hand)"
		done
		if [ "$SOURCE" = release ]; then
			ASSETS=$(release_assets "$SOURCE_REPO" "$T")
			{ grep -q '^SHA256SUMS ' <<<"$ASSETS" && grep -q '^SHA256SUMS.sig ' <<<"$ASSETS"; } \
				|| die "$T has no signed list: run scripts/sign-preview.sh $P first (or --source run)"
			gh release download "$T" -R "$SOURCE_REPO" -D "$SRC" -p SHA256SUMS -p SHA256SUMS.sig --clobber
			verify_signed_list "$SRC/SHA256SUMS" "$SRC/SHA256SUMS.sig" "$WORK" || die "$T's SHA256SUMS doesn't verify with the update key"
			[ "$(sed -n 1p "$SRC/SHA256SUMS")" = "$MANIFEST_HEADER" ] && [ "$(sed -n 2p "$SRC/SHA256SUMS")" = "tag $T" ] \
				|| die "$T's SHA256SUMS isn't a list for $T"
			[ "$(sed -n 3p "$SRC/SHA256SUMS")" = "commit $WANT" ] \
				|| die "$T's SHA256SUMS is for $(sed -n 3p "$SRC/SHA256SUMS"), not ${WANT:0:12}: run scripts/sign-preview.sh $P"
			say "  SHA256SUMS verified with the update key: tag $T, commit ${WANT:0:12}, $(sed -n 4p "$SRC/SHA256SUMS")"
			NAMES=$(cut -d' ' -f1 <<<"$ASSETS" | grep -Ev '^(SHA256SUMS|SHA256SUMS\.sig|install\.sh)$' || true)
		else
			NAMES=$(cd "$CI" && find . -type f \( -name '*.exe' -o -name '*.zip' -o -name '*.deb' -o -name '*.AppImage' \) -exec basename {} \; | LC_ALL=C sort -u)
		fi
		GOT=$(LC_ALL=C sort <<<"$NAMES" | tr '\n' ' ' | sed 's/ $//')
		[ "$GOT" = "$EXP" ] || die "$P files are \"$GOT\", expected exactly \"$EXP\""
		for n in $NAMES; do
			ci=$(find "$CI" -type f -name "$n")
			[ "$(printf '%s\n' "$ci" | grep -c .)" = 1 ] || die "the CI run has no single $n"
			if [ "$SOURCE" = release ]; then
				gh release download "$T" -R "$SOURCE_REPO" -D "$SRC" -p "$n" --clobber
				f=$SRC/$n
				h=$(sha256_of "$f") s=$(size_of "$f")
				d=$(awk -v n="$n" '$1 == n { print $3 }' <<<"$ASSETS")
				[ -z "$d" ] || [ "$d" = "sha256:$h" ] || die "$n: sha256 $h, but GitHub's digest says $d"
				grep -qx "file $n $s $h" "$SRC/SHA256SUMS" || die "$n ($s bytes, $h) isn't what $T's signed SHA256SUMS lists"
				[ "$(sha256_of "$ci")" = "$h" ] || die "$n on $T isn't the file CI run $RUN built"
			else
				f=$ci h=$(sha256_of "$f") s=$(size_of "$f")
			fi
			pub=$(public_name "$P" "$n")
			[ -n "$pub" ] || die "no public name for $n"
			say "  $n -> $pub  ($s bytes, sha256 $h$([ "$SOURCE" = release ] && echo ", = signed list = CI artifact"))"
			check_inside "$P" "$n" "$f" "$pub"
			cp "$f" "$WORK/stage/$pub"
			printf '%s\t%s\t%s\t%s\t%s\n' "$pub" "$n" "$P" "$h" "$s" >> "$WORK/staged.tsv"
		done
		rm -rf "$CI"
	done
	[ "$PROBLEMS" = 0 ] || die "$PROBLEMS problem(s) with the files (above): not uploading"

	# ---- the names the site links ----
	if [ "$NO_SITE" = 0 ]; then
		if [ -z "$SITE" ]; then
			common=$(cd "$ROOT" && git rev-parse --path-format=absolute --git-common-dir)
			SITE=$(dirname "$(dirname "$common")")/cedarlogic-site
		fi
		[ -f "$SITE/releases.json" ] || die "no releases.json in $SITE (--site DIR, or --no-site)"
		python3 -I - "$SITE/releases.json" "$VERSION" "$RELEASES_REPO" "$WORK/staged.tsv" "$PLATFORMS" <<'PY' || die "the site's releases.json and these files disagree (above)"
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
version, repo, platforms = sys.argv[2], sys.argv[3], sys.argv[5].split()
staged = {l.split("\t")[0] for l in open(sys.argv[4], encoding="utf-8") if l.strip()}
bad = 0
base = "https://github.com/%s/releases/download/v%s/" % (repo, version)
want = []
nxt = d["mac"].get("next") or {}
if nxt.get("version") != version:
    print("release-assets:   site: mac.next is %r, not %s" % (nxt.get("version"), version), file=sys.stderr); bad = 1
for p in platforms:
    rel = d[p]["channels"]["release"]
    if rel.get("version") != version:
        print("release-assets:   site: %s's release channel is %r, not %s" % (p, rel.get("version"), version), file=sys.stderr); bad = 1
    for group in ("files", "portable", "appimages"):
        for k, e in (rel.get(group) or {}).items():
            want.append((p, group, k, e))
for p, group, k, e in want:
    ok = e["file"] in staged and e.get("url") == base + e["file"]
    print("release-assets:   site %s %s.%s: %s %s" % (p, group, k, e["file"], "ok" if ok else "MISSING or wrong url"), file=sys.stderr)
    bad |= not ok
extra = staged - {e["file"] for _, _, _, e in want}
for n in sorted(extra):
    print("release-assets:   note: %s isn't linked by the site (fine if nothing needs it)" % n, file=sys.stderr)
sys.exit(1 if bad else 0)
PY
		say "  every file the site links for $PLATFORMS is here, under the name it links ($SITE/releases.json)"
	fi

	# ---- against the target ----
	for want in "CedarLogic-$VERSION-Mac.dmg" "CedarLogic-Native-$VERSION-mac.zip"; do
		grep -q "^$want " <<<"$TARGET" || warn "$TAG has no $want yet (go-list steps 2 and 3 put the Mac's files there)"
	done
	while IFS=$'\t' read -r pub _ _ h _; do
		old=$(awk -v n="$pub" '$1 == n { print $3 }' <<<"$TARGET")
		if [ -z "$old" ]; then say "  $TAG: would add $pub"
		elif [ "$old" = "sha256:$h" ]; then say "  $TAG: $pub is already this file"
		else say "  $TAG: would REPLACE $pub (now $old)"; fi
	done < "$WORK/staged.tsv"
	if grep -q '^SHA256SUMS ' <<<"$TARGET"; then warn "$TAG already has a SHA256SUMS: once these files change it's stale; sign again (go-list step 6)"; fi

	if [ "$MODE" = dry ]; then
		say "dry run: would run gh release upload $TAG -R $RELEASES_REPO --clobber $(cut -f1 "$WORK/staged.tsv" | tr '\n' ' ')"
	else
		confirm "Upload $(wc -l < "$WORK/staged.tsv" | tr -d ' ') files to $TAG in $RELEASES_REPO (replacing any of the same name)?" || die "not uploaded"
		FILES=()
		while IFS=$'\t' read -r pub _; do FILES+=("$WORK/stage/$pub"); done < "$WORK/staged.tsv"
		gh release upload "$TAG" -R "$RELEASES_REPO" --clobber "${FILES[@]}" || die "upload failed: run this again (it replaces what's there)"
		AFTER=$(release_assets "$RELEASES_REPO" "$TAG")
		while IFS=$'\t' read -r pub _ _ h s; do
			line=$(grep "^$pub " <<<"$AFTER" || true)
			[ -n "$line" ] || die "$pub isn't on $TAG after the upload"
			read -r _ size digest <<<"$line"
			[ "$size" = "$s" ] || die "$pub on $TAG is $size bytes, uploaded $s"
			[ -z "$digest" ] || [ "$digest" = "sha256:$h" ] || die "$pub on $TAG has digest $digest, uploaded sha256:$h"
		done < "$WORK/staged.tsv"
		say "uploaded and read back: $(cut -f1 "$WORK/staged.tsv" | tr '\n' ' ')"
		say "next: go-list step 6, packaging/sign-release.sh $TAG (it must come after every file)"
	fi
fi

# ---- step 5: the notes ---------------------------------------------------------------
if [ "$DO_NOTES" = 1 ]; then
	[ -f "$NOTES" ] || die "no notes file $NOTES"
	# Without a leading <!-- ... --> comment (notes to the owner).
	perl -0pe 's/\A\s*<!--.*?-->\s*//s' "$NOTES" > "$WORK/notes.md"
	[ -s "$WORK/notes.md" ] || die "$NOTES is empty"
	if grep -q 'TODO-OWNER' "$NOTES"; then
		if [ "$MODE" = go ]; then die "$NOTES still has a TODO-OWNER note: check what it asks, then take the note out"; fi
		warn "$NOTES still has a TODO-OWNER note (--go refuses until it's gone):"
		grep -n 'TODO-OWNER' "$NOTES" | sed "s/^/$ME:     /" >&2
	fi
	if grep -Eiq 'preview|beta [0-9]|[(]beta[)]|test build from|not a release' "$WORK/notes.md"; then warn "the notes read like a beta or a test build somewhere: release day says neither"; fi
	gh release view "$TAG" -R "$RELEASES_REPO" --json body --jq .body > "$WORK/old-notes.md"
	say "notes: $NOTES ($(wc -l < "$WORK/notes.md" | tr -d ' ') lines, $(wc -c < "$WORK/notes.md" | tr -d ' ') bytes) to replace $TAG's ($(wc -c < "$WORK/old-notes.md" | tr -d ' ') bytes, starting \"$(head -c 60 "$WORK/old-notes.md" | tr '\n' ' ')\")"
	head -5 "$WORK/notes.md" | sed "s/^/$ME:   | /" >&2
	if [ "$MODE" = dry ]; then
		say "dry run: would run gh release edit $TAG -R $RELEASES_REPO --notes-file <$NOTES without its comment>"
	else
		confirm "Set $TAG's notes to $NOTES?" || die "notes not set"
		gh release edit "$TAG" -R "$RELEASES_REPO" --notes-file "$WORK/notes.md" >/dev/null
		gh release view "$TAG" -R "$RELEASES_REPO" --json body --jq .body > "$WORK/new-notes.md"
		diff -q <(sed -e 's/[[:space:]]*$//' "$WORK/notes.md") <(sed -e 's/[[:space:]]*$//' "$WORK/new-notes.md") >/dev/null \
			|| warn "the release's notes read back differently from the file (GitHub may have changed line endings): look at the release page"
		say "notes set on $TAG"
	fi
fi
say "done ($MODE)"
