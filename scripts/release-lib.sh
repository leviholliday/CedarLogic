# shellcheck shell=bash
# Shared pieces for the release-day scripts (scripts/release-ci.sh,
# scripts/release-assets.sh, scripts/release-day.sh). Sourced, not run.
#
# Every script that sources this one has two modes and no default:
#   --dry-run   every check, downloads only into its work folder, and a
#               print-out of what it would change; changes nothing anywhere
#   --go        the real thing (the owner says "go" on release day)
# Run with neither, it refuses.

# The repos and names everything agrees on (packaging/README.md, "What the
# release must contain").
SOURCE_REPO=${SOURCE_REPO:-leviholliday/CedarLogic}
RELEASES_REPO=${RELEASES_REPO:-leviholliday/CedarLogic-Releases}
PRIVATE_REMOTE=${PRIVATE_REMOTE:-private}
# shellcheck disable=SC2034  # used by the scripts that source this
WIN_TAG=windows-native-testing WIN_BRANCH=windows/native WIN_WORKFLOW=windows-native.yml
# shellcheck disable=SC2034
LINUX_TAG=linux-native-testing LINUX_BRANCH=linux/native LINUX_WORKFLOW=linux-native.yml
# The update key the Windows and Linux apps and installers trust (scripts/sign-preview.sh's PUBHEX).
UPDATE_PUBHEX=4184daba2830c1db416be7869a7d873788170ea1c00104abcbfd018d7fe3cf40
ED25519_SPKI_PREFIX=302a300506032b6570032100
# shellcheck disable=SC2034
MANIFEST_HEADER="CedarLogic preview manifest 1"

: "${ME:=release}"
die() { echo "$ME: $*" >&2; exit 1; }
say() { echo "$ME: $*" >&2; }
warn() { echo "$ME: WARNING: $*" >&2; }

# MODE must be dry or go; refuses otherwise.
require_mode() {   # "$MODE"
	case "${1:-}" in
		dry|go) ;;
		*) die "say --dry-run (check everything, change nothing) or --go (the real thing, release day only)" ;;
	esac
}

sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
size_of() { wc -c < "$1" | tr -d ' '; }
hex_to_bin() { perl -e 'local $/; my $h = <STDIN>; $h =~ s/\s+//g; print pack("H*", $h)'; }

find_openssl() {   # OpenSSL 3+ on stdout (macOS's own openssl is LibreSSL: no raw Ed25519)
	local c
	for c in "${OPENSSL:-}" openssl /opt/homebrew/opt/openssl@3/bin/openssl /usr/local/opt/openssl@3/bin/openssl \
	         /opt/homebrew/opt/openssl/bin/openssl /usr/local/opt/openssl/bin/openssl; do
		[ -n "$c" ] || continue
		command -v "$c" >/dev/null 2>&1 || [ -x "$c" ] || continue
		if "$c" version 2>/dev/null | grep -Eq '^OpenSSL ([3-9]|[1-9][0-9])\.'; then echo "$c"; return 0; fi
	done
	return 1
}

# 0.4 -> 0.4.0 (the AppImages' and installers' three-part version).
pad_version() {
	local v=$1
	while [[ ! "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]]; do v=$v.0; done
	echo "$v"
}

# A raw Ed25519 public key (64 hex digits) as a PEM file.
pubhex_pem() {   # hex out.pem
	printf '%s%s' "$ED25519_SPKI_PREFIX" "$1" | hex_to_bin | "$OSSL" pkey -pubin -inform DER -out "$2" 2>/dev/null
}

# A signed list (SHA256SUMS + .sig) checked against the update key.
verify_signed_list() {   # SHA256SUMS SHA256SUMS.sig workdir
	local pem="$3/update-key.pem"
	pubhex_pem "$UPDATE_PUBHEX" "$pem" || return 1
	[ "$(size_of "$2")" = 64 ] || return 1
	"$OSSL" pkeyutl -verify -pubin -inkey "$pem" -rawin -in "$1" -sigfile "$2" >/dev/null 2>&1
}

# Sparkle's EdDSA signature (base64) of a file, against SUPublicEDKey (base64).
verify_sparkle_sig() {   # file sig-b64 pubkey-b64 workdir
	local hex pem="$4/sparkle-key.pem" sig="$4/sparkle.sig"
	hex=$(printf '%s' "$3" | base64 -d 2>/dev/null | perl -e 'local $/; print unpack("H*", <STDIN>)')
	[ ${#hex} = 64 ] || return 1
	pubhex_pem "$hex" "$pem" || return 1
	printf '%s' "$2" | base64 -d > "$sig" 2>/dev/null || return 1
	[ "$(size_of "$sig")" = 64 ] || return 1
	"$OSSL" pkeyutl -verify -pubin -inkey "$pem" -rawin -in "$1" -sigfile "$sig" >/dev/null 2>&1
}

# "name size digest" for each file of a release (digest may be empty).
release_assets() {   # repo tag
	gh api "repos/$1/releases/tags/$2" \
		--jq '.assets[] | [.name, (.size|tostring), (.digest // "")] | join(" ")' | LC_ALL=C sort
}

# The commit a tag (or branch) points at.
ref_commit() { gh api "repos/$1/commits/$2" --jq .sha; }

# The newest completed, successful run of a workflow on a branch for a commit.
green_run() {   # repo workflow branch commit
	gh run list -R "$1" --workflow "$2" --branch "$3" --commit "$4" --limit 20 \
		--json databaseId,status,conclusion \
		--jq '[.[] | select(.status == "completed" and .conclusion == "success")][0].databaseId // empty'
}

# Every workflow run on main for a commit finished green, and "CI" is one of them.
check_main_green() {   # repo commit
	local runs wf st co bad=0
	runs=$(gh run list -R "$1" --branch main --commit "$2" --limit 50 \
		--json workflowName,status,conclusion --jq '.[] | [.workflowName, .status, (.conclusion // "")] | join("\t")') || return 1
	[ -n "$runs" ] || { say "  main: no workflow runs for ${2:0:12} yet"; return 1; }
	while IFS=$'\t' read -r wf st co; do
		case "$st/$co" in
			completed/success|completed/skipped|completed/neutral) say "  main: $wf: $co" ;;
			completed/*) say "  main: $wf: $co"; bad=1 ;;
			*) say "  main: $wf: still $st"; bad=1 ;;
		esac
	done <<<"$runs"
	grep -q $'^CI\tcompleted\tsuccess$' <<<"$runs" || { say "  main: no green \"CI\" run"; bad=1; }
	return $bad
}

# Ask y/N on the terminal (YES=1 answers yes).
confirm() {   # prompt
	if [ "${YES:-0}" = 1 ]; then say "$1 [y/N] y (--yes)"; return 0; fi
	local a
	printf '%s [y/N] ' "$1" >&2
	read -r a < /dev/tty || return 1
	[[ "$a" =~ ^[Yy] ]]
}

# Run a command for real, or print it in a dry run.
act() {   # command...
	if [ "${MODE:-dry}" = go ]; then
		say "running: $(printf '%q ' "$@")"
		"$@"
	else
		say "would run: $(printf '%q ' "$@")"
	fi
}
