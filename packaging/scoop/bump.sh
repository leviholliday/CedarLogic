#!/usr/bin/env bash
# Point the Scoop manifest at a CedarLogic for Windows release.
#
#   packaging/scoop/bump.sh               the newest full release that has both Windows zips
#   packaging/scoop/bump.sh 0.4           that version (release v0.4)
#       --bucket DIR     a checkout of the bucket, github.com/leviholliday/scoop-cedarlogic
#                        (default $CEDARLOGIC_SCOOP_BUCKET): bump this repo's copy, then copy it
#                        to DIR/bucket/cedarlogic.json, so the two stay the same file
#       --manifest FILE  the manifest to rewrite (default: cedarlogic.json next to this script)
#       --repo O/R       the releases repo (default leviholliday/CedarLogic-Releases)
#       --dry-run        check everything, print the new values, change nothing
#
# What it does: finds the release (tag v<version>) and its two portable zips
# (CedarLogic-<version>-Windows-x64.zip and -ARM64.zip, the files the Windows
# app's own updater takes for Normal testers), downloads each, checks it
# against the sha256 GitHub keeps for the file and, when the release has a
# signed list, against SHA256SUMS (whose signature it checks with the update
# key, as linux/install.sh does; needs OpenSSL 3), checks the zip holds
# CedarLogic/CedarLogic.exe (the manifest's extract_dir and shortcut), then
# writes the version, both urls and both hashes into the manifest. It never
# commits or pushes: commit and push the bucket yourself (packaging/scoop/README.md).
set -euo pipefail

REPO=leviholliday/CedarLogic-Releases
HERE=$(cd "$(dirname "$0")" && pwd)
MANIFEST=$HERE/cedarlogic.json
BUCKET=${CEDARLOGIC_SCOOP_BUCKET:-}
BUCKET_URL=github.com/leviholliday/scoop-cedarlogic
VERSION='' DRY=0
OPENSSL=${OPENSSL:-openssl}

die() { echo "bump: $*" >&2; exit 1; }
say() { echo "bump: $*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--bucket) BUCKET=$2; shift 2 ;;
		--manifest) MANIFEST=$2; shift 2 ;;
		--repo) REPO=$2; shift 2 ;;
		--dry-run) DRY=1; shift ;;
		-h|--help) sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $1" ;;
		*) [ -z "$VERSION" ] || die "two versions given"; VERSION=${1#v}; shift ;;
	esac
done
[ -f "$MANIFEST" ] || die "no manifest at $MANIFEST"
BUCKET_FILE=
if [ -n "$BUCKET" ]; then
	BUCKET=${BUCKET%/}
	[ -d "$BUCKET/.git" ] || die "$BUCKET isn't a git checkout (clone https://$BUCKET_URL there first)"
	case "$(git -C "$BUCKET" remote get-url origin 2>/dev/null)" in
		*"$BUCKET_URL"|*"$BUCKET_URL.git"|*"${BUCKET_URL#github.com/}"|*"${BUCKET_URL#github.com/}.git") ;;
		*) say "note: $BUCKET's origin isn't $BUCKET_URL" ;;
	esac
	[ -z "$(git -C "$BUCKET" status --porcelain -- bucket/cedarlogic.json)" ] || die "$BUCKET/bucket/cedarlogic.json has uncommitted changes"
	BUCKET_FILE=$BUCKET/bucket/cedarlogic.json
fi
command -v curl >/dev/null || die "needs curl"
command -v python3 >/dev/null || die "needs python3"
command -v unzip >/dev/null || die "needs unzip"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-bump-scoop.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

api() {   # path -> JSON on stdout (gh when it's signed in: no rate limit)
	if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then gh api "$1"
	else curl -fsSL -H "Accept: application/vnd.github+json" "https://api.github.com/$1"; fi
}

# The release: one line per zip, "version<TAB>tag<TAB>kind<TAB>arch<TAB>name<TAB>url<TAB>digest",
# then the SHA256SUMS and .sig urls (or "-") on a last line.
# (For a test: CEDARLOGIC_SCOOP_TEST_RELEASES=FILE stands in for GitHub's list of releases.)
if [ -n "${CEDARLOGIC_SCOOP_TEST_RELEASES:-}" ]; then cp "$CEDARLOGIC_SCOOP_TEST_RELEASES" "$WORK/releases.json"
else api "repos/$REPO/releases?per_page=50" > "$WORK/releases.json" || die "couldn't list the releases of $REPO"; fi
python3 -I - "$WORK/releases.json" "$VERSION" > "$WORK/assets.tsv" <<'PY' || die "couldn't find the release"
import json, re, sys
releases = json.load(open(sys.argv[1]))
want = sys.argv[2]
for r in releases:
    tag = r.get("tag_name") or ""
    m = re.fullmatch(r"v?(\d+(?:\.\d+)+)", tag)
    if not m or r.get("draft"):
        continue
    v = m.group(1)
    if want and v != want:
        continue
    if not want and r.get("prerelease"):
        continue   # the newest *full* release, as Scoop's checkver sees it
    assets = {a.get("name"): a for a in r.get("assets") or []}
    names = {"64bit": "CedarLogic-%s-Windows-x64.zip" % v, "arm64": "CedarLogic-%s-Windows-ARM64.zip" % v}
    missing = [n for n in names.values() if n not in assets]
    if missing:
        if want:
            sys.exit("release %s has no %s" % (tag, ", ".join(missing)))
        continue   # a release without Windows files (a Mac-only fix, say)
    kind = "prerelease" if r.get("prerelease") else "release"
    for arch, n in names.items():
        a = assets[n]
        print("\t".join([v, tag, kind, arch, n, a["browser_download_url"], a.get("digest") or ""]))
    sums = assets.get("SHA256SUMS"), assets.get("SHA256SUMS.sig")
    print("\t".join(["sums", sums[0]["browser_download_url"] if sums[0] else "-", sums[1]["browser_download_url"] if sums[1] else "-"]))
    sys.exit(0)
sys.exit("no release %s with both Windows zips in the newest 50" % (("v" + want) if want else "(full, not a beta)"))
PY

SUMS_URL=$(awk -F'\t' '$1 == "sums" { print $2 }' "$WORK/assets.tsv")
SIG_URL=$(awk -F'\t' '$1 == "sums" { print $3 }' "$WORK/assets.tsv")
VERSION=$(awk -F'\t' '$1 != "sums" { print $1; exit }' "$WORK/assets.tsv")
TAG=$(awk -F'\t' '$1 != "sums" { print $2; exit }' "$WORK/assets.tsv")
KIND=$(awk -F'\t' '$1 != "sums" { print $3; exit }' "$WORK/assets.tsv")
[ "$KIND" = release ] || say "note: $TAG is a pre-release (a beta); the bucket is meant for full releases"
say "$TAG"

# The signed list, when the release has one (the update key's public half, as in linux/install.sh).
LIST=
if [ "$SUMS_URL" != - ] && [ "$SIG_URL" != - ]; then
	curl -fsSL -o "$WORK/SHA256SUMS" "$SUMS_URL" && curl -fsSL -o "$WORK/SHA256SUMS.sig" "$SIG_URL" || die "couldn't download $TAG's SHA256SUMS"
	cat > "$WORK/key.pem" <<'KEY'
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAQYTauigwwdtBa+eGmn2HN4gXDqHAAQSry/0BjX/jz0A=
-----END PUBLIC KEY-----
KEY
	"$OPENSSL" version 2>/dev/null | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.' || die "checking the signed list needs OpenSSL 3 or newer (set OPENSSL=)"
	"$OPENSSL" pkeyutl -verify -pubin -inkey "$WORK/key.pem" -rawin -in "$WORK/SHA256SUMS" -sigfile "$WORK/SHA256SUMS.sig" >/dev/null 2>&1 \
		|| die "$TAG's SHA256SUMS.sig doesn't verify with the update key"
	grep -qx "tag $TAG" "$WORK/SHA256SUMS" || die "$TAG's SHA256SUMS is for another release"
	LIST=$WORK/SHA256SUMS
	say "SHA256SUMS: signature verified"
else
	say "note: $TAG has no signed SHA256SUMS yet (packaging/sign-release.sh makes it; bump after signing)"
fi

: > "$WORK/new.tsv"
while IFS=$'\t' read -r v tag kind arch name url digest; do
	[ "$v" = sums ] && continue
	say "downloading $url"
	curl -fL --progress-bar -o "$WORK/$name" "$url"
	sha=$(shasum -a 256 "$WORK/$name" | cut -d' ' -f1)
	if [ -n "$digest" ]; then
		[ "$digest" = "sha256:$sha" ] || die "$name: sha256 is $sha, but GitHub's digest for it is $digest"
	fi
	if [ -n "$LIST" ]; then
		line=$(grep "^file $name " "$LIST" || true)
		[ -n "$line" ] || die "$name isn't in $TAG's SHA256SUMS"
		[ "${line##* }" = "$sha" ] || die "$name: sha256 is $sha, but SHA256SUMS says ${line##* }"
	fi
	unzip -l "$WORK/$name" | awk '{ print $NF }' | grep -qx 'CedarLogic/CedarLogic.exe' \
		|| die "$name has no CedarLogic/CedarLogic.exe (the manifest's extract_dir and shortcut name it)"
	say "$arch: $name, sha256 $sha"
	printf '%s\t%s\t%s\n' "$arch" "$url" "$sha" >> "$WORK/new.tsv"
done < "$WORK/assets.tsv"

python3 -I - "$MANIFEST" "$VERSION" "$WORK/new.tsv" "$DRY" <<'PY'
import json, sys
path, version, new, dry = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "1"
m = json.load(open(path, encoding="utf-8"))
old = m.get("version")
m["version"] = version
for line in open(new, encoding="utf-8"):
    arch, url, sha = line.rstrip("\n").split("\t")
    m["architecture"][arch]["url"] = url
    m["architecture"][arch]["hash"] = sha
text = json.dumps(m, indent=4, ensure_ascii=False) + "\n"
if dry:
    sys.stderr.write("bump: dry run: would change version %s -> %s in %s\n" % (old, version, path))
    sys.stdout.write(text)
else:
    open(path, "w", encoding="utf-8").write(text)
    sys.stderr.write("bump: updated %s: version %s -> %s\n" % (path, old, version))
PY
[ "$DRY" = 1 ] && exit 0
if [ -n "$BUCKET_FILE" ]; then
	mkdir -p "$BUCKET/bucket"
	cat "$MANIFEST" > "$BUCKET_FILE"
	say "updated $BUCKET_FILE (the same file)"
	say "next: commit and push $BUCKET, then on a Windows PC: scoop update; scoop install cedarlogic/cedarlogic (README)"
else
	say "next: bump the bucket (--bucket DIR), commit and push it"
fi
