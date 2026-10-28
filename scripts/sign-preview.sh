#!/usr/bin/env bash
# Sign a native Windows or Linux test build after CI has published it.
#
#   scripts/sign-preview.sh linux            sign what's on the linux-native-testing release now
#   scripts/sign-preview.sh windows          the same for windows-native-testing
#       --yes                                don't ask before signing (still shows what it signs)
#       --skip-ci-compare                    don't compare the release with the CI run's artifacts
#                                            (only when they've expired; everything else is checked)
#       --check-only                         every check, then stop: no key, no signing, no upload
#
#   scripts/sign-preview.sh --dry-run DIR [--tag T] [--commit C] [--build N] [--test-key] [--out D]
#                                            sign a manifest of the files in DIR locally; uploads
#                                            nothing. With --test-key, a throwaway key made here
#                                            (its public half printed) instead of the Keychain's.
#
# Why: CI builds and publishes the rolling test releases, but the apps (Windows
# and Linux updaters, linux/install.sh) install only files listed in a
# SHA256SUMS signed with the preview key, whose private half is only in the
# owner's macOS Keychain -- never on GitHub. A push to windows/native or
# linux/native therefore reaches no one until this script has looked at the
# result and signed it. (update-signing/UpdateSignature.h has the format.)
#
# Publishing a test build from now on:
#   1. push to linux/native (or windows/native)
#   2. wait for "Linux native build" ("Windows native build") to finish green,
#      including its publish job (gh run watch <id> -R leviholliday/CedarLogic)
#   3. scripts/sign-preview.sh linux (windows) -- checks, shows the commits
#      since the last one signed, asks, signs, uploads SHA256SUMS(.sig)
# Testers' apps see the build at their next update check after step 3.
#
# What it checks before signing, and refuses when any is off:
#   - the release's tag, the publishing branch and a successful CI run of the
#     right workflow are all on the same commit, and stay there while it works
#   - the release has exactly the files a build makes (no more, no fewer)
#   - each file's sha256 matches GitHub's digest for it, and the file of the
#     same name the CI run for that commit uploaded as an artifact
#     (install.sh: the one in the repo at that commit)
#   - the build (the commit's committer time) is newer than the last one this
#     Mac signed for that platform (~/.config/cedarlogic-signing/)
#   - the Keychain key's public half is the one the apps have built in, and
#     the signature it makes verifies against that public key
#
# The key: Keychain item, account "cedarlogic-preview", service
# "CedarLogic preview update signing (Ed25519, PEM)". It's read into a
# umask-077 temp file only for the one openssl call that signs, then deleted.
# Keep a backup of it somewhere safe (offline): if it's lost, a new key
# means a new public key in the apps, and every tester reinstalls by hand.
set -euo pipefail
umask 077

REPO=leviholliday/CedarLogic
PUBHEX=4184daba2830c1db416be7869a7d873788170ea1c00104abcbfd018d7fe3cf40
SPKI_PREFIX=302a300506032b6570032100   # DER SubjectPublicKeyInfo for an Ed25519 key, before the 32 bytes
KEY_ACCOUNT=cedarlogic-preview
KEY_SERVICE="CedarLogic preview update signing (Ed25519, PEM)"
HEADER="CedarLogic preview manifest 1"

die() { echo "sign-preview: $*" >&2; exit 1; }
say() { echo "sign-preview: $*" >&2; }

# ---- Tools --------------------------------------------------------------------
# OpenSSL 3 or later (macOS's own /usr/bin/openssl is LibreSSL, which can't do
# raw Ed25519 with pkeyutl).
find_openssl() {
	local c
	for c in "${OPENSSL:-}" openssl /opt/homebrew/opt/openssl@3/bin/openssl /usr/local/opt/openssl@3/bin/openssl \
	         /opt/homebrew/opt/openssl/bin/openssl /usr/local/opt/openssl/bin/openssl; do
		[ -n "$c" ] || continue
		command -v "$c" >/dev/null 2>&1 || continue
		if "$c" version 2>/dev/null | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.'; then echo "$c"; return 0; fi
	done
	return 1
}
OSSL=$(find_openssl) || die "needs OpenSSL 3 or later (brew install openssl@3, or set OPENSSL=/path/to/openssl)"

sha256_of() {
	if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
	else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
size_of() { wc -c < "$1" | tr -d ' '; }
hex_to_bin() { perl -e 'local $/; my $h = <STDIN>; $h =~ s/\s+//g; print pack("H*", $h)'; }
bin_to_hex() { perl -e 'local $/; print unpack("H*", <STDIN>)'; }

safe_name() { [[ ${#1} -le 128 && "$1" =~ ^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$ ]]; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-sign-preview.XXXXXX")
KEYFILE=
cleanup() {
	[ -n "$KEYFILE" ] && rm -f "$KEYFILE"
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

# A public key (64 hex digits) as a PEM file for openssl.
pub_pem() {
	printf '%s%s' "$SPKI_PREFIX" "$1" | hex_to_bin | "$OSSL" pkey -pubin -inform DER -out "$2" 2>/dev/null \
		|| die "couldn't make a public key from $1"
}

# The manifest of the files in $1 (every regular file but SHA256SUMS*), for
# tag $2, commit $3, build $4, into $5.
write_manifest() {
	local dir=$1 tag=$2 commit=$3 build=$4 out=$5 f name count=0
	[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || die "the commit isn't 40 lowercase hex digits: $commit"
	[[ "$build" =~ ^[1-9][0-9]{0,14}$ ]] || die "the build isn't a positive number: $build"
	safe_name "$tag" || die "bad tag: $tag"
	{
		printf '%s\n' "$HEADER"
		printf 'tag %s\n' "$tag"
		printf 'commit %s\n' "$commit"
		printf 'build %s\n' "$build"
	} > "$out"
	while IFS= read -r name; do
		f="$dir/$name"
		[ -f "$f" ] && [ ! -L "$f" ] || continue
		case "$name" in SHA256SUMS|SHA256SUMS.sig) continue ;; esac
		safe_name "$name" || die "a file name the apps would refuse: $name"
		[ "$(size_of "$f")" -gt 0 ] || die "$name is empty"
		printf 'file %s %s %s\n' "$name" "$(size_of "$f")" "$(sha256_of "$f")" >> "$out"
		count=$((count + 1))
	done < <(cd "$dir" && LC_ALL=C ls -1)
	[ "$count" -gt 0 ] || die "no files to sign in $dir"
}

# The Keychain key into a short-lived file, its public half checked, $1 signed into $2.
sign_with_keychain() {
	local manifest=$1 sig=$2 raw derived
	command -v security >/dev/null 2>&1 || die "the signing key is in the macOS Keychain: run this on the owner's Mac"
	KEYFILE=$(mktemp "$WORK/key.XXXXXX")
	raw=$(security find-generic-password -a "$KEY_ACCOUNT" -s "$KEY_SERVICE" -w 2>/dev/null) \
		|| { rm -f "$KEYFILE"; KEYFILE=; die "no Keychain item for account $KEY_ACCOUNT (service \"$KEY_SERVICE\")"; }
	# Stored hex-encoded (it's several lines); a PEM as it is.
	if [[ "$raw" == -----BEGIN* ]]; then printf '%s\n' "$raw" > "$KEYFILE"
	else printf '%s' "$raw" | xxd -r -p > "$KEYFILE"; fi
	raw=
	unset raw
	derived=$("$OSSL" pkey -in "$KEYFILE" -pubout -outform DER 2>/dev/null | tail -c 32 | bin_to_hex) || derived=
	if [ "$derived" != "$PUBHEX" ]; then
		rm -f "$KEYFILE"; KEYFILE=
		die "the Keychain key's public half isn't the one built into the apps ($PUBHEX); not signing"
	fi
	"$OSSL" pkeyutl -sign -inkey "$KEYFILE" -rawin -in "$manifest" -out "$sig" 2>/dev/null \
		|| { rm -f "$KEYFILE"; KEYFILE=; die "openssl couldn't sign"; }
	rm -f "$KEYFILE"
	KEYFILE=
}

# $1 signed into $2 with a key made just now; its public half (hex) on stdout.
sign_with_test_key() {
	local manifest=$1 sig=$2
	KEYFILE=$(mktemp "$WORK/testkey.XXXXXX")
	"$OSSL" genpkey -algorithm ed25519 -out "$KEYFILE" 2>/dev/null || die "openssl couldn't make a test key"
	"$OSSL" pkeyutl -sign -inkey "$KEYFILE" -rawin -in "$manifest" -out "$sig" 2>/dev/null || die "openssl couldn't sign"
	"$OSSL" pkey -in "$KEYFILE" -pubout -outform DER 2>/dev/null | tail -c 32 | bin_to_hex
	rm -f "$KEYFILE"
	KEYFILE=
}

verify_sig() {   # manifest sig pubhex
	local pem="$WORK/verify-pub.pem"
	pub_pem "$3" "$pem"
	[ "$(size_of "$2")" = 64 ] || return 1
	"$OSSL" pkeyutl -verify -pubin -inkey "$pem" -rawin -in "$1" -sigfile "$2" >/dev/null 2>&1
}

# ---- Dry run --------------------------------------------------------------------
if [ "${1:-}" = --dry-run ]; then
	shift
	DIR=${1:-}; [ -n "$DIR" ] && [ -d "$DIR" ] || die "--dry-run needs a folder of files"
	shift
	TAG=dry-run-testing COMMIT=$(printf '%040d' 0 | tr 0 a) BUILD=$(date +%s) TESTKEY=0 OUT=$DIR
	while [ $# -gt 0 ]; do
		case "$1" in
			--tag) TAG=$2; shift 2 ;;
			--commit) COMMIT=$2; shift 2 ;;
			--build) BUILD=$2; shift 2 ;;
			--test-key) TESTKEY=1; shift ;;
			--out) OUT=$2; shift 2 ;;
			*) die "unknown option $1" ;;
		esac
	done
	mkdir -p "$OUT"
	write_manifest "$DIR" "$TAG" "$COMMIT" "$BUILD" "$WORK/SHA256SUMS"
	if [ "$TESTKEY" = 1 ]; then
		KEY=$(sign_with_test_key "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig")
	else
		sign_with_keychain "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig"
		KEY=$PUBHEX
	fi
	verify_sig "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig" "$KEY" || die "the signature just made doesn't verify"
	# A changed manifest must not verify.
	sed 's/^build .*/build 1/' "$WORK/SHA256SUMS" > "$WORK/tampered"
	if verify_sig "$WORK/tampered" "$WORK/SHA256SUMS.sig" "$KEY"; then die "a changed manifest verified: something is badly wrong"; fi
	cp "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig" "$OUT/"
	if [ "$TESTKEY" = 1 ]; then pub_pem "$KEY" "$OUT/test-key.pub.pem"; fi
	cat "$OUT/SHA256SUMS" >&2
	say "dry run: signed and verified with $([ "$TESTKEY" = 1 ] && echo "a throwaway test key" || echo "the Keychain key") ($KEY); nothing uploaded"
	say "written: $OUT/SHA256SUMS and $OUT/SHA256SUMS.sig"
	echo "$KEY"
	exit 0
fi

# ---- The real thing ---------------------------------------------------------------
PLATFORM=${1:-}
shift || true
YES=0 SKIP_CI=0 CHECK_ONLY=0
while [ $# -gt 0 ]; do
	case "$1" in
		--yes) YES=1 ;;
		--skip-ci-compare) SKIP_CI=1 ;;
		--check-only) CHECK_ONLY=1 ;;
		*) die "unknown option $1" ;;
	esac
	shift
done
case "$PLATFORM" in
	linux)
		TAG=linux-native-testing BRANCH=linux/native WORKFLOW=linux-native.yml
		ARTIFACTS=(CedarLogic-Native-linux-appimage-x86_64 CedarLogic-Native-linux-appimage-aarch64
		           CedarLogic-Native-linux-deb-x86_64 CedarLogic-Native-linux-deb-aarch64)
		;;
	windows)
		TAG=windows-native-testing BRANCH=windows/native WORKFLOW=windows-native.yml
		ARTIFACTS=(CedarLogic-Windows-native-x64 CedarLogic-Windows-native-ARM64)
		;;
	*) die "usage: $0 linux|windows [--yes] [--skip-ci-compare]   or   $0 --dry-run DIR ..." ;;
esac
command -v gh >/dev/null 2>&1 || die "needs the GitHub CLI (gh)"

# Exactly the files a build publishes.
expected_files() {   # names on stdin; nonzero with a message when the set is wrong
	local names; names=$(cat)
	local want missing=0 n others
	if [ "$PLATFORM" = linux ]; then
		for want in '^CedarLogic-Native-[0-9A-Za-z.+_-]+-x86_64\.AppImage$' '^CedarLogic-Native-[0-9A-Za-z.+_-]+-aarch64\.AppImage$' \
		            '^cedarlogic_amd64\.deb$' '^cedarlogic_arm64\.deb$' '^install\.sh$'; do
			n=$(grep -Ec "$want" <<<"$names" || true)
			[ "$n" = 1 ] || { say "expected one file matching $want, found $n"; missing=1; }
		done
		others='^(CedarLogic-Native-[0-9A-Za-z.+_-]+-(x86_64|aarch64)\.AppImage|cedarlogic_(amd64|arm64)\.deb|install\.sh)$'
	else
		for want in '^CedarLogic-Windows-native-x64\.zip$' '^CedarLogic-Windows-native-ARM64\.zip$'; do
			n=$(grep -Ec "$want" <<<"$names" || true)
			[ "$n" = 1 ] || { say "expected $want, found $n"; missing=1; }
		done
		others='^(CedarLogic-Windows-native-(x64|ARM64)\.zip|CedarLogic-Windows-Setup-(x64|ARM64)\.exe)$'
	fi
	n=$(grep -Evc "$others" <<<"$names" || true)
	[ "$n" = 0 ] || { say "the release has files a build doesn't make:"; grep -Ev "$others" <<<"$names" >&2 || true; missing=1; }
	return $missing
}

release_state() {   # name size digest updated, one asset a line, without the manifest
	gh api "repos/$REPO/releases/tags/$TAG" \
		--jq '.assets[] | select(.name != "SHA256SUMS" and .name != "SHA256SUMS.sig") | [.name, (.size|tostring), (.digest // ""), .updated_at] | join(" ")' \
		| LC_ALL=C sort
}

say "checking $TAG on $REPO"
COMMIT=$(gh api "repos/$REPO/commits/$TAG" --jq .sha) || die "couldn't read the tag $TAG"
BUILD=$(gh api "repos/$REPO/commits/$COMMIT" --jq '.commit.committer.date | fromdateiso8601') || die "couldn't read the commit's time"
SUBJECT=$(gh api "repos/$REPO/commits/$COMMIT" --jq '.commit.message | split("\n")[0]')
HEAD=$(gh api "repos/$REPO/commits/$BRANCH" --jq .sha) || die "couldn't read $BRANCH"
[[ "$COMMIT" =~ ^[0-9a-f]{40}$ ]] || die "the tag's commit isn't a commit: $COMMIT"
[ "$HEAD" = "$COMMIT" ] || die "$BRANCH is on ${HEAD:0:12} but the release's tag is on ${COMMIT:0:12}: CI is still publishing a newer push, or the tag was moved by hand. Wait for CI, then run this again."

RUN=$(gh run list -R "$REPO" --workflow "$WORKFLOW" --branch "$BRANCH" --commit "$COMMIT" --limit 20 \
	--json databaseId,status,conclusion --jq '[.[] | select(.status == "completed" and .conclusion == "success")][0].databaseId // empty')
[ -n "$RUN" ] || die "no successful $WORKFLOW run on $BRANCH for ${COMMIT:0:12} (still running? failed?): not signing"
PENDING=$(gh run list -R "$REPO" --workflow "$WORKFLOW" --branch "$BRANCH" --limit 20 --json status --jq '[.[] | select(.status != "completed")] | length')
[ "$PENDING" = 0 ] || die "a $WORKFLOW run on $BRANCH is still going: it may publish over this release. Wait for it, then run this again."

BEFORE=$(release_state)
[ -n "$BEFORE" ] || die "the release has no files"
cut -d' ' -f1 <<<"$BEFORE" | expected_files || die "the release's files aren't what a build publishes: not signing"

say "downloading the release's files"
mkdir -p "$WORK/release"
while read -r name _; do
	safe_name "$name" || die "a file name the apps would refuse: $name"
	gh release download "$TAG" -R "$REPO" -D "$WORK/release" --pattern "$name" --clobber < /dev/null
done <<<"$BEFORE"

while read -r name size digest _; do
	f="$WORK/release/$name"
	[ -f "$f" ] || die "$name didn't download"
	[ "$(size_of "$f")" = "$size" ] || die "$name is $(size_of "$f") bytes, GitHub says $size"
	h=$(sha256_of "$f")
	[ -z "$digest" ] || [ "$digest" = "sha256:$h" ] || die "$name's sha256 is $h, GitHub's digest says $digest"
done <<<"$BEFORE"

if [ "$SKIP_CI" = 0 ]; then
	say "comparing with the artifacts of run $RUN (the CI run for ${COMMIT:0:12})"
	mkdir -p "$WORK/ci"
	for a in "${ARTIFACTS[@]}"; do
		gh run download "$RUN" -R "$REPO" -n "$a" -D "$WORK/ci/$a" || die "couldn't download artifact $a of run $RUN (expired? then --skip-ci-compare)"
	done
	if [ "$PLATFORM" = linux ]; then
		gh api "repos/$REPO/contents/linux/install.sh?ref=$COMMIT" -H "Accept: application/vnd.github.raw" > "$WORK/ci/install.sh" \
			|| die "couldn't read linux/install.sh at ${COMMIT:0:12}"
	fi
	while read -r name _; do
		ci=$(find "$WORK/ci" -type f -name "$name" | head -2)
		[ "$(printf '%s\n' "$ci" | grep -c .)" = 1 ] || die "the CI run has no single $name to compare with"
		[ "$(sha256_of "$ci")" = "$(sha256_of "$WORK/release/$name")" ] || die "$name on the release isn't the file CI run $RUN built"
	done <<<"$BEFORE"
fi

# Still the same release and commit after all that.
[ "$(gh api "repos/$REPO/commits/$TAG" --jq .sha)" = "$COMMIT" ] || die "the tag moved while this ran: run it again"
[ "$(release_state)" = "$BEFORE" ] || die "the release's files changed while this ran: run it again"

# Newer than the last build this Mac signed for this platform.
STATE_DIR=${XDG_CONFIG_HOME:-$HOME/.config}/cedarlogic-signing
LAST_FILE=$STATE_DIR/$PLATFORM-last
LAST_COMMIT= LAST_BUILD=0
if [ -f "$LAST_FILE" ]; then read -r LAST_COMMIT LAST_BUILD < "$LAST_FILE" || true; fi
if [ -n "$LAST_COMMIT" ] && [ "$LAST_COMMIT" = "$COMMIT" ]; then
	say "this commit was signed before; signing it again"
elif [ "${LAST_BUILD:-0}" -ge "$BUILD" ]; then
	die "build $BUILD (${COMMIT:0:12}) isn't newer than the last one signed, $LAST_BUILD (${LAST_COMMIT:0:12}): testers' apps would refuse it anyway"
fi

write_manifest "$WORK/release" "$TAG" "$COMMIT" "$BUILD" "$WORK/SHA256SUMS"
echo >&2
say "about to sign $TAG:"
say "  commit ${COMMIT} \"$SUBJECT\""
say "  build  $BUILD ($(date -r "$BUILD" 2>/dev/null || date -d "@$BUILD"))"
say "  CI run https://github.com/$REPO/actions/runs/$RUN"
if [ -n "$LAST_COMMIT" ] && [ "$LAST_COMMIT" != "$COMMIT" ]; then
	say "  new since the last signed build (${LAST_COMMIT:0:12}):"
	gh api "repos/$REPO/compare/$LAST_COMMIT...$COMMIT" --jq '.commits[] | "      " + .sha[0:9] + " " + (.commit.message | split("\n")[0])' >&2 || say "      (couldn't list them)"
fi
sed 's/^/      /' "$WORK/SHA256SUMS" >&2
if [ "$CHECK_ONLY" = 1 ]; then
	say "--check-only: everything checks out; nothing signed, nothing uploaded"
	exit 0
fi
if [ "$YES" = 0 ]; then
	printf 'Sign and upload? Every tester gets this build. [y/N] ' >&2
	read -r answer
	[[ "$answer" =~ ^[Yy] ]] || die "not signed"
fi

sign_with_keychain "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig"
verify_sig "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig" "$PUBHEX" || die "the signature doesn't verify with the apps' public key: not uploading"
[ "$(gh api "repos/$REPO/commits/$TAG" --jq .sha)" = "$COMMIT" ] || die "the tag moved while this ran: not uploading"

gh release upload "$TAG" -R "$REPO" "$WORK/SHA256SUMS" "$WORK/SHA256SUMS.sig" --clobber

# What testers will download: the same, and verifying.
mkdir -p "$WORK/back"
gh release download "$TAG" -R "$REPO" -D "$WORK/back" --pattern 'SHA256SUMS*'
cmp -s "$WORK/back/SHA256SUMS" "$WORK/SHA256SUMS" && cmp -s "$WORK/back/SHA256SUMS.sig" "$WORK/SHA256SUMS.sig" \
	|| die "what's on the release now isn't what was uploaded: look at it"
verify_sig "$WORK/back/SHA256SUMS" "$WORK/back/SHA256SUMS.sig" "$PUBHEX" || die "the uploaded signature doesn't verify: look at it"

mkdir -p "$STATE_DIR"
echo "$COMMIT $BUILD" > "$LAST_FILE"
say "signed: $TAG is ${COMMIT:0:12}, build $BUILD. Testers' apps take it at their next check."
