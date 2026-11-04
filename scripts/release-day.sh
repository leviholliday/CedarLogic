#!/usr/bin/env bash
# CedarLogic release day: packaging/README.md's go-list, steps 0 to 16, in
# order, with a checkpoint between each.
#
#   scripts/release-day.sh --dry-run            every step's checks, nothing changed anywhere
#   scripts/release-day.sh --go                 the real thing; asks y/N before each step
#       --version V        default 0.4
#       --commit C         the release commit (default: private/main's tip; --go remembers it
#                          in .release-work/state, so a resumed run uses the same one)
#       --from N / --to N / --only N   run part of the list (resume after a stop with --from N)
#       --yes              don't ask between steps (owner steps still stop)
#       --site DIR         the site checkout (default $CEDARLOGIC_SITE, else ../cedarlogic-site
#                          next to the main checkout)
#       --tap DIR          the Homebrew tap checkout (default $CEDARLOGIC_TAP, else ~/src/homebrew-tap)
#       --bucket DIR       the Scoop bucket checkout (default $CEDARLOGIC_SCOOP_BUCKET, else
#                          ~/src/scoop-cedarlogic)
#       --mac-app APP      the built Mac app (default mac/build/CedarLogic.app; given, step 2
#                          checks it instead of building)
#       --mac-notes FILE   the Mac update window's notes (default the public notes)
#       --notes FILE       the release page's notes (default docs/RELEASE-<V>-public-notes.md)
#       --work DIR         downloads and state (default .release-work here; TMPDIR goes there too)
#       --keep             a dry run leaves its downloads in the work folder
#
# What each step does (--go), and checks (--dry-run does every check):
#    0 ready      tools, the remote, main green at C, C's Mac build above the feed's, keys,
#                 checkouts, notes                                 (checks only)
#    1 CI         scripts/release-ci.sh: push C to windows/native and linux/native (fast-forward
#                 only), wait for CI, scripts/sign-preview.sh windows / linux
#    2 Mac build  CLEAN=1 mac/build.sh, mac/package.sh, check the version and build
#    3 Mac beta   publish-update.sh beta V --native ... --no-ci, upload the DMG, check the feed's
#                 one item for the zip: its build, length and Sparkle signature
#    4 files      scripts/release-assets.sh --files-only: Windows and Linux files onto vV, renamed
#    5 notes      scripts/release-assets.sh --notes-only
#    6 sign       packaging/sign-release.sh vV --build <C's commit time> --upload
#    7 promote    publish-update.sh promote V, then check vV is a full release, Latest, all files
#    8 one-liners (owner) install.sh / install.ps1 against the real release, on the devices
#    9 Homebrew   packaging/homebrew/bump.sh V --tap ..., brew style / audit, commit + push the tap
#   10 Scoop      make leviholliday/scoop-cedarlogic if needed, bump.sh --bucket, commit + push
#   11 winget     packaging/winget/bump.sh V, then (owner) the PC checks and the PR
#   12 README     the Releases repo's README from docs/drafts/Releases-README.md, commit + push
#   13 this repo  (owner) commit the bumped cask, Scoop and winget manifests, SUPPORT.md
#   14 website    (owner, production) STOPS and prints the site's release-day commands
#   15 check      live checks of the site's scripts and /get/ links, then (owner) the checklist
#   16 later      (owner) winget, once Microsoft merges the PR
# Owner steps print exactly what to run and stop (step 14 always stops; the
# others ask whether they're done, and stop on no or with --yes).
#
# In a dry run, a check that can't pass yet because an earlier step hasn't
# happened (v0.4 has no Windows files before step 4, say) is PENDING, not
# FAIL, and where it can, the step also tries its tools on stand-ins: the
# current windows-native-testing / linux-native-testing builds, renamed and
# staged locally, signed with a throwaway key, fed to the bump scripts and to
# the site's release_day.py through their test hooks. The summary at the end
# lists every step's result.
# shellcheck source-path=SCRIPTDIR
# shellcheck disable=SC2329,SC2015  # the steps are called by name; A && B || C on purpose
set -euo pipefail
ME=release-day
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=release-lib.sh
. "$ROOT/scripts/release-lib.sh"

MODE='' VERSION=0.4 COMMIT='' FROM=0 TO=16 YES=0 KEEP=0
SITE=${CEDARLOGIC_SITE:-} TAP=${CEDARLOGIC_TAP:-$HOME/src/homebrew-tap} BUCKET=${CEDARLOGIC_SCOOP_BUCKET:-$HOME/src/scoop-cedarlogic}
APP='' APP_GIVEN=0 MAC_NOTES='' NOTES='' WORKDIR=''
while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) [ -z "$MODE" ] || die "--dry-run or --go, not both"; MODE=dry; shift ;;
		--go) [ -z "$MODE" ] || die "--dry-run or --go, not both"; MODE=go; shift ;;
		--version) VERSION=${2#v}; shift 2 ;;
		--commit) COMMIT=$2; shift 2 ;;
		--from) FROM=$2; shift 2 ;;
		--to) TO=$2; shift 2 ;;
		--only) FROM=$2; TO=$2; shift 2 ;;
		--yes) YES=1; shift ;;
		--site) SITE=$2; shift 2 ;;
		--tap) TAP=$2; shift 2 ;;
		--bucket) BUCKET=$2; shift 2 ;;
		--mac-app) APP=$2; APP_GIVEN=1; shift 2 ;;
		--mac-notes) MAC_NOTES=$2; shift 2 ;;
		--notes) NOTES=$2; shift 2 ;;
		--work) WORKDIR=$2; shift 2 ;;
		--keep) KEEP=1; shift ;;
		-h|--help) sed -n '2,52p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown option $1" ;;
	esac
done
require_mode "$MODE"
export YES
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "bad version $VERSION"
[[ "$FROM" =~ ^[0-9]+$ && "$TO" =~ ^[0-9]+$ && "$FROM" -le "$TO" ]] || die "--from/--to/--only take step numbers 0-16"
TAG=v$VERSION
NOTES=${NOTES:-$ROOT/docs/RELEASE-$VERSION-public-notes.md}
MAC_NOTES=${MAC_NOTES:-$NOTES}
APP=${APP:-$ROOT/mac/build/CedarLogic.app}
DMG=$ROOT/mac/build/CedarLogic-$VERSION-Mac.dmg
WORKDIR=${WORKDIR:-$ROOT/.release-work}
COMMON=$(cd "$ROOT" && git rev-parse --path-format=absolute --git-common-dir)
MAIN_CHECKOUT=$(dirname "$COMMON")
SITE=${SITE:-$(dirname "$MAIN_CHECKOUT")/cedarlogic-site}
COPY_BRANCH=$PRIVATE_REMOTE/docs/release-0.4-copy   # PR #20, until it's merged
mkdir -p "$WORKDIR/tmp" "$WORKDIR/sim"
export TMPDIR="$WORKDIR/tmp"    # the sub-scripts' downloads land here, not in /var/folders
SUMMARY=$WORKDIR/summary.tsv
: > "$SUMMARY"
command -v gh >/dev/null || die "needs the GitHub CLI (gh)"
# shellcheck disable=SC2034  # used by release-lib.sh
OSSL=$(find_openssl) || die "needs OpenSSL 3 or later (brew install openssl@3)"

# ---- the release commit ----------------------------------------------------------
git -C "$ROOT" fetch -q "$PRIVATE_REMOTE" || die "git fetch $PRIVATE_REMOTE failed"
STATE=$WORKDIR/state
if [ -z "$COMMIT" ] && [ "$MODE" = go ] && [ -f "$STATE" ]; then
	COMMIT=$(sed -n 's/^commit //p' "$STATE")
	say "release commit from $STATE: $COMMIT (--commit to change it)"
fi
COMMIT=${COMMIT:-$PRIVATE_REMOTE/main}
COMMIT=$(git -C "$ROOT" rev-parse --verify "$COMMIT^{commit}") || die "not a commit: $COMMIT"
[ "$MODE" = dry ] || printf 'commit %s\nversion %s\n' "$COMMIT" "$VERSION" > "$STATE"
BUILD_NUM=$(git -C "$ROOT" rev-list --count "$COMMIT")        # the Mac's CFBundleVersion (mac/build.sh)
BUILD_TIME=$(git -C "$ROOT" log -1 --format=%ct "$COMMIT")    # the Linux/Windows manifests' build
say "$MODE run for CedarLogic $VERSION: commit ${COMMIT:0:12} \"$(git -C "$ROOT" log -1 --format=%s "$COMMIT")\""
say "  Mac build number $BUILD_NUM, manifest build $BUILD_TIME; work folder $WORKDIR"

# ---- helpers ---------------------------------------------------------------------------
STEP=0
res() {   # STATUS message   (OK WARN PENDING FAIL OWNER)
	printf '%s\t%s\t%s\n' "$STEP" "$1" "$2" >> "$SUMMARY"
	case "$1" in
		OK) say "ok: $2" ;;
		FAIL) say "FAIL: $2" ;;
		*) say "$1: $2" ;;
	esac
}
# PENDING in a dry run, FAIL (and stop) in a real one.
pend() { if [ "$MODE" = dry ]; then res PENDING "$1"; else res FAIL "$1"; return 1; fi; }

LOG=$WORKDIR/last.log
# A sub-script: in a dry run its output is indented and kept in $LOG; for real it runs as it is
# (so its own prompts show).
sub() {
	if [ "$MODE" = go ]; then "$@"; return; fi
	set +e
	"$@" > "$LOG" 2>&1
	local rc=$?
	set -e
	sed -E -e '/#=#=#|#{20}/d' -e 's/^/    | /' "$LOG" >&2
	return "$rc"
}
# Why the last sub-script stopped: its last two lines.
why() { sed -E '/#=#=#|#{20}/d' "$LOG" | tail -2 | tr '\n' ' ' | sed 's/  */ /g' | cut -c1-200; }

strip_comment() { perl -0pe 's/\A\s*<!--.*?-->\s*//s' "$1" > "$2"; }

# What the release holds now.
REL_JSON=$WORKDIR/release.json
facts() {
	gh api "repos/$RELEASES_REPO/releases/tags/$TAG" > "$REL_JSON" 2>/dev/null || { echo '{}' > "$REL_JSON"; }
	REL_EXISTS=$(python3 -I -c 'import json,sys; print("yes" if json.load(open(sys.argv[1])).get("tag_name") else "no")' "$REL_JSON")
	REL_PRE=$(python3 -I -c 'import json,sys; print(str(json.load(open(sys.argv[1])).get("prerelease", True)).lower())' "$REL_JSON")
	REL_NAMES=$(python3 -I -c 'import json,sys; print("\n".join(a["name"] for a in json.load(open(sys.argv[1])).get("assets", [])))' "$REL_JSON")
	has() { grep -qx "$1" <<<"$REL_NAMES"; }
	HAS_WIN=no HAS_LINUX=no HAS_SUMS=no
	if has "CedarLogic-$VERSION-Windows-x64.zip"; then HAS_WIN=yes; fi
	if has "cedarlogic_${VERSION}_amd64.deb"; then HAS_LINUX=yes; fi
	if has SHA256SUMS && has SHA256SUMS.sig; then HAS_SUMS=yes; fi
}

# Every file the release must hold on the day (packaging/README.md).
V3=$(pad_version "$VERSION")
EXPECTED_FILES="CedarLogic-$VERSION-Mac.dmg CedarLogic-Native-$VERSION-mac.zip
CedarLogic-$VERSION-Windows-Setup-x64.exe CedarLogic-$VERSION-Windows-Setup-ARM64.exe
CedarLogic-$VERSION-Windows-x64.zip CedarLogic-$VERSION-Windows-ARM64.zip
cedarlogic_${VERSION}_amd64.deb cedarlogic_${VERSION}_arm64.deb
CedarLogic-Native-$V3-x86_64.AppImage CedarLogic-Native-$V3-aarch64.AppImage SHA256SUMS SHA256SUMS.sig"

# A file from this checkout, or (dry run, PR #20 not merged here) from the copy branch.
from_repo() {   # path out -> 0 found here, 2 taken from the copy branch, 1 neither
	if [ -e "$ROOT/$1" ]; then cp -R "$ROOT/$1" "$2"; return 0; fi
	if [ "$MODE" = dry ] && git -C "$ROOT" cat-file -e "$COPY_BRANCH:$1" 2>/dev/null; then
		git -C "$ROOT" archive "$COPY_BRANCH" "$1" | tar -x -C "$WORKDIR/copy-branch" 2>/dev/null
		cp -R "$WORKDIR/copy-branch/$1" "$2"
		return 2
	fi
	return 1
}
mkdir -p "$WORKDIR/copy-branch"

# The Mac's updater feed: exactly one item for vV's zip; its build, length and signature.
SU_KEY=$(sed -n 's/.*Add SUPublicEDKey string \([A-Za-z0-9+/=]*\).*/\1/p' "$ROOT/mac/build.sh" | head -1)
check_mac_feed() {   # [expected build]
	local want=${1:-} feed=$WORKDIR/feed.xml zip url line n
	url="https://github.com/$RELEASES_REPO/releases/download/$TAG/CedarLogic-Native-$VERSION-mac.zip"
	gh api "repos/$RELEASES_REPO/contents/appcast-native-beta.xml" -H "Accept: application/vnd.github.raw" > "$feed" || { res FAIL "can't read appcast-native-beta.xml"; return 1; }
	line=$(python3 -I - "$feed" "$url" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
items = re.findall(r"    <!-- item (\S+) (\S+) -->\n(.*?)    </item>\n", text, re.S)
hits = []
for key, v, blk in items:
    m = re.search(r'<enclosure url="([^"]+)"', blk)
    if m and m.group(1) == sys.argv[2]:
        sig = re.search(r'sparkle:edSignature="([^"]+)"', blk)
        ln = re.search(r'length="(\d+)"', blk)
        hits.append("%s %s %s %s" % (key, v, ln.group(1) if ln else "?", sig.group(1) if sig else "-"))
print(len(hits))
print("\n".join(hits))
PY
)
	n=$(head -1 <<<"$line")
	if [ "$n" = 0 ]; then pend "the beta feed has no item for $TAG's Mac zip yet (step 3)"; return; fi
	if [ "$n" != 1 ]; then res FAIL "the beta feed has $n items for $TAG's Mac zip: the older ones are stale (same file, old signature); remove them from appcast-native-beta.xml"; return 1; fi
	read -r key build len sig <<<"$(sed -n 2p <<<"$line")"
	say "  beta feed: one item for $TAG's zip: $key build $build, length $len"
	zip=$WORKDIR/sparkle/CedarLogic-Native-$VERSION-mac.zip
	mkdir -p "$WORKDIR/sparkle"
	gh release download "$TAG" -R "$RELEASES_REPO" -D "$WORKDIR/sparkle" -p "CedarLogic-Native-$VERSION-mac.zip" --clobber
	[ "$(size_of "$zip")" = "$len" ] || { res FAIL "the feed says $len bytes, $TAG's zip is $(size_of "$zip")"; return 1; }
	if verify_sparkle_sig "$zip" "$sig" "$SU_KEY" "$WORKDIR/sparkle"; then
		say "  its edSignature verifies against the app's SUPublicEDKey ($SU_KEY)"
	else
		res FAIL "the feed's edSignature for $TAG's zip doesn't verify against SUPublicEDKey: Macs would refuse the update"; return 1
	fi
	rm -f "$zip"
	if [ -n "$want" ] && [ "$build" != "$want" ]; then
		if [ "$MODE" = dry ]; then res PENDING "the feed's $TAG item is build $build (beta 2); after step 3 it must be $want"
		else res FAIL "the feed's $TAG item is build $build, not $want"; return 1; fi
	else
		res OK "Mac feed: one item for $TAG's zip, build $build, signature valid"
	fi
}

# ---- the steps ---------------------------------------------------------------------------
step_0() {
	local t u
	for t in git python3 curl unzip perl shasum; do command -v "$t" >/dev/null || { res FAIL "needs $t"; return 1; }; done
	gh auth status >/dev/null 2>&1 || { res FAIL "gh isn't signed in (gh auth login)"; return 1; }
	u=$(git -C "$ROOT" remote get-url "$PRIVATE_REMOTE")
	case "$u" in *github.com[:/]"$SOURCE_REPO"|*github.com[:/]"$SOURCE_REPO".git) ;; *) res FAIL "remote $PRIVATE_REMOTE is $u, not $SOURCE_REPO"; return 1 ;; esac
	[ "$COMMIT" = "$(git -C "$ROOT" rev-parse "$PRIVATE_REMOTE/main")" ] || res WARN "the release commit isn't $PRIVATE_REMOTE/main's tip any more"
	if check_main_green "$SOURCE_REPO" "$COMMIT"; then res OK "main is green at ${COMMIT:0:12}"; else res FAIL "main isn't green at ${COMMIT:0:12}"; fi
	facts
	[ "$REL_EXISTS" = yes ] || { res FAIL "no release $TAG in $RELEASES_REPO"; return 1; }
	res OK "$TAG exists ($([ "$REL_PRE" = true ] && echo pre-release || echo full release)), files: $(tr '\n' ' ' <<<"$REL_NAMES")"
	# The Mac build must beat the newest build in the beta feed.
	newest=$(gh api "repos/$RELEASES_REPO/contents/appcast-native-beta.xml" -H "Accept: application/vnd.github.raw" \
		| sed -n 's/.*<!-- item macos \([0-9]*\) -->.*/\1/p' | sort -n | tail -1)
	if [ "$BUILD_NUM" -gt "${newest:-0}" ]; then res OK "the Mac build at ${COMMIT:0:12} is $BUILD_NUM, above the feed's newest ($newest)"
	else pend "the Mac build at ${COMMIT:0:12} would be $BUILD_NUM, not above the beta feed's newest ($newest, beta 2): Sparkle wouldn't offer it. Release from a newer main (the Actions panel change still has to merge, go-list step 0)"; fi
	if security find-generic-password -a cedarlogic-preview -s "CedarLogic preview update signing (Ed25519, PEM)" >/dev/null 2>&1; then
		res OK "the update key is in the Keychain (not read)"; else res FAIL "no update key in the Keychain (account cedarlogic-preview)"; fi
	local sign=''
	for t in "$ROOT/build/_deps/sparkle-src/bin/sign_update" "$ROOT/../../../build/_deps/sparkle-src/bin/sign_update" "$(command -v sign_update || true)"; do
		[ -n "$t" ] && [ -x "$t" ] && { sign=$t; break; }
	done
	if [ -n "$sign" ]; then res OK "Sparkle's sign_update: $sign"; else res WARN "Sparkle's sign_update not found where publish-update.sh looks (run from a worktree under .claude/worktrees/)"; fi
	if [ -n "${DMGBUILD:-}" ] || command -v dmgbuild >/dev/null; then res OK "dmgbuild: ${DMGBUILD:-$(command -v dmgbuild)}"; else res WARN "no dmgbuild (set DMGBUILD=<venv>/bin/dmgbuild for mac/package.sh)"; fi
	if [ -d "$TAP/.git" ]; then res OK "Homebrew tap checkout: $TAP"; else res WARN "no tap checkout at $TAP (git clone https://github.com/leviholliday/homebrew-tap $TAP)"; fi
	if [ -f "$SITE/releases.json" ] && [ -f "$SITE/scripts/release_day.py" ]; then res OK "site checkout: $SITE"; else res FAIL "no site checkout with releases.json and scripts/release_day.py at $SITE (--site)"; fi
	for t in packaging/scoop/bump.sh docs/drafts/Releases-README.md; do
		[ -e "$ROOT/$t" ] || res WARN "$t isn't in this checkout: merge PR #20 ($COPY_BRANCH) before release day"
	done
	if [ -f "$NOTES" ]; then
		if grep -q TODO-OWNER "$NOTES"; then res WARN "$NOTES has a TODO-OWNER note to check and remove (step 5 refuses until then)"; else res OK "notes: $NOTES"; fi
	else res FAIL "no notes file $NOTES"; fi
	if command -v shellcheck >/dev/null; then
		if shellcheck -x "$ROOT"/scripts/release-*.sh >/dev/null 2>&1; then res OK "shellcheck: the release scripts are clean"; else res WARN "shellcheck has notes on scripts/release-*.sh"; fi
	fi
}

step_1() {
	local args=(--commit "$COMMIT")
	[ "$MODE" = dry ] && args+=(--dry-run) || args+=(--go)
	[ "$YES" = 0 ] || args+=(--yes)
	if sub bash "$ROOT/scripts/release-ci.sh" "${args[@]}"; then
		if [ "$MODE" = dry ]; then res OK "release-ci.sh: main green, both branches fast-forward, sign-preview.sh --check-only passes on the current test builds"
		else res OK "both test builds are on ${COMMIT:0:12} and signed"; fi
	else res FAIL "scripts/release-ci.sh failed (above)"; return 1; fi
}

step_2() {
	local v b head dirty
	head=$(git -C "$ROOT" rev-parse HEAD)
	dirty=$(git -C "$ROOT" status --porcelain -- mac)
	if [ "$MODE" = go ] && [ "$APP_GIVEN" = 0 ]; then
		[ "$head" = "$COMMIT" ] || { res FAIL "this checkout is on ${head:0:12}, not the release commit ${COMMIT:0:12}: build from a clean checkout of it (git switch --detach $COMMIT)"; return 1; }
		[ -z "$dirty" ] || { res FAIL "uncommitted files under mac/ (the build number would get a +): $dirty"; return 1; }
		[ -n "${DMGBUILD:-}" ] || command -v dmgbuild >/dev/null || { res FAIL "set DMGBUILD=<venv>/bin/dmgbuild"; return 1; }
		(cd "$ROOT" && CLEAN=1 mac/build.sh) || { res FAIL "mac/build.sh failed"; return 1; }
		(cd "$ROOT" && mac/package.sh) || { res FAIL "mac/package.sh failed"; return 1; }
	elif [ "$MODE" = dry ]; then
		if [ "$head" = "$COMMIT" ]; then res OK "this checkout is on the release commit"
		else res PENDING "this checkout is on ${head:0:12}, not ${COMMIT:0:12}: release day builds from a clean checkout of it"; fi
		[ -z "$dirty" ] || res WARN "uncommitted files under mac/ here: $(tr '\n' ' ' <<<"$dirty")"
		say "would run: (cd $ROOT && CLEAN=1 mac/build.sh && DMGBUILD=\${DMGBUILD:-dmgbuild} mac/package.sh)"
	fi
	if [ -d "$APP" ]; then
		v=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist")
		b=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$APP/Contents/Info.plist")
		say "  $APP: version $v, build $b"
		[ "$v" = "$VERSION" ] || { res FAIL "the app says version $v, not $VERSION"; return 1; }
		[ "$b" = "$BUILD_NUM" ] || { [ "$MODE" = dry ] && res PENDING "the app here is build $b; the release commit's is $BUILD_NUM"; } || { res FAIL "the app is build $b, the release commit's is $BUILD_NUM"; return 1; }
		[ -f "$DMG" ] || pend "no $DMG (mac/package.sh)"
		res OK "Mac app $v build $b"
	else
		pend "no built app at $APP (this step builds it on release day)"
	fi
}

step_3() {
	local notes=$WORKDIR/mac-notes.md b
	[ -f "$MAC_NOTES" ] || { res FAIL "no Mac notes $MAC_NOTES"; return 1; }
	strip_comment "$MAC_NOTES" "$notes"
	if grep -q TODO-OWNER "$MAC_NOTES"; then
		[ "$MODE" = dry ] && res WARN "$MAC_NOTES has a TODO-OWNER note (the feed gets the file without its comment)" || { res FAIL "$MAC_NOTES still has a TODO-OWNER note"; return 1; }
	fi
	if [ -d "$APP" ]; then
		b=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$APP/Contents/Info.plist")
		if [ "$MODE" = dry ]; then
			say "publish-update.sh beta $VERSION --dry-run (it signs the zip locally with the Keychain's Sparkle key; nothing is published)"
			if sub bash "$ROOT/scripts/publish-update.sh" beta "$VERSION" --native "$APP" --no-ci --notes "$notes" --dry-run; then res OK "publish-update.sh beta --dry-run"; else res FAIL "publish-update.sh beta --dry-run failed"; fi
		else
			bash "$ROOT/scripts/publish-update.sh" beta "$VERSION" --native "$APP" --no-ci --notes "$notes" || { res FAIL "publish-update.sh beta failed"; return 1; }
			[ -f "$DMG" ] || { res FAIL "no $DMG to upload"; return 1; }
			gh release upload "$TAG" -R "$RELEASES_REPO" --clobber "$DMG" || { res FAIL "DMG upload failed"; return 1; }
		fi
		check_mac_feed "$b"
	else
		pend "no app at $APP for publish-update.sh (step 2); checking the feed as it is"
		say "would run: bash scripts/publish-update.sh beta $VERSION --native $APP --no-ci --notes <$MAC_NOTES without its comment>"
		say "would run: gh release upload $TAG -R $RELEASES_REPO --clobber $DMG"
		check_mac_feed "$BUILD_NUM"
	fi
}

testing_on_commit() {   # both test builds' tags on C?
	[ "$(ref_commit "$SOURCE_REPO" "$WIN_TAG")" = "$COMMIT" ] && [ "$(ref_commit "$SOURCE_REPO" "$LINUX_TAG")" = "$COMMIT" ]
}

step_4() {
	local args=(--files-only --commit "$COMMIT" --site "$SITE" --work "$WORKDIR/assets")
	if [ "$MODE" = go ]; then args+=(--go); [ "$YES" = 0 ] || args+=(--yes)
	else
		args+=(--dry-run --keep)
		if ! testing_on_commit; then
			res PENDING "the test builds aren't on ${COMMIT:0:12} until step 1: checking the current ones as stand-ins"
			args+=(--stand-in)
		fi
	fi
	if sub bash "$ROOT/scripts/release-assets.sh" "${args[@]}"; then
		res OK "release-assets.sh: 8 Windows/Linux files checked (signed list, CI artifacts, contents, site names)$([ "$MODE" = dry ] && echo ", staged in $WORKDIR/assets/stage" || echo ", uploaded to $TAG")"
		if [ "$MODE" = dry ] && grep -q "personal address" "$LOG"; then res WARN "the .debs' Maintainer is a personal address (see above)"; fi
	else res FAIL "scripts/release-assets.sh --files-only failed (above)"; return 1; fi
}

step_5() {
	local args=(--notes-only --notes "$NOTES" --work "$WORKDIR/notes")
	[ "$MODE" = go ] && args+=(--go) || args+=(--dry-run)
	[ "$YES" = 0 ] || args+=(--yes)
	if sub bash "$ROOT/scripts/release-assets.sh" "${args[@]}"; then res OK "the notes would be set on $TAG"
	else res FAIL "scripts/release-assets.sh --notes-only failed (above)"; return 1; fi
}

step_6() {
	local found stage=$WORKDIR/assets/stage out
	facts
	if [ "$MODE" = go ]; then
		found=$(bash "$ROOT/packaging/sign-release.sh" "$TAG" --check-only | tail -1) || { res FAIL "sign-release.sh --check-only failed"; return 1; }
		[ "$found" = "$BUILD_TIME" ] || { res FAIL "sign-release.sh finds build $found, but the release commit's time is $BUILD_TIME: the files aren't all from ${COMMIT:0:12}"; return 1; }
		confirm "Sign $TAG (build $BUILD_TIME) with the update key and upload SHA256SUMS?" || { res FAIL "not signed"; return 1; }
		bash "$ROOT/packaging/sign-release.sh" "$TAG" --build "$BUILD_TIME" --upload || { res FAIL "sign-release.sh --upload failed"; return 1; }
		res OK "$TAG signed, build $BUILD_TIME"
		return
	fi
	if [ "$HAS_WIN$HAS_LINUX" = yesyes ]; then
		if sub bash "$ROOT/packaging/sign-release.sh" "$TAG" --check-only; then
			found=$(tail -1 "$LOG")
			[ "$found" = "$BUILD_TIME" ] && res OK "sign-release.sh --check-only: build $found" || res FAIL "sign-release.sh finds build $found, not $BUILD_TIME"
		else res FAIL "sign-release.sh --check-only failed"; fi
	else
		res PENDING "$TAG has no Windows/Linux files until step 4: signing the staged stand-ins instead"
	fi
	# The stand-in release: the Mac's files now on vV + step 4's staged ones.
	[ -d "$stage" ] || { res PENDING "no staged files from step 4 (run steps 4 and 6 together)"; return; }
	gh release download "$TAG" -R "$RELEASES_REPO" -D "$stage" -p "CedarLogic-$VERSION-Mac.dmg" -p "CedarLogic-Native-$VERSION-mac.zip" --clobber
	if sub env CEDARLOGIC_SIGN_TEST_RELEASE="$stage" bash "$ROOT/packaging/sign-release.sh" "$TAG" --check-only; then
		found=$(tail -1 "$LOG")
		res OK "stand-in: sign-release.sh finds build $found from the signed test lists"
	elif grep -q "test builds disagree" "$LOG"; then
		res PENDING "stand-in: the two test builds are from different commits, so no one build number (on release day both are ${COMMIT:0:12}, build $BUILD_TIME)"
		found=$(sed -n 's/^build //p' "$WORKDIR/assets/src/linux/SHA256SUMS" 2>/dev/null || true)
	else res FAIL "stand-in: sign-release.sh --check-only failed (above)"; return 1; fi
	out=$WORKDIR/sim/signed
	rm -rf "$out"
	if sub env CEDARLOGIC_SIGN_TEST_RELEASE="$stage" bash "$ROOT/packaging/sign-release.sh" "$TAG" --test-key --build "${found:-$BUILD_TIME}" --out "$out"; then
		res OK "stand-in: SHA256SUMS for $TAG ($(grep -c '^file ' "$out/SHA256SUMS") files) signed with a throwaway key and verified"
	else res FAIL "stand-in: sign-release.sh --test-key failed"; return 1; fi
	say "would run: packaging/sign-release.sh $TAG --build $BUILD_TIME --upload"
}

step_7() {
	local args=(promote "$VERSION") missing='' f
	[ "$MODE" = dry ] && args+=(--dry-run)
	if [ "$MODE" = go ]; then
		facts
		[ "$HAS_SUMS" = yes ] || { res FAIL "$TAG isn't signed (step 6)"; return 1; }
		bash "$ROOT/scripts/publish-update.sh" "${args[@]}" || { res FAIL "publish-update.sh promote failed"; return 1; }
	else
		if sub bash "$ROOT/scripts/publish-update.sh" "${args[@]}"; then res OK "publish-update.sh promote --dry-run: $(grep -m1 '^promoted' "$LOG" || echo 'feeds diff above')"
		else res FAIL "publish-update.sh promote --dry-run failed"; fi
	fi
	facts
	for f in $EXPECTED_FILES; do grep -qx "$f" <<<"$REL_NAMES" || missing="$missing $f"; done
	local pre latest
	pre=$(gh release view "$TAG" -R "$RELEASES_REPO" --json isPrerelease --jq .isPrerelease)
	latest=$(gh api "repos/$RELEASES_REPO/releases/latest" --jq .tag_name 2>/dev/null || echo none)
	if [ "$pre" = false ] && [ "$latest" = "$TAG" ] && [ -z "$missing" ]; then
		res OK "$TAG is a full release, Latest, with all $(wc -w <<<"$EXPECTED_FILES" | tr -d ' ') files"
	else
		pend "$TAG: pre-release $pre, Latest is $latest, missing:${missing:- nothing}"
	fi
	say "would run: bash scripts/publish-update.sh promote $VERSION"
}

step_8() {
	say "by hand, on the devices (go-list step 8):"
	say "  Zorin laptop and the Pi:  sh linux/install.sh --dry-run   then   sh linux/install.sh"
	say "  Windows PC:  powershell -ExecutionPolicy Bypass -File packaging\\windows\\install.ps1 -SelfTest   then -DryRun, then a real install"
	say "here: sh linux/install.sh --dry-run --no-launch --version $VERSION (finds, downloads and checks; installs nothing)"
	if sub env CEDARLOGIC_ARCH=x86_64 sh "$ROOT/linux/install.sh" --dry-run --no-launch --version "$VERSION"; then
		res OK "install.sh --dry-run --version $VERSION checks out from here"
	else
		pend "install.sh --dry-run --version $VERSION doesn't pass yet: $(why)"
	fi
	if command -v pwsh >/dev/null; then
		if sub pwsh -NoProfile -File "$ROOT/packaging/windows/install.ps1" -SelfTest; then res OK "install.ps1 -SelfTest"; else res WARN "install.ps1 -SelfTest failed"; fi
	fi
	res OWNER "run the one-liners on the Linux laptop, the Pi and the Windows PC (above)"
}

step_9() {
	local args=("$VERSION")
	[ -d "$TAP/.git" ] && args+=(--tap "$TAP")
	if [ "$MODE" = dry ]; then
		args+=(--dry-run)
		if sub bash "$ROOT/packaging/homebrew/bump.sh" "${args[@]}"; then res OK "homebrew/bump.sh --dry-run: $(grep -m1 'would change' "$LOG" | sed 's/^bump: //' | cut -c1-160)"
		else res FAIL "homebrew/bump.sh --dry-run failed (above)"; fi
		grep -q "pre-release" "$LOG" && res PENDING "$TAG is still a pre-release (step 7 first)"
		say "would run: brew style leviholliday/tap/cedarlogic && brew audit --cask --online leviholliday/tap/cedarlogic"
		say "would run: git -C $TAP commit -am \"cedarlogic $VERSION\" && git -C $TAP push"
		[ -d "$TAP/.git" ] || res WARN "no tap checkout at $TAP"
		return 0
	fi
	[ -d "$TAP/.git" ] || { res FAIL "no tap checkout at $TAP"; return 1; }
	bash "$ROOT/packaging/homebrew/bump.sh" "${args[@]}" || { res FAIL "homebrew/bump.sh failed"; return 1; }
	brew style leviholliday/tap/cedarlogic && brew audit --cask --online leviholliday/tap/cedarlogic || { res FAIL "brew style/audit failed"; return 1; }
	git -C "$TAP" --no-pager diff
	confirm "Commit and push the tap ($TAP)?" || { res FAIL "tap not pushed"; return 1; }
	git -C "$TAP" commit -am "cedarlogic $VERSION" && git -C "$TAP" push || { res FAIL "tap commit/push failed"; return 1; }
	res OK "the tap's cask is at $VERSION"
}

step_10() {
	local bump=packaging/scoop/bump.sh dir=$WORKDIR/scoop rc=0 exists
	rm -rf "$dir"; mkdir -p "$dir"
	from_repo packaging/scoop "$dir/scoop" || rc=$?
	[ "$rc" != 1 ] || { res FAIL "no packaging/scoop here (merge PR #20)"; return 1; }
	[ "$rc" = 0 ] || res WARN "packaging/scoop isn't in this checkout: using $COPY_BRANCH's (merge PR #20 before release day)"
	bump=$dir/scoop/bump.sh
	if gh repo view leviholliday/scoop-cedarlogic >/dev/null 2>&1; then exists=yes; else exists=no; fi
	if [ "$MODE" = go ]; then
		[ "$rc" = 0 ] || { res FAIL "merge PR #20 first"; return 1; }
		bump=$ROOT/packaging/scoop/bump.sh
		if [ "$exists" = no ]; then
			confirm "Create the public repo leviholliday/scoop-cedarlogic?" || { res FAIL "bucket not made"; return 1; }
			gh repo create leviholliday/scoop-cedarlogic --public --description "Scoop bucket for CedarLogic, the free logic gate simulator" --homepage https://cedarlogic.netlify.app/ || { res FAIL "gh repo create failed"; return 1; }
		fi
		[ -d "$BUCKET/.git" ] || git clone https://github.com/leviholliday/scoop-cedarlogic "$BUCKET" || { res FAIL "clone failed"; return 1; }
		bash "$bump" "$VERSION" --bucket "$BUCKET" || { res FAIL "scoop/bump.sh failed"; return 1; }
		[ -f "$BUCKET/README.md" ] || printf '# CedarLogic for Scoop\n\n    scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic\n    scoop install cedarlogic/cedarlogic\n\nThe app: https://cedarlogic.netlify.app\n' > "$BUCKET/README.md"
		git -C "$BUCKET" add -A && git -C "$BUCKET" --no-pager diff --cached --stat
		confirm "Commit and push the bucket ($BUCKET)?" || { res FAIL "bucket not pushed"; return 1; }
		git -C "$BUCKET" commit -m "CedarLogic $VERSION" && git -C "$BUCKET" push || { res FAIL "bucket commit/push failed"; return 1; }
		res OK "the Scoop bucket has $VERSION"
		return
	fi
	if [ "$exists" = yes ]; then res OK "leviholliday/scoop-cedarlogic exists"; else say "would run: gh repo create leviholliday/scoop-cedarlogic --public ... (it doesn't exist yet, by design)"; fi
	if sub bash "$bump" "$VERSION" --manifest "$dir/scoop/cedarlogic.json" --dry-run; then res OK "scoop/bump.sh $VERSION --dry-run"
	else pend "scoop/bump.sh $VERSION --dry-run: $(why)(needs step 4)"; fi
	# Stand-in: v<V> as it will be, its zips being the test build's.
	gh api "repos/$SOURCE_REPO/releases/tags/$WIN_TAG" > "$dir/testing.json"
	python3 -I - "$dir/testing.json" "$VERSION" > "$dir/releases.json" <<'PY'
import json, sys
t = json.load(open(sys.argv[1])); v = sys.argv[2]
names = {"CedarLogic-Windows-native-x64.zip": "CedarLogic-%s-Windows-x64.zip" % v,
         "CedarLogic-Windows-native-ARM64.zip": "CedarLogic-%s-Windows-ARM64.zip" % v}
assets = [dict(a, name=names[a["name"]]) for a in t["assets"] if a["name"] in names]
print(json.dumps([{"tag_name": "v" + v, "draft": False, "prerelease": False, "assets": assets}]))
PY
	if sub env CEDARLOGIC_SCOOP_TEST_RELEASES="$dir/releases.json" bash "$bump" "$VERSION" --manifest "$dir/scoop/cedarlogic.json" --dry-run; then
		sed -n '/^{/,/^}/p' "$LOG" > "$WORKDIR/sim/scoop-manifest.json"
		res OK "stand-in: scoop/bump.sh checks the test build's zips (digest, CedarLogic/CedarLogic.exe) and writes the manifest"
	else res FAIL "stand-in: scoop/bump.sh failed (above)"; fi
	say "would run: git clone https://github.com/leviholliday/scoop-cedarlogic $BUCKET; packaging/scoop/bump.sh $VERSION --bucket $BUCKET; README; commit; push"
}

step_11() {
	local base=manifests/l/LeviHolliday/CedarLogic/$V3 out
	if [ "$MODE" = go ]; then
		bash "$ROOT/packaging/winget/bump.sh" "$VERSION" || { res FAIL "winget/bump.sh failed"; return 1; }
		res OK "winget manifests written: packaging/winget/$base"
	else
		out=$WORKDIR/winget/$base
		if sub bash "$ROOT/packaging/winget/bump.sh" "$VERSION" --out "$out"; then res OK "winget/bump.sh $VERSION (into $out)"
		else pend "winget/bump.sh $VERSION: $(why)(needs step 4)"; fi
		out=$WORKDIR/winget-standin/$base
		if sub bash "$ROOT/packaging/winget/bump.sh" "$VERSION" --repo "$SOURCE_REPO" --tag "$WIN_TAG" \
				--asset-x64 CedarLogic-Windows-Setup-x64.exe --asset-arm64 CedarLogic-Windows-Setup-ARM64.exe --out "$out"; then
			res OK "stand-in: winget/bump.sh checks the test build's installers (Inno Setup, version $V3) and writes the manifests"
			if grep -q 'not validated' "$LOG"; then res WARN "the winget manifests weren't schema-checked here (WINGET_VALIDATE_PYTHON=<venv with PyYAML + jsonschema>, or winget validate on the PC)"; fi
		else res FAIL "stand-in: winget/bump.sh failed (above)"; fi
	fi
	say "by hand (go-list step 11): on the PC, winget validate --manifest <dir> and winget install --manifest <dir> (packaging/winget/$base),"
	say "  then the PR to microsoft/winget-pkgs from leviholliday/winget-pkgs (packaging/winget/README.md; accept the CLA on the first one)"
	res OWNER "winget: validate and install on the PC, then open the winget-pkgs PR"
}

step_12() {
	local dir=$WORKDIR/releases-readme rc=0
	rm -rf "$dir"; mkdir -p "$dir"
	from_repo docs/drafts/Releases-README.md "$dir/draft.md" || rc=$?
	[ "$rc" != 1 ] || { res FAIL "no docs/drafts/Releases-README.md (merge PR #20)"; return 1; }
	[ "$rc" = 0 ] || res WARN "docs/drafts/Releases-README.md isn't in this checkout: using $COPY_BRANCH's (merge PR #20)"
	strip_comment "$dir/draft.md" "$dir/README.new.md"
	gh repo clone "$RELEASES_REPO" "$dir/repo" -- --depth 1 -q || { res FAIL "can't clone $RELEASES_REPO"; return 1; }
	cp "$dir/README.new.md" "$dir/repo/README.md"
	git -C "$dir/repo" --no-pager diff --stat >&2 || true
	if git -C "$dir/repo" diff --quiet; then res OK "the Releases README is already the draft"; return; fi
	if [ "$MODE" = dry ]; then
		res OK "the Releases README would change ($(git -C "$dir/repo" diff --shortstat | sed 's/^ //'))"
		say "would run: git commit -m \"README for CedarLogic $VERSION\" && git push (in a clone of $RELEASES_REPO)"
		return
	fi
	confirm "Commit and push the new README to $RELEASES_REPO?" || { res FAIL "README not pushed"; return 1; }
	git -C "$dir/repo" commit -qam "README for CedarLogic $VERSION" && git -C "$dir/repo" push -q || { res FAIL "README push failed"; return 1; }
	res OK "the Releases README is up"
}

step_13() {
	say "by hand (go-list step 13), in this repo:"
	say "  git add packaging/homebrew/Casks/cedarlogic.rb packaging/scoop/cedarlogic.json packaging/winget/manifests"
	say "  bring SUPPORT.md's versions up to date (0.4 everywhere, no \"until 0.4 is out\")"
	say "  git commit -m \"Package managers at $VERSION\" && git push $PRIVATE_REMOTE HEAD   (never origin)"
	say "  gh pr merge 20 -R $SOURCE_REPO --merge   (the docs PR, if it isn't merged yet)"
	git -C "$ROOT" status --short -- packaging SUPPORT.md >&2 || true
	res OWNER "commit the bumped manifests and SUPPORT.md, merge the docs PR"
}

step_14() {
	local rj=$WORKDIR/sim/releases.json fx=$WORKDIR/sim/fixtures
	[ -f "$SITE/scripts/release_day.py" ] || { res FAIL "no $SITE/scripts/release_day.py"; return 1; }
	cp "$SITE/releases.json" "$rj"
	say "python3 scripts/release_day.py --brew --scoop --dry-run (in $SITE, on a copy of releases.json)"
	if sub env CEDARLOGIC_RELEASES_JSON="$rj" python3 -I "$SITE/scripts/release_day.py" --brew --scoop --dry-run; then
		res OK "release_day.py --brew --scoop --dry-run passes: the site is ready for its release-day build"
	else
		pend "release_day.py --dry-run: $(why)"
	fi
	if [ "$MODE" = dry ] && [ -d "$WORKDIR/assets/stage" ]; then
		# Stand-in: v<V> as it will be after steps 3-10.
		rm -rf "$fx"; mkdir -p "$fx"
		[ -f "$WORKDIR/sim/signed/SHA256SUMS" ] && cp "$WORKDIR/sim/signed/SHA256SUMS" "$fx/SHA256SUMS"
		python3 -I - "$WORKDIR/assets/stage" "$TAG" "$RELEASES_REPO" "$fx" "$VERSION" "$ROOT/packaging/homebrew/Casks/cedarlogic.rb" "$WORKDIR/sim/scoop-manifest.json" <<'PY'
import base64, json, os, re, sys
stage, tag, repo, fx, version, cask, scoop = sys.argv[1:]
names = sorted(os.listdir(stage)) + ["SHA256SUMS", "SHA256SUMS.sig"]
assets = []
for n in names:
    p = os.path.join(stage, n)
    assets.append({"name": n, "size": os.path.getsize(p) if os.path.exists(p) else 64,
                   "browser_download_url": "https://github.com/%s/releases/download/%s/%s" % (repo, tag, n)})
rel = {"tag_name": tag, "draft": False, "prerelease": False, "assets": assets}
json.dump(rel, open(os.path.join(fx, "repos_%s_releases_tags_%s.json" % (repo.replace("/", "_"), tag)), "w"))
c = re.sub(r'^(\s*)version "[^"]*"', r'\g<1>version "%s"' % version, open(cask).read(), flags=re.M)
json.dump({"content": base64.b64encode(c.encode()).decode()}, open(os.path.join(fx, "repos_leviholliday_homebrew-tap_contents_Casks_cedarlogic.rb.json"), "w"))
if os.path.exists(scoop) and os.path.getsize(scoop):
    json.dump({"content": base64.b64encode(open(scoop, "rb").read()).decode()},
              open(os.path.join(fx, "repos_leviholliday_scoop-cedarlogic_contents_bucket_cedarlogic.json.json"), "w"))
PY
		cp "$SITE/releases.json" "$rj"
		if sub env CEDARLOGIC_RELEASES_JSON="$rj" CEDARLOGIC_RELEASE_DAY_FIXTURES="$fx" python3 -I "$SITE/scripts/release_day.py" --brew --scoop --dry-run; then
			res OK "stand-in: release_day.py --brew --scoop passes on $TAG as it will be (every file it links, signed list, tap and bucket at $VERSION)"
		else res FAIL "stand-in: release_day.py fails on $TAG as it will be: $(why)"; fi
	fi
	cat >&2 <<EOF
$ME: the website (go-list step 14), with the owner there (production): in $SITE's main checkout
    git pull && gh pr merge 1 --merge && gh pr merge 2 --merge && git pull
    scripts/sync_installers.sh "$MAIN_CHECKOUT" main main
    python3 scripts/release_day.py --brew --scoop --dry-run
    python3 scripts/release_day.py --brew --scoop
    python3 scripts/build_pages.py
    python3 scripts/check_links.py --remote
    npm run test:release-switches && npm run test:installers
    git add -A && git commit -m "CedarLogic $VERSION" && git push origin main
    npm run deploy
$ME: then: scripts/release-day.sh --go --from 15
EOF
	res OWNER "the website: the commands above, with the owner there (this step never deploys)"
}

step_15() {
	local u hdr loc bad=0 g
	for u in install.sh install.ps1; do
		hdr=$(curl -sI "https://cedarlogic.netlify.app/$u" | tr -d '\r')
		if grep -q '^HTTP/[0-9.]* 200' <<<"$hdr" && grep -qi '^content-type: text/plain' <<<"$hdr"; then say "  /$u: 200, text/plain"
		else say "  /$u: $(head -1 <<<"$hdr"), $(grep -i '^content-type' <<<"$hdr")"; bad=1; fi
	done
	[ "$bad" = 0 ] && res OK "the site serves install.sh and install.ps1 as text/plain" || res FAIL "install.sh / install.ps1 aren't served as 200 text/plain"
	bad=0
	for g in mac windows-x64 windows-arm64 windows-x64-zip windows-arm64-zip linux-amd64 linux-arm64 linux-x86_64-appimage linux-aarch64-appimage; do
		loc=$(curl -sI "https://cedarlogic.netlify.app/get/$g" | tr -d '\r' | sed -n 's/^[Ll]ocation: //p' | head -1)
		case "$loc" in *"/$RELEASES_REPO/releases/download/$TAG/"*) say "  /get/$g -> ${loc##*/}" ;; *) say "  /get/$g -> ${loc:-nothing}"; bad=1 ;; esac
	done
	[ "$bad" = 0 ] && res OK "every /get/ link lands on a $TAG file" || pend "not every /get/ link lands on a $TAG file yet (step 14)"
	cat >&2 <<EOF
$ME: by hand (go-list step 15):
    Mac: brew install --cask leviholliday/tap/cedarlogic on a Mac without CedarLogic (Open Anyway once), and the DMG from the site;
         a Mac on 0.3.4 (Normal) is offered $VERSION by CedarLogic > Check for Updates
    Windows: irm https://cedarlogic.netlify.app/install.ps1 | iex on a clean PC, the x64 installer from the site, the two Scoop lines
    Linux: curl -fsSL https://cedarlogic.netlify.app/install.sh | sh on Ubuntu or Zorin, the Pi and Fedora; a downloaded .deb by double-click
    each app: Settings > General > Testing group > Normal tester, Check Now: "up to date" at $VERSION
    the site: /whats-new/ shows $VERSION, no page says preview, test build or beta;
         python3 scripts/sync-live-check.py https://cedarlogic.netlify.app --production   (in $SITE)
EOF
	res OWNER "check every way in, as a user would (above)"
}

step_16() {
	say "later (go-list step 16): once the winget-pkgs PR is merged and winget install --id LeviHolliday.CedarLogic -e works on a clean PC:"
	say "  in $SITE: python3 scripts/release_day.py --winget, build, commit, deploy (owner there)"
	say "  drop \"once listed\" from this repo's README and the Releases README"
	res OWNER "winget, days later"
}

TITLES=("ready" "Windows and Linux builds from main (CI + sign-preview)" "the Mac build" "the Mac build onto $TAG, Beta first"
	"the Windows and Linux files onto $TAG, renamed" "the release notes for all three" "sign $TAG, last of the files"
	"out of beta: $TAG to everyone" "the one-liners against the real release" "Homebrew" "Scoop: the bucket"
	"winget: the manifests and the PR" "the Releases repo's README" "this repo" "the website (owner there)"
	"check every way in" "later: winget")
KINDS=(auto auto auto auto auto auto auto auto owner auto auto owner auto owner stop owner owner)

# ---- run ---------------------------------------------------------------------------------
trap 'echo >&2; say "stopped. Resume with: scripts/release-day.sh --$([ "$MODE" = go ] && echo go || echo dry-run) --from $STEP"' INT
for STEP in $(seq "$FROM" "$TO"); do
	echo >&2
	say "==== step $STEP: ${TITLES[$STEP]} ($([ "$MODE" = go ] && echo for real || echo dry run))"
	if [ "$MODE" = go ] && [ "${KINDS[$STEP]}" = auto ] && [ "$STEP" != 0 ]; then
		confirm "Run step $STEP (${TITLES[$STEP]})?" || { say "stopped before step $STEP. Resume with: scripts/release-day.sh --go --from $STEP"; exit 0; }
	fi
	set +e
	( set -e; "step_$STEP" )
	rc=$?
	set -e
	if [ "$rc" != 0 ] && ! grep -q "^$STEP	FAIL	" "$SUMMARY"; then res FAIL "step $STEP stopped (exit $rc; see above)"; fi
	if [ "$MODE" = go ]; then
		if grep -q "^$STEP	FAIL	" "$SUMMARY"; then
			say "step $STEP failed. Fix it, then: scripts/release-day.sh --go --from $STEP"; exit 1
		fi
		case "${KINDS[$STEP]}" in
			stop) say "step $STEP is the owner's (above). When it's done: scripts/release-day.sh --go --from $((STEP + 1))"; exit 0 ;;
			owner)
				if [ "$YES" = 1 ] || ! confirm "Step $STEP is by hand (above). Done?"; then
					say "when it's done: scripts/release-day.sh --go --from $((STEP + 1))"; exit 0
				fi ;;
		esac
	fi
done

# ---- summary ------------------------------------------------------------------------------
echo >&2
say "==== summary ($MODE run, CedarLogic $VERSION, commit ${COMMIT:0:12}, steps $FROM-$TO)"
awk -F'\t' '{ printf "  %2s  %-7s  %s\n", $1, $2, $3 }' "$SUMMARY" >&2
cp "$SUMMARY" "$WORKDIR/summary-last.tsv"
awk -F'\t' '{ c[$2]++ } END { s = ""; for (k in c) s = s k " " c[k] "  "; print "release-day: totals: " s }' "$SUMMARY" >&2
FAILED=0
if grep -q "	FAIL	" "$SUMMARY"; then FAILED=1; fi
if [ "$MODE" = dry ] && [ "$KEEP" = 0 ]; then
	rm -rf "$WORKDIR/assets" "$WORKDIR/notes" "$WORKDIR/sim" "$WORKDIR/tmp" "$WORKDIR/scoop" "$WORKDIR/winget" \
		"$WORKDIR/winget-standin" "$WORKDIR/releases-readme" "$WORKDIR/sparkle" "$WORKDIR/copy-branch" "$WORKDIR/feed.xml" "$WORKDIR/release.json" \
		"$WORKDIR/mac-notes.md" "$LOG" "$SUMMARY"
	say "downloads removed (--keep keeps them); the summary is in $WORKDIR/summary-last.tsv"
fi
exit "$FAILED"
