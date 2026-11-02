#!/usr/bin/env bash
# Write the winget manifests for a CedarLogic for Windows release.
#
#   packaging/winget/bump.sh 0.4             release v0.4 -> manifests/.../CedarLogic/0.4.0/
#       --package-version V   winget's version (default: the release's, padded to three
#                             numbers, 0.4 -> 0.4.0: what the installer shows in Settings > Apps)
#       --repo O/R            default leviholliday/CedarLogic-Releases
#       --tag T               default v<version>
#       --asset-x64 NAME      default CedarLogic-<version>-Windows-Setup-x64.exe
#       --asset-arm64 NAME    default CedarLogic-<version>-Windows-Setup-ARM64.exe
#       --out DIR             the version folder to write (default under manifests/ here)
#       --no-validate         skip validate.py at the end
#
# Downloads both installers, checks each against GitHub's sha256 digest for it
# and that it's an Inno Setup installer whose version is the package version,
# then writes the three manifests (copied from the newest version already
# here, so wording changes carry over) with the new version, URLs, hashes and
# release date, and validates them. It changes nothing on GitHub: the PR to
# microsoft/winget-pkgs is in packaging/winget/README.md.
#
# Try it on the rolling test build (writes to a scratch folder):
#   packaging/winget/bump.sh 0.4 --repo leviholliday/CedarLogic --tag windows-native-testing \
#     --asset-x64 CedarLogic-Windows-Setup-x64.exe --asset-arm64 CedarLogic-Windows-Setup-ARM64.exe \
#     --out /tmp/w/manifests/l/LeviHolliday/CedarLogic/0.4.0
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ID=LeviHolliday.CedarLogic
BASE=$HERE/manifests/l/LeviHolliday/CedarLogic
REPO=leviholliday/CedarLogic-Releases
VERSION='' PKGVER='' TAG='' X64='' ARM='' OUT='' VALIDATE=1

die() { echo "winget-bump: $*" >&2; exit 1; }
say() { echo "winget-bump: $*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--package-version) PKGVER=$2; shift 2 ;;
		--repo) REPO=$2; shift 2 ;;
		--tag) TAG=$2; shift 2 ;;
		--asset-x64) X64=$2; shift 2 ;;
		--asset-arm64) ARM=$2; shift 2 ;;
		--out) OUT=$2; shift 2 ;;
		--no-validate) VALIDATE=0; shift ;;
		-h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $1" ;;
		*) [ -z "$VERSION" ] || die "two versions given"; VERSION=${1#v}; shift ;;
	esac
done
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "give the release's version, like 0.4"
if [ -z "$PKGVER" ]; then
	PKGVER=$VERSION
	while [[ ! "$PKGVER" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]]; do PKGVER=$PKGVER.0; done
fi
[[ "$PKGVER" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "bad package version $PKGVER"
TAG=${TAG:-v$VERSION}
X64=${X64:-CedarLogic-$VERSION-Windows-Setup-x64.exe}
ARM=${ARM:-CedarLogic-$VERSION-Windows-Setup-ARM64.exe}
OUT=${OUT:-$BASE/$PKGVER}
command -v curl >/dev/null || die "needs curl"
command -v python3 >/dev/null || die "needs python3"

# The newest version folder here is the template (the new one itself, if it's there).
TEMPLATE=
if [ -f "$OUT/$ID.installer.yaml" ]; then TEMPLATE=$OUT
else
	TEMPLATE=$(find "$BASE" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort -t. -k1,1n -k2,2n -k3,3n -k4,4n | tail -1)
	[ -n "$TEMPLATE" ] || die "no manifests under $BASE to start from"
	TEMPLATE=$BASE/$TEMPLATE
fi
say "template: $TEMPLATE"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cl-bump-winget.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

api() {
	if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then gh api "$1"
	else curl -fsSL -H "Accept: application/vnd.github+json" "https://api.github.com/$1"; fi
}
api "repos/$REPO/releases/tags/$TAG" > "$WORK/release.json" || die "no release $TAG in $REPO"

# name -> "url<TAB>digest", plus the release's date and whether it's a pre-release.
INFO=$(python3 -I - "$WORK/release.json" "$X64" "$ARM" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
assets = {a["name"]: a for a in r.get("assets") or []}
out = []
for name in sys.argv[2:]:
    a = assets.get(name)
    if not a:
        sys.exit("the release has no %s (it has: %s)" % (name, ", ".join(sorted(assets)) or "nothing"))
    out.append(a["browser_download_url"] + "\t" + (a.get("digest") or ""))
out.append((r.get("published_at") or r.get("created_at") or "")[:10])
out.append("prerelease" if r.get("prerelease") or r.get("draft") else "release")
print("\n".join(out))
PY
) || die "the release isn't usable"
{ IFS=$'\t' read -r X64_URL X64_DIGEST; IFS=$'\t' read -r ARM_URL ARM_DIGEST; read -r DATE; read -r KIND; } <<<"$INFO"
[ "$KIND" = release ] || say "note: $TAG is a pre-release or draft; winget-pkgs should only get full releases"
[[ "$DATE" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || die "the release has no date"

fetch() {   # url digest name -> sha256 on stdout
	local url=$1 digest=$2 name=$3 sha
	say "downloading $url"
	curl -fL --progress-bar -o "$WORK/$name" "$url"
	sha=$(shasum -a 256 "$WORK/$name" | cut -d' ' -f1)
	if [ -n "$digest" ]; then
		[ "$digest" = "sha256:$sha" ] || die "$name: sha256 $sha, but GitHub's digest is $digest"
	else
		say "note: GitHub gave no digest for $name"
	fi
	# An Inno Setup installer, saying the version winget will expect.
	python3 -I - "$WORK/$name" "$PKGVER" <<'PY' || exit 1
import sys
data = open(sys.argv[1], "rb").read()
name, want = sys.argv[1].rsplit("/", 1)[-1], sys.argv[2]
if data[:2] != b"MZ":
    sys.exit("winget-bump: %s isn't a Windows program" % name)
if b"Inno Setup" not in data:
    sys.exit("winget-bump: %s isn't an Inno Setup installer (the manifest says InstallerType: inno)" % name)
key = "ProductVersion".encode("utf-16-le")
i = data.find(key)
if i < 0:
    sys.exit("winget-bump: %s has no ProductVersion in its version info" % name)
j = i + len(key)
while data[j:j + 2] == b"\0\0":
    j += 2
k = data.find(b"\0\0", j)
while k >= 0 and (k - j) % 2:
    k = data.find(b"\0\0", k + 1)
got = data[j:k].decode("utf-16-le", "replace").strip(" \0")
if got != want and not got.startswith(want + "."):
    sys.exit("winget-bump: %s says version %s, but the package version is %s (--package-version?)" % (name, got, want))
print("winget-bump: %s: Inno Setup, version %s" % (name, got), file=sys.stderr)
PY
	echo "$sha"
}
X64_SHA=$(fetch "$X64_URL" "$X64_DIGEST" "$X64")
ARM_SHA=$(fetch "$ARM_URL" "$ARM_DIGEST" "$ARM")
cmp -s "$WORK/$X64" "$WORK/$ARM" && die "the x64 and ARM64 installers are the same file"

mkdir -p "$OUT"
python3 -I - "$TEMPLATE" "$OUT" "$ID" "$PKGVER" "$DATE" "$X64_URL" "$X64_SHA" "$ARM_URL" "$ARM_SHA" <<'PY'
import os, re, sys
src, dst, pid, ver, date, x64_url, x64_sha, arm_url, arm_sha = sys.argv[1:]
urls = {"x64": (x64_url, x64_sha), "arm64": (arm_url, arm_sha)}
names = [pid + ".yaml", pid + ".installer.yaml"] + sorted(f for f in os.listdir(src) if f.startswith(pid + ".locale."))
for name in names:
    lines = open(os.path.join(src, name), encoding="utf-8").read().split("\n")
    out, arch, done = [], None, set()
    for line in lines:
        line = re.sub(r"^PackageVersion: .*$", "PackageVersion: " + ver, line)
        line = re.sub(r"^(\s*)DisplayVersion: .*$", r"\g<1>DisplayVersion: " + ver, line)
        line = re.sub(r"^ReleaseDate: .*$", "ReleaseDate: " + date, line)
        if line.startswith("# packaging/winget/bump.sh writes") or line.startswith("# the zeros below are placeholders"):
            continue
        m = re.match(r"^- Architecture: (\S+)$", line)
        if m:
            arch = m.group(1)
        if arch in urls:
            line = re.sub(r"^(\s+)InstallerUrl: .*$", lambda m: m.group(1) + "InstallerUrl: " + urls[arch][0], line)
            if re.match(r"^\s+InstallerSha256: ", line):
                line = re.sub(r"^(\s+)InstallerSha256: .*$", lambda m: m.group(1) + "InstallerSha256: " + urls[arch][1].upper(), line)
                done.add(arch)
        out.append(line)
    if name.endswith(".installer.yaml") and done != set(urls):
        sys.exit("winget-bump: the installer manifest's Installers aren't x64 and arm64 (found %s)" % sorted(done))
    text = "\n".join(out)
    if name.endswith(".installer.yaml") and not text.startswith("# yaml-language-server"):
        sys.exit("winget-bump: the template lost its schema line")
    with open(os.path.join(dst, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
print("winget-bump: wrote %s" % dst, file=sys.stderr)
PY

if [ "$VALIDATE" = 1 ]; then
	PY=python3
	python3 -c 'import yaml, jsonschema' 2>/dev/null || PY=${WINGET_VALIDATE_PYTHON:-}
	if [ -n "$PY" ]; then "$PY" "$HERE/validate.py" "$OUT"
	else say "not validated: needs PyYAML and jsonschema (set WINGET_VALIDATE_PYTHON=<venv>/bin/python3), or run winget validate on Windows"; fi
fi
say "next: the PR to microsoft/winget-pkgs (packaging/winget/README.md)"
